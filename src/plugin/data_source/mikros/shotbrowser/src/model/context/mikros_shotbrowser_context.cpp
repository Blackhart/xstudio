// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_shotbrowser_context.cpp
 * @brief Async DAL loads and combo updates for the Mikros Shot Browser panel.
 */

#include "model/context/mikros_shotbrowser_context.hpp"

#include "model/log.hpp"

#include "xstudio/atoms.hpp"

namespace trace = xstudio::mikros_shotbrowser::log;

#include <map>
#include <thread>

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace {

/**
 * @brief Logs one selector after it has been resolved.
 * @tparam Row POCO with `id` and `name`.
 * @param entity `project`, `sequence`, `shot`, or `asset`.
 * @param row Chosen row, or empty when the selector is cleared.
 */
template <typename Row>
void logRow(const char *entity, const std::optional<Row> &row) {
    if (!row) {
        trace::select(entity, false, 0, {});
        return;
    }
    trace::select(entity, true, row->id, row->name);
}

template <typename Row>
std::vector<std::string> uniqueLabels(const std::vector<Row> &rows) {
    std::map<std::string, int> nameCounts;
    for (const Row &row : rows) {
        ++nameCounts[row.name];
    }
    std::vector<std::string> labels;
    labels.reserve(rows.size());
    for (const Row &row : rows) {
        if (nameCounts[row.name] > 1) {
            labels.push_back(row.name + " (" + std::to_string(row.id) + ")");
        } else {
            labels.push_back(row.name);
        }
    }
    return labels;
}

template <typename Row>
std::optional<Row> rowForLabel(const std::vector<Row> &rows, const std::string &label) {
    const std::vector<std::string> labels = uniqueLabels(rows);
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (labels[index] == label) {
            return rows[index];
        }
    }
    return std::nullopt;
}

template <typename Row>
utility::JsonStore namedRowsToJson(const std::vector<Row> &rows) {
    nlohmann::json out = nlohmann::json::array();
    for (const Row &row : rows) {
        out.push_back({{"id", row.id}, {"name", row.name}});
    }
    return utility::JsonStore(out);
}

template <typename Row>
std::vector<Row> namedRowsFromJson(const utility::JsonStore &json) {
    std::vector<Row> rows;
    if (!json.is_array()) {
        return rows;
    }
    rows.reserve(json.size());
    for (const auto &entry : json) {
        rows.push_back(Row{
            entry.at("id").template get<std::int64_t>(),
            entry.at("name").template get<std::string>(),
        });
    }
    return rows;
}

/**
 * @brief True when the reply's optional id matches the current selection.
 * @tparam Row POCO with an `id` field.
 * @param has_id Whether the worker sent an id for this filter.
 * @param id Id from the worker when `has_id` is true.
 * @param selected Current selection, or empty when none.
 */
template <typename Row>
bool selectionMatches(
    bool has_id,
    std::int64_t id,
    const std::optional<Row> &selected) {
    if (has_id) {
        return selected && selected->id == id;
    }
    return !selected;
}

/**
 * @brief Optional parents carried with a catalog page.
 *
 * Versions set whichever parents are selected.
 */
struct PageScope {
    bool has_sequence = false;     ///< True when `sequence_id` filters the page.
    std::int64_t sequence_id = 0;  ///< Sequence id when `has_sequence` is true.
    bool has_shot = false;         ///< True when `shot_id` filters the page.
    std::int64_t shot_id = 0;      ///< Shot id when `has_shot` is true.
    bool has_asset = false;        ///< True when `asset_id` filters the page.
    std::int64_t asset_id = 0;     ///< Asset id when `has_asset` is true.
};

/**
 * @brief Replaces or extends a catalog and refreshes its combo labels.
 *
 * The first page clears the combo value. Later pages keep a selection made
 * while earlier pages were already visible.
 *
 * @tparam Row POCO with `id` and `name`.
 * @param stored Rows accumulated for this combo.
 * @param attr Combo attribute.
 * @param rows Rows from the latest page.
 * @param append False for the first page of a request.
 */
template <typename Row>
void applyCatalogPage(
    std::vector<Row> &stored,
    module::StringChoiceAttribute *attr,
    std::vector<Row> rows,
    bool append) {
    if (!append) {
        stored = std::move(rows);
        attr->set_role_data(module::Attribute::StringChoices, uniqueLabels(stored));
        attr->set_value("");
        return;
    }
    if (rows.empty()) {
        return;
    }
    stored.insert(stored.end(), rows.begin(), rows.end());
    attr->set_role_data(module::Attribute::StringChoices, uniqueLabels(stored));
}

