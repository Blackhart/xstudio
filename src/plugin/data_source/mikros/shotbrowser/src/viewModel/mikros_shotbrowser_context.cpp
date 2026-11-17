// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/mikros_shotbrowser_context.cpp
 * @brief Panel actor: construction, attributes, and the relay that delivers each page to its screen.
 */

#include "viewModel/mikros_shotbrowser_context.hpp"
#include "viewModel/context_shared.hpp"
#include "viewModel/asset_tab.hpp"
#include "viewModel/open_media.hpp"
#include "viewModel/thumbnails.hpp"
#include "viewModel/playlist_card.hpp"
#include "viewModel/playlist_tree.hpp"
#include "viewModel/project_selector.hpp"
#include "viewModel/shot_tree.hpp"
#include "viewModel/version_card.hpp"
#include "viewModel/version_list.hpp"

#include "model/constants.hpp"

#include "xstudio/utility/helpers.hpp"

#include <filesystem>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace {

/**
 * @brief Writes `set` or `default` after a resolved setting.
 * @param set True when the environment variable is non-empty.
 * @return The origin word.
 */
const char *settingOrigin(bool set) { return set ? "set" : "default"; }

/**
 * @brief Formats one text setting for the launch log.
 * @param name Environment variable name.
 * @param setting Resolved value.
 * @return `name=value (set)`, `name=value (default)`, or `name=unset`.
 */
std::string textSettingField(const char *name, const constants::TextSetting &setting) {
    if (!setting.set && setting.value.empty()) {
        return std::string(name) + "=unset";
    }
    return std::string(name) + "=" + setting.value + " (" + settingOrigin(setting.set) + ")";
}

/**
 * @brief Formats one numeric setting for the launch log.
 * @param name Environment variable name.
 * @param setting Resolved value.
 * @return `name=value (set)` or `name=value (default)`.
 */
std::string numberSettingField(const char *name, const constants::NumberSetting &setting) {
    return std::string(name) + "=" + std::to_string(setting.value) + " (" +
           settingOrigin(setting.set) + ")";
}

/**
 * @brief Formats the ShotGrid script key without printing the secret.
 * @return `SHOTGRID_CLIENT_SECRET=unset`, or the redacted key marked `set`.
 */
std::string secretSettingField() {
    const constants::TextSetting secret = constants::shotGridClientSecret();
    if (!secret.set) {
        return std::string(constants::kShotGridClientSecret) + "=unset";
    }
    return std::string(constants::kShotGridClientSecret) + "=" + constants::keyHint(secret.value) +
           " (set)";
}

/**
 * @brief Logs every Shot Browser setting and whether it came from the environment.
 */
void logSettings() {
    trace::info(
        std::string("env ") + textSettingField(constants::kBackend, constants::backend()) + " " +
        numberSettingField(constants::kCacheTtlSeconds, constants::cacheTtlSeconds()) + " " +
        numberSettingField(constants::kCacheBytes, constants::cacheMaxBytes()) + " " +
        numberSettingField(constants::kPageSize, constants::pageSize()) + " " +
        textSettingField(constants::kSchemaPath, constants::schemaPath()) + " " +
        textSettingField(constants::kShotGridUrl, constants::shotGridUrl()) + " " +
        textSettingField(constants::kShotGridClientId, constants::shotGridClientId()) + " " +
        textSettingField(constants::kShotGridMapping, constants::shotGridMapping()) + " " +
        secretSettingField());
}

/**
 * @brief Logs every path the Shot Browser writes.
 */
void logWriteLocations() {
    const std::string preferences = utility::preference_path_context("PLUGIN");
    const std::filesystem::path root = thumb::thumbRoot();
    if (root.empty()) {
        trace::info(
            "writes preferences " + preferences +
            " (browser_mode, result_view, left_pane_width); thumbnail directory unavailable "
            "because HOME is not set");
        return;
    }
    trace::info(
        "writes preferences " + preferences +
        " (browser_mode, result_view, left_pane_width); thumbnails " + root.string());
}

