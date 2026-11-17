// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_tree.cpp
 * @brief Which playlists to load, when a branch unfolds, and the reload after a filter change.
 */

#include "viewModel/playlist_tree.hpp"
#include "viewModel/playlist_card.hpp"

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Loads one playlist's versions and mails each page.
 * @param mail Destination. `catalog` is `playlist_versions`.
 * @param parents Sequence, shot, and asset filter shared with the version list.
 * @param playlistId Playlist the user unfolded.
 */
void publishVersionsOfPlaylist(
    const TreeMail &mail, const VersionParents &parents, std::int64_t playlistId) {
    std::vector<poco::Version> versions;
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<poco::Version> page = dal::findVersions(
            mail.projectId,
            dal::readPageLimit(),
            cursor,
            parents.sequenceId,
            parents.shotId,
            parents.assetId,
            playlistId);
        if (!page.rows.empty()) {
            versions.insert(versions.end(), page.rows.begin(), page.rows.end());
            scheduleThumbPage(mail.weak, mail.projectId, page.rows);
        }
        const bool complete = !page.next_cursor;
        if (!versions.empty() || complete) {
            sendPlaylistVersions(mail, playlistId, versions, complete);
        }
        if (complete) {
            break;
        }
        cursor = page.next_cursor;
    }
}

/**
 * @brief Loads playlist names and mails the tree after each page.
 * @param mail Destination and request. `catalog` is `playlist_tree`.
 */
void publishPlaylistNames(const TreeMail &mail) {
    nlohmann::json tree = nlohmann::json::array();
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<poco::Playlist> page =
            dal::findPlaylists(mail.projectId, dal::readPageLimit(), cursor);
        for (const poco::Playlist &playlist : page.rows) {
            tree.push_back(
                {{"name", playlist.name},
                 {"id", playlist.id},
                 {"versions", nlohmann::json::array()}});
        }
        if (!page.rows.empty()) {
            sendTree(mail, tree); ///< Names only. Versions wait for an unfold.
        }
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
    }
}

} // namespace xstudio::mikros_shotbrowser

namespace xstudio::mikros_shotbrowser {

void
refreshPlaylistTree(PanelState &state) {
    ++state.playlist_request_id_; ///< Drop a playlist tree that is still loading.
    state.playlists_ready_ = state.selected_project_.has_value();
    state.playlist_tree_->set_value(nlohmann::json::array()); ///< Hide playlists from the previous selection.
    if (!state.selected_project_) {
        return;
    }
    startPlaylistLoad(state, state.playlist_request_id_, state.selected_project_->id);
}

void startPlaylistVersionLoad(PanelState &state, 
    std::uint64_t requestId, std::int64_t projectId, std::int64_t playlistId) {
    const VersionParents parents =
        readVersionParents(state.selected_sequence_, state.selected_shot_, state.selected_asset_);
    CatalogLoadRequest request =
        makeCatalogRequest(state.panel, CatalogKind::PlaylistVersions, requestId, projectId);
    request.playlist_id = playlistId;
    request.sequence = parents.sequenceId;
    request.shot = parents.shotId;
    request.asset = parents.assetId;
    launchCatalogLoad(state.panel->system(), std::move(request));
}

bool playlistVersionsReady(const PanelState &state, std::int64_t playlistId) {
    const nlohmann::json tree = state.playlist_tree_ ? state.playlist_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return false;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != playlistId) {
            continue;
        }
        return node.value("versionsLoaded", false);
    }
    return false;
}

void startPlaylistLoad(PanelState &state, 
    std::uint64_t requestId, std::int64_t projectId) {
    launchCatalogLoad(
        state.panel->system(), makeCatalogRequest(state.panel, CatalogKind::PlaylistTree, requestId, projectId));
}

void onPlaylistExpanded(PanelState &state) {
    const std::string name = state.expanded_playlist_->value();
    if (name.empty() || !state.selected_project_) {
        return;
    }
    const nlohmann::json tree = state.playlist_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || node.value("name", std::string()) != name || !node.contains("id")) {
            continue;
        }
        const std::int64_t playlistId = node["id"].get<std::int64_t>();
        if (!playlistVersionsReady(state, playlistId)) {
            startPlaylistVersionLoad(state, state.playlist_request_id_, state.selected_project_->id, playlistId);
        }
        return;
    }
}

void reloadExpandedPlaylist(PanelState &state) {
    if (!state.result_view_ || state.result_view_->value() != "playlist" || !state.selected_project_) {
        return;
    }
    const std::string name = state.expanded_playlist_->value();
    if (name.empty()) {
        return;
    }
    nlohmann::json tree = state.playlist_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    for (auto &node : tree) {
        if (!node.is_object() || node.value("name", std::string()) != name || !node.contains("id")) {
            continue;
        }
        node["versionsLoaded"] = false;
        const std::int64_t playlistId = node["id"].get<std::int64_t>();
        state.playlist_tree_->set_value(tree);
        startPlaylistVersionLoad(state, state.playlist_request_id_, state.selected_project_->id, playlistId);
        return;
    }
}

void applyPlaylistVersions(PanelState &state, 
    std::uint64_t requestId, const utility::JsonStore &payload) {
    if (requestId != state.playlist_request_id_) {
        trace::drop("playlist_versions", requestId);
        return;
    }
    const nlohmann::json patch = nlohmann::json(payload);
    if (!patch.contains("playlistId")) {
        return;
    }
    nlohmann::json tree = state.playlist_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    const std::int64_t playlistId = patch["playlistId"].get<std::int64_t>();
    for (auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != playlistId) {
            continue;
        }
        node["versions"] = patch["versions"];
        if (patch.value("complete", false)) {
            node["versionsLoaded"] = true;
        }
        state.playlist_tree_->set_value(tree);
        stampPlaylistThumbs(state); ///< A file stored earlier can show with this page.
        return;
    }
}

void applyPlaylistTree(PanelState &state, 
    std::uint64_t requestId,
    bool /*hasSequence*/,
    std::int64_t /*sequenceId*/,
    bool /*hasShot*/,
    std::int64_t /*shotId*/,
    bool /*hasAsset*/,
    std::int64_t /*assetId*/,
    const utility::JsonStore &payload) {
    if (requestId != state.playlist_request_id_) {
        trace::drop("playlist_tree", requestId);
        return;
    }
    state.playlist_tree_->set_value(nlohmann::json(payload));
}


} // namespace xstudio::mikros_shotbrowser