/**
 * @brief Loads every page of a catalog and mails each one to the actor.
 * @tparam Row POCO returned by `fetch`.
 * @tparam Fetch Callable taking a cursor and returning `dal::RowPage<Row>`.
 * @param weak Actor to receive the pages.
 * @param request_id Request that must still be current when the page is applied.
 * @param catalog Catalog name stored on the message (`project`, `sequence`, …).
 * @param project_id Parent project id, or 0 for the project catalog.
 * @param fetch One DAL page call.
 * @param scope Optional parents that must still match when the page is applied.
 */
template <typename Row, typename Fetch>
std::vector<Row> collectPages(Fetch fetch) {
    std::vector<Row> rows;
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<Row> page = fetch(cursor);
        rows.insert(rows.end(), page.rows.begin(), page.rows.end());
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
    }
    return rows;
}

template <typename Row, typename Fetch>
void publishPages(
    const caf::weak_actor_ptr &weak,
    std::uint64_t request_id,
    const std::string &catalog,
    std::int64_t project_id,
    Fetch fetch,
    PageScope scope = {}) {
    std::optional<std::string> cursor;
    bool append = false;
    for (;;) {
        const dal::RowPage<Row> page = fetch(cursor);
        if (auto handle = weak.lock()) {
            anon_mail(
                data_source::put_data_atom_v,
                request_id,
                catalog,
                project_id,
                scope.has_sequence,
                scope.sequence_id,
                scope.has_shot,
                scope.shot_id,
                scope.has_asset,
                scope.asset_id,
                append,
                namedRowsToJson(page.rows))
                .send(caf::actor_cast<caf::actor>(handle));
        }
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
        append = true;
    }
}

} // namespace

MikrosShotBrowserContext::MikrosShotBrowserContext(
    caf::actor_config &cfg, const utility::JsonStore &init_settings)
    : plugin::StandardPlugin(cfg, "MikrosShotBrowserPlugin", init_settings) {

    project_ = add_string_choice_attribute("Project", "Project", "", {}, {});
    project_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    sequence_ = add_string_choice_attribute("Sequence", "Sequence", "", {}, {});
    sequence_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    shot_ = add_string_choice_attribute("Shot", "Shot", "", {}, {});
    shot_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    shot_tree_ = add_json_attribute("ShotTree", "ShotTree", nlohmann::json::array());
    shot_tree_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    asset_ = add_string_choice_attribute("Asset", "Asset", "", {}, {});
    asset_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    version_ = add_string_choice_attribute("Version", "Version", "", {}, {});
    version_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    playlist_tree_ = add_json_attribute("PlaylistTree", "PlaylistTree", nlohmann::json::array());
    playlist_tree_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    make_behavior();
    connect_to_ui();
    loadProjectsAsync();

    register_ui_panel_qml(
        "Mikros Shot Browser",
        R"(
            import MikrosShotBrowser 1.0
            MikrosShotBrowserPanel {
                anchors.fill: parent
            }
        )",
        100.0f,
        "qrc:/mikros_shotbrowser_icons/mikros_shotbrowser.svg",
        5.0f);
}

std::optional<poco::Project> MikrosShotBrowserContext::projectForLabel(
    const std::string &label) const {
    return rowForLabel(projects_, label);
}

std::optional<poco::Sequence> MikrosShotBrowserContext::sequenceForLabel(
    const std::string &label) const {
    const nlohmann::json tree = shot_tree_ ? shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("name") || !node.contains("id")) {
            continue;
        }
        if (!node["name"].is_string() || node["name"].get<std::string>() != label) {
            continue;
        }
        return poco::Sequence{node["id"].get<std::int64_t>(), label};
    }
    return std::nullopt;
}

std::optional<poco::Shot> MikrosShotBrowserContext::shotForLabel(
    const std::string &label) const {
    return shotFromTree(label);
}

std::optional<poco::Shot> MikrosShotBrowserContext::shotFromTree(
    const std::string &label) const {
    const nlohmann::json tree = shot_tree_ ? shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("shots") || !node.contains("shotIds")) {
            continue;
        }
        const nlohmann::json &labels = node["shots"];
        const nlohmann::json &ids = node["shotIds"];
        if (!labels.is_array() || !ids.is_array()) {
            continue;
        }
        const std::size_t count = std::min(labels.size(), ids.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (!labels[index].is_string() || labels[index].get<std::string>() != label) {
                continue;
            }
            return poco::Shot{ids[index].get<std::int64_t>(), label};
        }
    }
    return std::nullopt;
}