/**
 * @brief Publishes the empty combos and trees the panel binds to.
 * @param panel Actor that owns the attributes.
 * @param state Where the attribute pointers are stored.
 */
void addPanelAttributes(MikrosShotBrowserContext &panel, PanelState &state) {
    state.project_ = panel.add_string_choice_attribute("Project", "Project", "", {}, {});
    state.project_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.sequence_ = panel.add_string_choice_attribute("Sequence", "Sequence", "", {}, {});
    state.sequence_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.shot_ = panel.add_string_choice_attribute("Shot", "Shot", "", {}, {});
    state.shot_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.shot_tree_ = panel.add_json_attribute("ShotTree", "ShotTree", nlohmann::json::array());
    state.shot_tree_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.asset_ = panel.add_string_choice_attribute("Asset", "Asset", "", {}, {});
    state.asset_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.version_ = panel.add_string_choice_attribute("Version", "Version", "", {}, {});
    state.version_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.versions_loaded_ = panel.add_boolean_attribute("VersionsLoaded", "VersionsLoaded", false);
    state.versions_loaded_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.version_cards_ = panel.add_json_attribute("VersionCards", "VersionCards", nlohmann::json::array());
    state.version_cards_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.playlist_tree_ =
        panel.add_json_attribute("PlaylistTree", "PlaylistTree", nlohmann::json::array());
    state.playlist_tree_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.browser_mode_ = panel.add_string_attribute("BrowserMode", "BrowserMode", "shot");
    state.browser_mode_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.result_view_ = panel.add_string_attribute("ResultView", "ResultView", "versions");
    state.result_view_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.expanded_sequence_ = panel.add_string_attribute("ExpandedSequence", "ExpandedSequence", "");
    state.expanded_sequence_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.expanded_playlist_ = panel.add_string_attribute("ExpandedPlaylist", "ExpandedPlaylist", "");
    state.expanded_playlist_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.open_version_ = panel.add_string_attribute("OpenVersion", "OpenVersion", "");
    state.open_version_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    state.opened_media_ = panel.add_string_attribute("OpenedMedia", "OpenedMedia", "");
    state.opened_media_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

/** @brief Loads sequences or assets when the user switches tab, if that tab is empty. */
void onBrowserModeChosen(PanelState &state) {
    if (!state.selected_project_) {
        return;
    }
    if (state.browser_mode_->value() == "asset") {
        if (!state.assets_ready_) {
            refreshAssets(state);
        }
        return;
    }
    if (!state.sequences_ready_) {
        refreshShotTree(state);
    }
}

/** @brief Loads playlist names when the user opens the playlist view. */
void onResultViewChosen(PanelState &state) {
    if (state.result_view_->value() != "playlist" || !state.selected_project_ || state.playlists_ready_) {
        return;
    }
    refreshPlaylistTree(state);
}

/**
 * @brief True when the panel is still on the project a worker reply belongs to.
 * @param state Panel catalogs and attributes.
 * @param projectId Project id carried by the reply.
 */
bool projectStillSelected(const PanelState &state, std::int64_t projectId) {
    return state.selected_project_ && state.selected_project_->id == projectId;
}

} // namespace

