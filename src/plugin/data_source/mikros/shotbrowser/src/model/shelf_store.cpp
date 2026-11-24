// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/shelf_store.cpp
 * @brief Preference-backed shelf list.
 */

#include "model/shelf_store.hpp"

#include "xstudio/global_store/global_store.hpp"
#include "xstudio/utility/uuid.hpp"

#include <algorithm>
#include <exception>

namespace xstudio::mikros_shotbrowser {
namespace {

const char *kShelvesPreference = "/plugin/data_source/mikros_shotbrowser/shelves";
const char *kAllShelfId = "all";

/**
 * @brief Reads one shelf object from preference JSON.
 * @param entry Object with id, name, and filters (or legacy entity/where).
 * @return Shelf, or empty when required fields are missing.
 */
std::optional<poco::Shelf> shelfFromJson(const nlohmann::json &entry) {
    if (!entry.is_object() || !entry.contains("id") || !entry.contains("name")) {
        return std::nullopt;
    }
    poco::Shelf shelf;
    shelf.id = entry.at("id").get<std::string>();
    shelf.name = entry.at("name").get<std::string>();
    if (entry.contains("filters") && entry.at("filters").is_object()) {
        shelf.filters = entry.at("filters");
    } else {
        // Legacy shelves stored a single entity + where tree.
        const std::string legacyEntity = entry.value("entity", std::string());
        nlohmann::json legacyWhere = nullptr;
        if (entry.contains("where") && !entry.at("where").is_null()) {
            legacyWhere = entry.at("where");
        }
        shelf.filters = nlohmann::json::object();
        if (!legacyEntity.empty() && !legacyWhere.is_null()) {
            shelf.filters[legacyEntity] = std::move(legacyWhere);
        }
    }
    if (shelf.id.empty() || shelf.id == kAllShelfId) {
        return std::nullopt; ///< All is virtual; never persist it.
    }
    return shelf;
}

/**
 * @brief Writes one shelf for preference storage.
 * @param shelf Shelf to serialize.
 * @return JSON object.
 */
nlohmann::json shelfToJson(const poco::Shelf &shelf) {
    return nlohmann::json{
        {"id", shelf.id},
        {"name", shelf.name},
        {"filters", shelf.filters.is_null() ? nlohmann::json::object() : shelf.filters},
    };
}

} // namespace

const char *ShelfStore::preferencePath() { return kShelvesPreference; }

const char *ShelfStore::allShelfId() { return kAllShelfId; }

poco::Shelf ShelfStore::allShelf() {
    poco::Shelf shelf;
    shelf.id = kAllShelfId;
    shelf.name = "All";
    shelf.filters = nlohmann::json::object();
    return shelf;
}

void ShelfStore::load(plugin::StandardPlugin &panel) {
    shelves_.clear();
    try {
        global_store::GlobalStoreHelper preferences(panel.system());
        const nlohmann::json stored = preferences.value<nlohmann::json>(kShelvesPreference);
        if (!stored.is_array()) {
            return;
        }
        for (const nlohmann::json &entry : stored) {
            if (std::optional<poco::Shelf> shelf = shelfFromJson(entry)) {
                shelves_.push_back(std::move(*shelf));
            }
        }
    } catch (const std::exception &) {
        shelves_.clear(); ///< Missing preference leaves an empty user list.
    }
}

void ShelfStore::save(plugin::StandardPlugin &panel) const {
    try {
        global_store::GlobalStoreHelper preferences(panel.system());
        nlohmann::json array = nlohmann::json::array();
        for (const poco::Shelf &shelf : shelves_) {
            array.push_back(shelfToJson(shelf));
        }
        preferences.set_value(array, kShelvesPreference);
    } catch (const std::exception &) {
        ///< Read-only or missing preference tree: keep memory as the source of truth.
    }
}

std::string ShelfStore::upsert(poco::Shelf shelf) {
    if (shelf.id.empty() || shelf.id == kAllShelfId) {
        return {};
    }
    if (shelf.id == "new") {
        shelf.id = to_string(utility::Uuid::generate()); ///< Fresh id for a create from QML.
    }
    const std::string id = shelf.id;
    for (poco::Shelf &row : shelves_) {
        if (row.id == id) {
            row = std::move(shelf);
            return id;
        }
    }
    shelves_.push_back(std::move(shelf));
    return id;
}

void ShelfStore::remove(const std::string &id) {
    if (id.empty() || id == kAllShelfId) {
        return;
    }
    shelves_.erase(
        std::remove_if(
            shelves_.begin(),
            shelves_.end(),
            [&id](const poco::Shelf &row) { return row.id == id; }),
        shelves_.end());
    if (active_id_ == id) {
        active_id_.clear();
    }
}

bool ShelfStore::moveTo(const std::string &id, std::size_t toIndex) {
    if (id.empty() || id == kAllShelfId || shelves_.empty()) {
        return false;
    }
    const auto from = std::find_if(
        shelves_.begin(),
        shelves_.end(),
        [&id](const poco::Shelf &row) { return row.id == id; });
    if (from == shelves_.end()) {
        return false;
    }
    const std::size_t fromIndex = static_cast<std::size_t>(from - shelves_.begin());
    const std::size_t dest = std::min(toIndex, shelves_.size() - 1);
    if (fromIndex == dest) {
        return false;
    }
    poco::Shelf row = std::move(*from);
    shelves_.erase(from);
    shelves_.insert(shelves_.begin() + static_cast<std::ptrdiff_t>(dest), std::move(row));
    return true;
}

std::optional<poco::Shelf> ShelfStore::findById(const std::string &id) const {
    for (const poco::Shelf &row : shelves_) {
        if (row.id == id) {
            return row;
        }
    }
    return std::nullopt;
}

void ShelfStore::setActiveId(std::string id) { active_id_ = std::move(id); }

} // namespace xstudio::mikros_shotbrowser