std::optional<poco::Sequence> MikrosShotBrowserContext::sequenceForShotLabel(
    const std::string &label) const {
    const nlohmann::json tree = shot_tree_ ? shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("name") || !node.contains("id") ||
            !node.contains("shots")) {
            continue;
        }
        const nlohmann::json &labels = node["shots"];
        if (!labels.is_array()) {
            continue;
        }
        for (const auto &entry : labels) {
            if (!entry.is_string() || entry.get<std::string>() != label) {
                continue;
            }
            return poco::Sequence{
                node["id"].get<std::int64_t>(), node["name"].get<std::string>()};
        }
    }
    return std::nullopt;
}

std::optional<poco::Asset> MikrosShotBrowserContext::assetForLabel(
    const std::string &label) const {
    return rowForLabel(assets_, label);
}

void MikrosShotBrowserContext::clearSequenceBranch() {
    const bool had = selected_sequence_.has_value() || !sequence_->value().empty();
    selected_sequence_ = std::nullopt;
    if (!sequence_->value().empty()) {
        sequence_->set_value("", false);
    }
    if (had) {
        trace::select("sequence", false, 0, {});
    }
    clearShotSelection();
}

void MikrosShotBrowserContext::clearAssetBranch() {
    const bool had = selected_asset_.has_value() || !asset_->value().empty();
    selected_asset_ = std::nullopt;
    if (!asset_->value().empty()) {
        asset_->set_value("", false);
    }
    if (had) {
        trace::select("asset", false, 0, {});
    }
}

void MikrosShotBrowserContext::loadProjectsAsync() {
    ++project_request_id_;
    projects_.clear();
    selected_project_ = std::nullopt;
    project_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    project_->set_value("");

    const std::uint64_t request_id = project_request_id_;
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id]() {
        publishPages<poco::Project>(
            weak,
            request_id,
            "project",
            0,
            [](const std::optional<std::string> &cursor) {
                return dal::projects(dal::pageLimit(), cursor);
            });
    }).detach();
}

void MikrosShotBrowserContext::refreshSequences() {
    ++sequence_request_id_;
    const bool hadSequence = selected_sequence_.has_value() || !sequence_->value().empty();
    selected_sequence_ = std::nullopt;
    if (!sequence_->value().empty()) {
        sequence_->set_value("", false);
    }
    if (hadSequence) {
        trace::select("sequence", false, 0, {});
    }
    shot_tree_->set_value(nlohmann::json::array());

    if (!selected_project_) {
        return;
    }

    const std::uint64_t request_id = sequence_request_id_;
    const std::int64_t project_id = selected_project_->id;
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id]() {
        const std::vector<poco::Sequence> sequences = collectPages<poco::Sequence>(
            [project_id](const std::optional<std::string> &cursor) {
                return dal::sequences(project_id, dal::pageLimit(), cursor);
            });
        nlohmann::json tree = nlohmann::json::array();
        for (const poco::Sequence &sequence : sequences) {
            const std::vector<poco::Shot> shots = collectPages<poco::Shot>(
                [sequenceId = sequence.id](const std::optional<std::string> &cursor) {
                    return dal::shots(sequenceId, dal::pageLimit(), cursor);
                });
            nlohmann::json names = nlohmann::json::array();
            nlohmann::json ids = nlohmann::json::array();
            const std::vector<std::string> labels = uniqueLabels(shots);
            for (std::size_t index = 0; index < shots.size(); ++index) {
                names.push_back(labels[index]);
                ids.push_back(shots[index].id);
            }
            tree.push_back(
                {{"name", sequence.name},
                 {"id", sequence.id},
                 {"shots", std::move(names)},
                 {"shotIds", std::move(ids)}});
        }
        if (auto handle = weak.lock()) {
            anon_mail(
                data_source::put_data_atom_v,
                request_id,
                std::string("shot_tree"),
                project_id,
                false,
                std::int64_t{0},
                false,
                std::int64_t{0},
                false,
                std::int64_t{0},
                false,
                utility::JsonStore(std::move(tree)))
                .send(caf::actor_cast<caf::actor>(handle));
        }
    }).detach();
}

void MikrosShotBrowserContext::clearShotSelection() {
    const bool had = selected_shot_.has_value() || !shot_->value().empty();
    selected_shot_ = std::nullopt;
    if (!shot_->value().empty()) {
        shot_->set_value("", false);
    }
    if (had) {
        trace::select("shot", false, 0, {});
    }
}