MikrosShotBrowserContext::MikrosShotBrowserContext(
    caf::actor_config &cfg, const utility::JsonStore &init_settings)
    : plugin::StandardPlugin(cfg, "MikrosShotBrowserPlugin", init_settings),
      state_(std::make_unique<PanelState>()) {

    state_->panel = this;     ///< Screen functions spawn loads and insert media through this actor.
    registerCatalogLoadType(); ///< Before the first detached load sends a request.
    addPanelAttributes(*this, *state_); ///< Combos and trees the panel binds to.
    logSettings();       ///< Each environment variable, and whether it is set or default.
    logWriteLocations(); ///< Preferences file and thumbnail directory, resolved now.
    make_behavior();
    connect_to_ui();
    loadProjectsAsync(*state_); ///< First catalog, before the panel asks.
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

MikrosShotBrowserContext::~MikrosShotBrowserContext() = default;

void MikrosShotBrowserContext::attribute_changed(
    const utility::Uuid &attr_uuid, const int role) {
    PanelState &state = *state_;
    if (role != module::Attribute::Value) {
        plugin::StandardPlugin::attribute_changed(attr_uuid, role);
        return;
    }
    if (attr_uuid == state.project_->uuid()) {
        onProjectChosen(state);
    } else if (attr_uuid == state.browser_mode_->uuid()) {
        onBrowserModeChosen(state);
    } else if (attr_uuid == state.result_view_->uuid()) {
        onResultViewChosen(state);
    } else if (attr_uuid == state.expanded_sequence_->uuid()) {
        onSequenceExpanded(state);
    } else if (attr_uuid == state.expanded_playlist_->uuid()) {
        onPlaylistExpanded(state);
    } else if (state.aligning_sequence_) {
        plugin::StandardPlugin::attribute_changed(attr_uuid, role);
        return; ///< The shot handler is aligning the sequence. Do not clear the shot.
    } else if (attr_uuid == state.sequence_->uuid()) {
        onSequenceChosen(state);
    } else if (attr_uuid == state.shot_->uuid()) {
        onShotChosen(state);
    } else if (attr_uuid == state.asset_->uuid()) {
        onAssetChosen(state);
    } else if (state.open_version_ && attr_uuid == state.open_version_->uuid()) {
        startOpen(state, state.open_version_->value());
    }
    plugin::StandardPlugin::attribute_changed(attr_uuid, role);
}

caf::message_handler MikrosShotBrowserContext::message_handler_extensions() {
    return caf::message_handler{
        [=](data_source::put_data_atom,
            const std::uint64_t requestId,
            const std::string &catalog,
            const std::int64_t projectId,
            const bool hasSequence,
            const std::int64_t sequenceId,
            const bool hasShot,
            const std::int64_t shotId,
            const bool hasAsset,
            const std::int64_t assetId,
            const bool append,
            const utility::JsonStore &payload) {
            PanelState &state = *state_;
            if (catalog == "open_media") {
                placeOpenedMedia(state, openReady(payload)); ///< Paths checked off the scheduler.
                return;
            }
            if (catalog == "version_thumbs") {
                rememberThumbs(state, projectId, payload); ///< Keep the file even if the filter moved on.
                if (projectStillSelected(state, projectId)) {
                    publishVersionCards(state);
                    stampPlaylistThumbs(state);
                }
                return;
            }
            if (catalog == "project") {
                applyProjectPage(state, requestId, append, payload);
                return;
            }
            if (!projectStillSelected(state, projectId)) {
                trace::drop(catalog, requestId); ///< The user picked another project.
                return;
            }
            if (catalog == "shot_tree") {
                applyShotTree(state, requestId, payload);
            } else if (catalog == "sequence_shots") {
                applySequenceShots(state, requestId, payload);
            } else if (catalog == "playlist_versions") {
                applyPlaylistVersions(state, requestId, payload);
            } else if (catalog == "asset") {
                applyAssetPage(state, requestId, append, payload);
            } else if (catalog == "playlist_tree") {
                applyPlaylistTree(
                    state,
                    requestId,
                    hasSequence,
                    sequenceId,
                    hasShot,
                    shotId,
                    hasAsset,
                    assetId,
                    payload);
            } else if (catalog == "version") {
                applyVersionPage(
                    state,
                    requestId,
                    hasSequence,
                    sequenceId,
                    hasShot,
                    shotId,
                    hasAsset,
                    assetId,
                    append,
                    payload);
            }
        }};
}
