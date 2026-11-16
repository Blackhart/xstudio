// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_shotbrowser_context.cpp
 * @brief Async DAL loads and combo updates for the Mikros Shot Browser panel.
 */

#include "model/context/mikros_shotbrowser_context.hpp"

#include "xstudio/atoms.hpp"

#include <map>
#include <thread>

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace {

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

/** @brief Page size used when streaming catalog rows. */
constexpr std::size_t kPageLimit = 5;

/**
 * @brief Optional parents carried with a catalog page.
 *
 * Shots set the sequence fields. Versions set whichever parents are selected.
 */
struct PageScope {
    bool has_sequence = false;     ///< True when `sequence_id` filters the page.
    std::int64_t sequence_id = 0;  ///< Sequence id when `has_sequence` is true.
    bool has_shot = false;         ///< True when `shot_id` filters the page.
    std::int64_t shot_id = 0;      ///< Shot id when `has_shot` is true.
    bool has_asset = false;        ///< True when `asset_id` filters the page.
    std::int64_t asset_id = 0;     ///< Asset id when `has_asset` is true.
    bool has_playlist = false;     ///< True when `playlist_id` filters the page.
    std::int64_t playlist_id = 0;  ///< Playlist id when `has_playlist` is true.
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
                scope.has_playlist,
                scope.playlist_id,
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

    playlist_ = add_string_choice_attribute("Playlist", "Playlist", "", {}, {});
    playlist_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    version_ = add_string_choice_attribute("Version", "Version", "", {}, {});
    version_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

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
    return rowForLabel(sequences_, label);
}

std::optional<poco::Shot> MikrosShotBrowserContext::shotForLabel(
    const std::string &label) const {
    return rowForLabel(shots_, label);
}

std::optional<poco::Asset> MikrosShotBrowserContext::assetForLabel(
    const std::string &label) const {
    return rowForLabel(assets_, label);
}

std::optional<poco::Playlist> MikrosShotBrowserContext::playlistForLabel(
    const std::string &label) const {
    return rowForLabel(playlists_, label);
}

void MikrosShotBrowserContext::clearSequenceBranch() {
    ++shot_request_id_;
    selected_sequence_ = std::nullopt;
    selected_shot_ = std::nullopt;
    shots_.clear();
    if (!sequence_->value().empty()) {
        sequence_->set_value("", false);
    }
    shot_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    if (!shot_->value().empty()) {
        shot_->set_value("", false);
    }
}

void MikrosShotBrowserContext::clearAssetBranch() {
    selected_asset_ = std::nullopt;
    if (!asset_->value().empty()) {
        asset_->set_value("", false);
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
                return dal::projects(kPageLimit, cursor);
            });
    }).detach();
}

void MikrosShotBrowserContext::refreshSequences() {
    ++sequence_request_id_;
    sequences_.clear();
    selected_sequence_ = std::nullopt;
    sequence_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    sequence_->set_value("");
    shot_tree_->set_value(nlohmann::json::array());

    if (!selected_project_) {
        return;
    }

    const std::uint64_t request_id = sequence_request_id_;
    const std::int64_t project_id = selected_project_->id;
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id]() {
        publishPages<poco::Sequence>(
            weak,
            request_id,
            "sequence",
            project_id,
            [project_id](const std::optional<std::string> &cursor) {
                return dal::sequences(project_id, kPageLimit, cursor);
            });

        const std::vector<poco::Sequence> sequences = collectPages<poco::Sequence>(
            [project_id](const std::optional<std::string> &cursor) {
                return dal::sequences(project_id, kPageLimit, cursor);
            });
        nlohmann::json tree = nlohmann::json::array();
        for (const poco::Sequence &sequence : sequences) {
            const std::vector<poco::Shot> shots = collectPages<poco::Shot>(
                [project_id, sequenceId = sequence.id](const std::optional<std::string> &cursor) {
                    return dal::shots(project_id, kPageLimit, cursor, sequenceId);
                });
            nlohmann::json names = nlohmann::json::array();
            for (const std::string &label : uniqueLabels(shots)) {
                names.push_back(label);
            }
            tree.push_back({{"name", sequence.name}, {"shots", std::move(names)}});
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
                std::int64_t{0},
                false,
                utility::JsonStore(std::move(tree)))
                .send(caf::actor_cast<caf::actor>(handle));
        }
    }).detach();
}

void MikrosShotBrowserContext::refreshShots() {
    ++shot_request_id_;
    shots_.clear();
    selected_shot_ = std::nullopt;
    shot_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    shot_->set_value("");

    if (!selected_project_) {
        return;
    }

    const std::uint64_t request_id = shot_request_id_;
    const std::int64_t project_id = selected_project_->id;
    const std::optional<std::int64_t> sequence_id =
        selected_sequence_ ? std::optional<std::int64_t>{selected_sequence_->id} : std::nullopt;
    const bool has_sequence = sequence_id.has_value();
    const std::int64_t sequence_filter = sequence_id ? *sequence_id : std::int64_t{0};
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    PageScope scope;
    scope.has_sequence = has_sequence;
    scope.sequence_id = sequence_filter;
    std::thread([weak, request_id, project_id, sequence_id, scope]() {
        publishPages<poco::Shot>(
            weak,
            request_id,
            "shot",
            project_id,
            [project_id, sequence_id](const std::optional<std::string> &cursor) {
                return dal::shots(project_id, kPageLimit, cursor, sequence_id);
            },
            scope);
    }).detach();
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
                return dal::assets(project_id, kPageLimit, cursor);
            });
    }).detach();
}