void MikrosShotBrowserContext::refreshAssets() {
    ++asset_request_id_;
    assets_.clear();
    selected_asset_ = std::nullopt;
    asset_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    asset_->set_value("");

    if (!selected_project_) {
        return;
    }

    const std::uint64_t request_id = asset_request_id_;
    const std::int64_t project_id = selected_project_->id;
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id]() {
        publishPages<poco::Asset>(
            weak,
            request_id,
            "asset",
            project_id,
            [project_id](const std::optional<std::string> &cursor) {
                return dal::assets(project_id, dal::pageLimit(), cursor);
            });
    }).detach();
}

void MikrosShotBrowserContext::refreshPlaylistTree() {
    ++playlist_request_id_;
    playlist_tree_->set_value(nlohmann::json::array());

    if (!selected_project_) {
        return;
    }

    const std::uint64_t request_id = playlist_request_id_;
    const std::int64_t project_id = selected_project_->id;
    const std::optional<std::int64_t> sequence_id =
        selected_sequence_ ? std::optional<std::int64_t>{selected_sequence_->id} : std::nullopt;
    const std::optional<std::int64_t> shot_id =
        selected_shot_ ? std::optional<std::int64_t>{selected_shot_->id} : std::nullopt;
    const std::optional<std::int64_t> asset_id =
        selected_asset_ ? std::optional<std::int64_t>{selected_asset_->id} : std::nullopt;
    PageScope scope;
    scope.has_sequence = sequence_id.has_value();
    scope.sequence_id = sequence_id ? *sequence_id : std::int64_t{0};
    scope.has_shot = shot_id.has_value();
    scope.shot_id = shot_id ? *shot_id : std::int64_t{0};
    scope.has_asset = asset_id.has_value();
    scope.asset_id = asset_id ? *asset_id : std::int64_t{0};
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id, sequence_id, shot_id, asset_id, scope]() {
        const std::vector<poco::Playlist> playlists = collectPages<poco::Playlist>(
            [project_id](const std::optional<std::string> &cursor) {
                return dal::playlists(project_id, dal::pageLimit(), cursor);
            });
        nlohmann::json tree = nlohmann::json::array();
        for (const poco::Playlist &playlist : playlists) {
            const std::vector<poco::Version> versions = collectPages<poco::Version>(
                [project_id, sequence_id, shot_id, asset_id, playlistId = playlist.id](
                    const std::optional<std::string> &cursor) {
                    return dal::versions(
                        project_id,
                        dal::pageLimit(),
                        cursor,
                        sequence_id,
                        shot_id,
                        asset_id,
                        playlistId);
                });
            nlohmann::json names = nlohmann::json::array();
            for (const std::string &label : uniqueLabels(versions)) {
                names.push_back(label);
            }
            tree.push_back(
                {{"name", playlist.name}, {"id", playlist.id}, {"versions", std::move(names)}});
        }
        if (auto handle = weak.lock()) {
            anon_mail(
                data_source::put_data_atom_v,
                request_id,
                std::string("playlist_tree"),
                project_id,
                scope.has_sequence,
                scope.sequence_id,
                scope.has_shot,
                scope.shot_id,
                scope.has_asset,
                scope.asset_id,
                false,
                utility::JsonStore(std::move(tree)))
                .send(caf::actor_cast<caf::actor>(handle));
        }
    }).detach();
}

void MikrosShotBrowserContext::refreshVersions() {
    ++version_request_id_;
    versions_.clear();
    version_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    version_->set_value("");
    refreshPlaylistTree();

    if (!selected_project_) {
        return;
    }

    const std::uint64_t request_id = version_request_id_;
    const std::int64_t project_id = selected_project_->id;
    const std::optional<std::int64_t> sequence_id =
        selected_sequence_ ? std::optional<std::int64_t>{selected_sequence_->id} : std::nullopt;
    const std::optional<std::int64_t> shot_id =
        selected_shot_ ? std::optional<std::int64_t>{selected_shot_->id} : std::nullopt;
    const std::optional<std::int64_t> asset_id =
        selected_asset_ ? std::optional<std::int64_t>{selected_asset_->id} : std::nullopt;
    PageScope scope;
    scope.has_sequence = sequence_id.has_value();
    scope.sequence_id = sequence_id ? *sequence_id : std::int64_t{0};
    scope.has_shot = shot_id.has_value();
    scope.shot_id = shot_id ? *shot_id : std::int64_t{0};
    scope.has_asset = asset_id.has_value();
    scope.asset_id = asset_id ? *asset_id : std::int64_t{0};
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id, sequence_id, shot_id, asset_id, scope]() {
        publishPages<poco::Version>(
            weak,
            request_id,
            "version",
            project_id,
            [project_id, sequence_id, shot_id, asset_id](
                const std::optional<std::string> &cursor) {
                return dal::versions(
                    project_id, dal::pageLimit(), cursor, sequence_id, shot_id, asset_id);
            },
            scope);
    }).detach();
}

