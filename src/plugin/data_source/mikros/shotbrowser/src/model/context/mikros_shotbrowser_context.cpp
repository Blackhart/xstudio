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

/** @brief Page size used when streaming version names into the combo. */
constexpr std::size_t kVersionPageLimit = 5;

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

std::vector<std::string> MikrosShotBrowserContext::projectLabels() const {
    return uniqueLabels(projects_);
}

std::vector<std::string> MikrosShotBrowserContext::sequenceLabels() const {
    return uniqueLabels(sequences_);
}

std::vector<std::string> MikrosShotBrowserContext::shotLabels() const {
    return uniqueLabels(shots_);
}

std::vector<std::string> MikrosShotBrowserContext::assetLabels() const {
    return uniqueLabels(assets_);
}

std::vector<std::string> MikrosShotBrowserContext::playlistLabels() const {
    return uniqueLabels(playlists_);
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
    selected_sequence_ = std::nullopt;
    selected_shot_ = std::nullopt;
    shots_.clear();
    if (!sequence_->value().empty()) {
        sequence_->set_value("");
    }
    shot_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    if (!shot_->value().empty()) {
        shot_->set_value("");
    }
}

void MikrosShotBrowserContext::clearAssetBranch() {
    selected_asset_ = std::nullopt;
    if (!asset_->value().empty()) {
        asset_->set_value("");
    }
}

void MikrosShotBrowserContext::loadProjectsAsync() {
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak]() {
        const utility::JsonStore payload = namedRowsToJson(dal::projects());
        if (auto handle = weak.lock()) {
            anon_mail(data_source::get_data_atom_v, payload)
                .send(caf::actor_cast<caf::actor>(handle));
        }
    }).detach();
}

void MikrosShotBrowserContext::refreshSequences() {
    ++sequence_request_id_;
    sequences_.clear();
    selected_sequence_ = std::nullopt;
    sequence_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    sequence_->set_value("");

    if (!selected_project_) {
        return;
    }

    const std::uint64_t request_id = sequence_request_id_;
    const std::int64_t project_id = selected_project_->id;
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id]() {
        const utility::JsonStore payload = namedRowsToJson(dal::sequences(project_id));
        if (auto handle = weak.lock()) {
            anon_mail(
                data_source::put_data_atom_v,
                request_id,
                std::string("sequence"),
                project_id,
                payload)
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

    if (!selected_sequence_) {
        return;
    }

    const std::uint64_t request_id = shot_request_id_;
    const std::int64_t project_id = selected_project_ ? selected_project_->id : std::int64_t{0};
    const std::int64_t sequence_id = selected_sequence_->id;
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id, sequence_id]() {
        const utility::JsonStore payload = namedRowsToJson(dal::shots(sequence_id));
        if (auto handle = weak.lock()) {
            anon_mail(
                data_source::put_data_atom_v,
                request_id,
                std::string("shot"),
                project_id,
                sequence_id,
                payload)
                .send(caf::actor_cast<caf::actor>(handle));
        }
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
        const utility::JsonStore payload = namedRowsToJson(dal::assets(project_id));
        if (auto handle = weak.lock()) {
            anon_mail(
                data_source::put_data_atom_v,
                request_id,
                std::string("asset"),
                project_id,
                payload)
                .send(caf::actor_cast<caf::actor>(handle));
        }
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
        const utility::JsonStore payload = namedRowsToJson(dal::playlists(project_id));
        if (auto handle = weak.lock()) {
            anon_mail(
                data_source::put_data_atom_v,
                request_id,
                std::string("playlist"),
                project_id,
                payload)
                .send(caf::actor_cast<caf::actor>(handle));
        }
    }).detach();
}

