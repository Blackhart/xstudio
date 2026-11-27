// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/shelf_list.cpp
 * @brief Shelf list attributes and DAL selection.
 */

#include "viewModel/shelf_list.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"

#include "pms/expr_json.hpp"

#include <exception>
#include <map>
#include <memory>

namespace xstudio::mikros_shotbrowser {
namespace {

const char *kAttrsGroup = "mikros_shotbrowser_attributes";

/**
 * @brief Builds one list row for the QML shelf tree.
 * @param shelf Shelf to publish.
 * @param section `studio` or `user`.
 * @return JSON object.
 */
nlohmann::json shelfRowJson(const poco::Shelf &shelf, const char *section) {
    return nlohmann::json{
        {"id", shelf.id},
        {"name", shelf.name},
        {"filters", shelf.filters.is_null() ? nlohmann::json::object() : shelf.filters},
        {"section", section},
        {"editable", std::string(section) == "user"},
    };
}

/**
 * @brief Parses a filters object into a DAL map.
 * @param filters JSON object of entity → ExprNode.
 * @return Map for setActiveShelf. Empty when filters is empty/null.
 * @throw std::exception When a tree fails exprFromJson.
 */
std::map<std::string, std::shared_ptr<const pms::ExprNode>> filtersFromJson(
    const nlohmann::json &filters) {
    std::map<std::string, std::shared_ptr<const pms::ExprNode>> out;
    if (!filters.is_object()) {
        return out;
    }
    for (auto it = filters.begin(); it != filters.end(); ++it) {
        if (it.key() == "project" || it.value().is_null()) {
            continue; ///< Project is the panel scope; skip null trees.
        }
        out[it.key()] = pms::exprFromJson(it.value());
    }
    return out;
}

} // namespace

ShelfList::ShelfList(plugin::StandardPlugin &panel) : QObject(nullptr), panel_(panel) {
    store_.load(panel); ///< Preferences before attributes so the first publish is complete.
    initQmlAttributes(panel);
    publishShelves();
    publishShelfFields();
    selectAllByDefault(); ///< Startup default; mode switch calls it again.
}

void ShelfList::initQmlAttributes(plugin::StandardPlugin &panel) {
    shelves_ = panel.add_json_attribute("Shelves", "Shelves", nlohmann::json::array());
    shelves_->expose_in_ui_attrs_group(kAttrsGroup);
    selected_shelf_ = panel.add_string_attribute("SelectedShelf", "SelectedShelf", "");
    selected_shelf_->expose_in_ui_attrs_group(kAttrsGroup);
    shelf_fields_ =
        panel.add_json_attribute("ShelfFields", "ShelfFields", nlohmann::json::array());
    shelf_fields_->expose_in_ui_attrs_group(kAttrsGroup);
    upsert_shelf_ =
        panel.add_json_attribute("UpsertShelf", "UpsertShelf", nlohmann::json::object());
    upsert_shelf_->expose_in_ui_attrs_group(kAttrsGroup);
    remove_shelf_ = panel.add_string_attribute("RemoveShelf", "RemoveShelf", "");
    remove_shelf_->expose_in_ui_attrs_group(kAttrsGroup);
    move_shelf_ = panel.add_json_attribute("MoveShelf", "MoveShelf", nlohmann::json::object());
    move_shelf_->expose_in_ui_attrs_group(kAttrsGroup);
}

void ShelfList::publishShelves() {
    if (!shelves_) {
        return;
    }
    nlohmann::json tree = nlohmann::json::array();
    tree.push_back(shelfRowJson(ShelfStore::allShelf(), "studio")); ///< Project-wide All under Studio.
    for (const poco::Shelf &shelf : store_.shelves()) {
        tree.push_back(shelfRowJson(shelf, "user"));
    }
    shelves_->set_value(utility::JsonStore(tree));
}

void ShelfList::clearSelection() {
    store_.setActiveId({});
    dal::clearActiveShelf();
    if (selected_shelf_) {
        selected_shelf_->set_value("");
    }
}

void ShelfList::selectAllByDefault() {
    selectShelf(ShelfStore::allShelfId());
    if (selected_shelf_) {
        selected_shelf_->set_value(ShelfStore::allShelfId(), false); ///< UI only; selectShelf already ran.
    }
}

void ShelfList::publishShelfFields() {
    if (!shelf_fields_) {
        return;
    }
    nlohmann::json fields = nlohmann::json::array();
    for (const dal::FilterableField &field : dal::filterableFieldsForShelves()) {
        fields.push_back(
            {{"entity", field.entity},
             {"key", field.key},
             {"type", field.type},
             {"ops", field.ops}});
    }
    shelf_fields_->set_value(utility::JsonStore(fields));
}

void ShelfList::selectShelf(const std::string &shelfId) {
    if (shelfId.empty()) {
        log::info("ui select shelf none"); ///< A cleared shelf empties the result panes.
        clearSelection();
        emit onShelfSelected(); ///< Cleared selection still notifies so results empty.
        return;
    }
    if (shelfId == ShelfStore::allShelfId()) {
        const poco::Shelf all = ShelfStore::allShelf();
        log::info(
            "ui select shelf id=" + all.id + " name=" +
            log::quoted(all.name)); ///< All reloads the project with no extra Where.
        store_.setActiveId(shelfId);
        dal::setActiveShelf({}); ///< All: no extra Where / In.
        emit onShelfSelected();
        return;
    }
    const std::optional<poco::Shelf> shelf = store_.findById(shelfId);
    if (!shelf) {
        return;
    }
    store_.setActiveId(shelfId);
    try {
        dal::setActiveShelf(filtersFromJson(shelf->filters));
    } catch (const std::exception &) {
        return; ///< Bad JSON: leave the previous selection alone.
    }
    log::info(
        "ui select shelf id=" + shelf->id + " name=" +
        log::quoted(shelf->name)); ///< The shelf Where reloads the result pane.
    emit onShelfSelected();
}

void ShelfList::upsertFromJson(const nlohmann::json &payload) {
    if (!payload.is_object()) {
        return;
    }
    poco::Shelf shelf;
    shelf.id = payload.value("id", std::string("new"));
    shelf.name = payload.value("name", std::string());
    if (payload.contains("filters") && payload.at("filters").is_object()) {
        shelf.filters = payload.at("filters");
    } else {
        shelf.filters = nlohmann::json::object();
    }
    if (shelf.name.empty()) {
        return;
    }
    if (shelf.filters.contains("project")) {
        return; ///< Project stays the panel selection, never a shelf term.
    }
    try {
        (void)filtersFromJson(shelf.filters); ///< Reject illegal trees before save.
    } catch (const std::exception &) {
        return;
    }
    const std::string savedId = store_.upsert(std::move(shelf));
    store_.save(panel_);
    publishShelves();
    if (!savedId.empty() && store_.activeId() == savedId) {
        if (const std::optional<poco::Shelf> saved = store_.findById(savedId)) {
            log::info(
                "ui save shelf id=" + saved->id + " name=" +
                log::quoted(saved->name)); ///< The displayed shelf changed, so results reload.
        }
        selectShelf(savedId); ///< Keep the DAL Where in sync with the edited filters.
    }
}

void ShelfList::removeShelf(const std::string &shelfId) {
    const bool displayed = store_.activeId() == shelfId;
    store_.remove(shelfId);
    store_.save(panel_);
    if (store_.activeId().empty()) {
        if (displayed) {
            // The displayed shelf is gone, so results clear.
            log::info("ui remove shelf id=" + shelfId);
        }
        dal::clearActiveShelf();
        if (selected_shelf_) {
            selected_shelf_->set_value("");
        }
        emit onShelfSelected();
    }
    publishShelves();
}

void ShelfList::moveShelf(const std::string &shelfId, std::size_t toIndex) {
    if (!store_.moveTo(shelfId, toIndex)) {
        return;
    }
    store_.save(panel_);
    publishShelves();
}

void ShelfList::moveFromJson(const nlohmann::json &payload) {
    if (!payload.is_object()) {
        return;
    }
    const std::string id = payload.value("id", std::string());
    if (id.empty() || !payload.contains("toIndex") || !payload.at("toIndex").is_number_integer()) {
        return;
    }
    const auto toIndex = payload.at("toIndex").get<std::size_t>();
    moveShelf(id, toIndex);
}

void ShelfList::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value) {
        return;
    }
    if (selected_shelf_ && attr_uuid == selected_shelf_->uuid()) {
        selectShelf(selected_shelf_->value());
        return;
    }
    if (upsert_shelf_ && attr_uuid == upsert_shelf_->uuid()) {
        upsertFromJson(upsert_shelf_->value());
        upsert_shelf_->set_value(utility::JsonStore(nlohmann::json::object()));
        return;
    }
    if (remove_shelf_ && attr_uuid == remove_shelf_->uuid()) {
        const std::string id = remove_shelf_->value();
        remove_shelf_->set_value("");
        if (!id.empty()) {
            removeShelf(id);
        }
        return;
    }
    if (move_shelf_ && attr_uuid == move_shelf_->uuid()) {
        moveFromJson(move_shelf_->value());
        move_shelf_->set_value(utility::JsonStore(nlohmann::json::object()));
    }
}

} // namespace xstudio::mikros_shotbrowser