void MikrosShotBrowserContext::attribute_changed(
    const utility::Uuid &attr_uuid, const int role) {
    if (role != module::Attribute::Value) {
        plugin::StandardPlugin::attribute_changed(attr_uuid, role);
        return;
    }

    if (attr_uuid == project_->uuid()) {
        selected_project_ = projectForLabel(project_->value());
        logRow("project", selected_project_);
        refreshSequences();
        clearShotSelection();
        refreshAssets();
        refreshVersions();
    } else if (aligning_sequence_) {
        plugin::StandardPlugin::attribute_changed(attr_uuid, role);
        return;
    } else if (attr_uuid == sequence_->uuid()) {
        selected_sequence_ = sequenceForLabel(sequence_->value());
        logRow("sequence", selected_sequence_);
        if (selected_sequence_) {
            clearAssetBranch();
        }
        if (!selected_asset_) {
            clearShotSelection();
        }
        refreshVersions();
    } else if (attr_uuid == shot_->uuid()) {
        selected_shot_ = shotForLabel(shot_->value());
        logRow("shot", selected_shot_);
        if (selected_shot_) {
            clearAssetBranch();
            if (const std::optional<poco::Sequence> parent = sequenceForShotLabel(shot_->value())) {
                const bool realign = sequence_->value() != parent->name;
                selected_sequence_ = parent;
                if (realign) {
                    aligning_sequence_ = true;
                    sequence_->set_value(parent->name);
                    aligning_sequence_ = false;
                    logRow("sequence", selected_sequence_);
                }
            }
        }
        refreshVersions();
    } else if (attr_uuid == asset_->uuid()) {
        selected_asset_ = assetForLabel(asset_->value());
        logRow("asset", selected_asset_);
        if (selected_asset_) {
            clearSequenceBranch();
        } else {
            clearShotSelection();
        }
        refreshVersions();
    }

    plugin::StandardPlugin::attribute_changed(attr_uuid, role);
}

caf::message_handler MikrosShotBrowserContext::message_handler_extensions() {
    return caf::message_handler{
        [=](data_source::put_data_atom,
            const std::uint64_t request_id,
            const std::string &catalog,
            const std::int64_t project_id,
            const bool has_sequence,
            const std::int64_t sequence_id,
            const bool has_shot,
            const std::int64_t shot_id,
            const bool has_asset,
            const std::int64_t asset_id,
            const bool append,
            const utility::JsonStore &payload) {
            const auto stale = [&]() { trace::drop(catalog, request_id); };
            if (catalog == "project") {
                if (request_id != project_request_id_) {
                    stale();
                    return;
                }
                applyCatalogPage(
                    projects_, project_, namedRowsFromJson<poco::Project>(payload), append);
                return;
            }
            if (!selected_project_ || selected_project_->id != project_id) {
                stale();
                return;
            }
            if (catalog == "shot_tree") {
                if (request_id != sequence_request_id_) {
                    stale();
                    return;
                }
                shot_tree_->set_value(nlohmann::json(payload));
            } else if (catalog == "asset") {
                if (request_id != asset_request_id_) {
                    stale();
                    return;
                }
                applyCatalogPage(
                    assets_, asset_, namedRowsFromJson<poco::Asset>(payload), append);
            } else if (catalog == "playlist_tree") {
                if (request_id != playlist_request_id_) {
                    stale();
                    return;
                }
                if (!selectionMatches(has_sequence, sequence_id, selected_sequence_) ||
                    !selectionMatches(has_shot, shot_id, selected_shot_) ||
                    !selectionMatches(has_asset, asset_id, selected_asset_)) {
                    stale();
                    return;
                }
                playlist_tree_->set_value(nlohmann::json(payload));
            } else if (catalog == "version") {
                if (request_id != version_request_id_) {
                    stale();
                    return;
                }
                if (!selectionMatches(has_sequence, sequence_id, selected_sequence_) ||
                    !selectionMatches(has_shot, shot_id, selected_shot_) ||
                    !selectionMatches(has_asset, asset_id, selected_asset_)) {
                    stale();
                    return;
                }
                applyCatalogPage(
                    versions_, version_, namedRowsFromJson<poco::Version>(payload), append);
            }
        }};
}