void MikrosShotBrowserContext::refreshVersions() {
    ++version_request_id_;
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
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, request_id, project_id, sequence_id, shot_id, asset_id, playlist_id]() {
        std::optional<std::string> cursor;
        bool append = false;
        for (;;) {
            const dal::NamePage page = dal::versionNames(
                project_id,
                kVersionPageLimit,
                cursor,
                sequence_id,
                shot_id,
                asset_id,
                playlist_id);
            if (auto handle = weak.lock()) {
                anon_mail(
                    data_source::get_data_atom_v,
                    request_id,
                    project_id,
                    sequence_id.has_value(),
                    sequence_id ? *sequence_id : std::int64_t{0},
                    shot_id.has_value(),
                    shot_id ? *shot_id : std::int64_t{0},
                    asset_id.has_value(),
                    asset_id ? *asset_id : std::int64_t{0},
                    playlist_id.has_value(),
                    playlist_id ? *playlist_id : std::int64_t{0},
                    append,
                    page.names)
                    .send(caf::actor_cast<caf::actor>(handle));
            }
            if (!page.next_cursor) {
                break;
            }
            cursor = page.next_cursor;
            append = true;
        }
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
        refreshShots();
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
        [=](data_source::get_data_atom, const utility::JsonStore &payload) {
            projects_ = namedRowsFromJson<poco::Project>(payload);
            selected_project_ = std::nullopt;
            project_->set_role_data(module::Attribute::StringChoices, projectLabels());
            project_->set_value("");
            refreshSequences();
            refreshShots();
            refreshAssets();
            refreshPlaylists();
            refreshVersions();
        },
        [=](data_source::put_data_atom,
            const std::uint64_t request_id,
            const std::string &catalog,
            const std::int64_t project_id,
            const utility::JsonStore &payload) {
            if (!selected_project_ || selected_project_->id != project_id) {
                return;
            }
            if (catalog == "sequence") {
                if (request_id != sequence_request_id_) {
                    return;
                }
                sequences_ = namedRowsFromJson<poco::Sequence>(payload);
                selected_sequence_ = std::nullopt;
                sequence_->set_role_data(module::Attribute::StringChoices, sequenceLabels());
                sequence_->set_value("");
                refreshShots();
            } else if (catalog == "asset") {
                if (request_id != asset_request_id_) {
                    return;
                }
                assets_ = namedRowsFromJson<poco::Asset>(payload);
                selected_asset_ = std::nullopt;
                asset_->set_role_data(module::Attribute::StringChoices, assetLabels());
                asset_->set_value("");
            } else if (catalog == "playlist") {
                if (request_id != playlist_request_id_) {
                    return;
                }
                playlists_ = namedRowsFromJson<poco::Playlist>(payload);
                selected_playlist_ = std::nullopt;
                playlist_->set_role_data(module::Attribute::StringChoices, playlistLabels());
                playlist_->set_value("");
            }
        },
        [=](data_source::put_data_atom,
            const std::uint64_t request_id,
            const std::string &catalog,
            const std::int64_t project_id,
            const std::int64_t sequence_id,
            const utility::JsonStore &payload) {
            if (catalog != "shot" || request_id != shot_request_id_) {
                return;
            }
            if (!selected_project_ || selected_project_->id != project_id) {
                return;
            }
            if (!selected_sequence_ || selected_sequence_->id != sequence_id) {
                return;
            }
            shots_ = namedRowsFromJson<poco::Shot>(payload);
            selected_shot_ = std::nullopt;
            shot_->set_role_data(module::Attribute::StringChoices, shotLabels());
            shot_->set_value("");
        },
        [=](data_source::get_data_atom,
            const std::uint64_t request_id,
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
            const std::vector<std::string> &names) {
            if (request_id != version_request_id_) {
                return;
            }
            if (!selected_project_ || selected_project_->id != project_id) {
                return;
            }
            if (!selectionMatches(has_sequence, sequence_id, selected_sequence_) ||
                !selectionMatches(has_shot, shot_id, selected_shot_) ||
                !selectionMatches(has_asset, asset_id, selected_asset_) ||
                !selectionMatches(has_playlist, playlist_id, selected_playlist_)) {
                return;
            }
            if (!append) {
                version_->set_role_data(module::Attribute::StringChoices, names);
                version_->set_value("");
                return;
            }
            if (names.empty()) {
                return;
            }
            std::vector<std::string> choices = version_->options();
            choices.insert(choices.end(), names.begin(), names.end());
            version_->set_role_data(module::Attribute::StringChoices, choices);
        }};
}