void MikrosShotBrowserContext::refreshPlaylists() {
    ++playlist_request_id_;
    playlists_.clear();
    selected_playlist_ = std::nullopt;
    playlist_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    playlist_->set_value("");

    if (!selected_project_) {
        return;
    }

    const std::uint64_t request_id = playlist_request_id_;
    const std::int64_t project_id = selected_project_->id;
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id]() {
        publishPages<poco::Playlist>(
            weak,
            request_id,
            "playlist",
            project_id,
            [project_id](const std::optional<std::string> &cursor) {
                return dal::playlists(project_id, kPageLimit, cursor);
            });
    }).detach();
}

void MikrosShotBrowserContext::refreshVersions() {
    ++version_request_id_;
    versions_.clear();
    version_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    version_->set_value("");

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
    const std::optional<std::int64_t> playlist_id =
        selected_playlist_ ? std::optional<std::int64_t>{selected_playlist_->id} : std::nullopt;
    PageScope scope;
    scope.has_sequence = sequence_id.has_value();
    scope.sequence_id = sequence_id ? *sequence_id : std::int64_t{0};
    scope.has_shot = shot_id.has_value();
    scope.shot_id = shot_id ? *shot_id : std::int64_t{0};
    scope.has_asset = asset_id.has_value();
    scope.asset_id = asset_id ? *asset_id : std::int64_t{0};
    scope.has_playlist = playlist_id.has_value();
    scope.playlist_id = playlist_id ? *playlist_id : std::int64_t{0};
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id, sequence_id, shot_id, asset_id, playlist_id, scope]() {
        publishPages<poco::Version>(
            weak,
            request_id,
            "version",
            project_id,
            [project_id, sequence_id, shot_id, asset_id, playlist_id](
                const std::optional<std::string> &cursor) {
                return dal::versions(
                    project_id,
                    kPageLimit,
                    cursor,
                    sequence_id,
                    shot_id,
                    asset_id,
                    playlist_id);
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
        refreshSequences();
        refreshShots();
        refreshAssets();
        refreshPlaylists();
        refreshVersions();
    } else if (attr_uuid == sequence_->uuid()) {
        selected_sequence_ = sequenceForLabel(sequence_->value());
        if (selected_sequence_) {
            clearAssetBranch();
        }
        if (!selected_asset_) {
            refreshShots();
        }
        refreshVersions();
    } else if (attr_uuid == shot_->uuid()) {
        selected_shot_ = shotForLabel(shot_->value());
        if (selected_shot_) {
            clearAssetBranch();
        }
        refreshVersions();
    } else if (attr_uuid == asset_->uuid()) {
        selected_asset_ = assetForLabel(asset_->value());
        if (selected_asset_) {
            clearSequenceBranch();
        } else {
            refreshShots();
        }
        refreshVersions();
    } else if (attr_uuid == playlist_->uuid()) {
        selected_playlist_ = playlistForLabel(playlist_->value());
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
            const bool has_playlist,
            const std::int64_t playlist_id,
            const bool append,
            const utility::JsonStore &payload) {
            if (catalog == "project") {
                if (request_id != project_request_id_) {
                    return;
                }
                applyCatalogPage(
                    projects_, project_, namedRowsFromJson<poco::Project>(payload), append);
                return;
            }
            if (!selected_project_ || selected_project_->id != project_id) {
                return;
            }
            if (catalog == "sequence") {
                if (request_id != sequence_request_id_) {
                    return;
                }
                applyCatalogPage(
                    sequences_,
                    sequence_,
                    namedRowsFromJson<poco::Sequence>(payload),
                    append);
            } else if (catalog == "shot_tree") {
                if (request_id != sequence_request_id_) {
                    return;
                }
                shot_tree_->set_value(nlohmann::json(payload));
            } else if (catalog == "shot") {
                if (request_id != shot_request_id_ || selected_asset_) {
                    return;
                }
                if (!selectionMatches(has_sequence, sequence_id, selected_sequence_)) {
                    return;
                }
                applyCatalogPage(
                    shots_, shot_, namedRowsFromJson<poco::Shot>(payload), append);
            } else if (catalog == "asset") {
                if (request_id != asset_request_id_) {
                    return;
                }
                applyCatalogPage(
                    assets_, asset_, namedRowsFromJson<poco::Asset>(payload), append);
            } else if (catalog == "playlist") {
                if (request_id != playlist_request_id_) {
                    return;
                }
                applyCatalogPage(
                    playlists_,
                    playlist_,
                    namedRowsFromJson<poco::Playlist>(payload),
                    append);
            } else if (catalog == "version") {
                if (request_id != version_request_id_) {
                    return;
                }
                if (!selectionMatches(has_sequence, sequence_id, selected_sequence_) ||
                    !selectionMatches(has_shot, shot_id, selected_shot_) ||
                    !selectionMatches(has_asset, asset_id, selected_asset_) ||
                    !selectionMatches(has_playlist, playlist_id, selected_playlist_)) {
                    return;
                }
                applyCatalogPage(
                    versions_, version_, namedRowsFromJson<poco::Version>(payload), append);
            }
        }};
}
