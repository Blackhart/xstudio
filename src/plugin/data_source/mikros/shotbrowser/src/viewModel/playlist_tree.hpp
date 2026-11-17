// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_tree.hpp
 * @brief Which playlists to load, when a branch unfolds, and the reload after a filter change.
 */
#pragma once

#include "viewModel/context_shared.hpp"

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Loads playlist names and mails the tree after each page.
 * @param mail Destination and request. `catalog` is `playlist_tree`.
 */
void publishPlaylistNames(const TreeMail &mail);

/**
 * @brief Loads one playlist's versions and mails each page.
 * @param mail Destination. `catalog` is `playlist_versions`.
 * @param parents Sequence, shot, and asset filter shared with the version list.
 * @param playlistId Playlist the user unfolded.
 */
void publishVersionsOfPlaylist(
    const TreeMail &mail, const VersionParents &parents, std::int64_t playlistId);

/** @brief Clears playlists and loads their names, without their versions. */
void refreshPlaylistTree(PanelState &state);

/**
 * @brief Loads the versions nested under one playlist.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the playlist tree. A newer tree drops these pages.
 * @param projectId Project that owns the playlist.
 * @param playlistId Playlist the user unfolded.
 */
void startPlaylistVersionLoad(
    PanelState &state, std::uint64_t requestId, std::int64_t projectId, std::int64_t playlistId);

/**
 * @brief True when that playlist's versions were already loaded to the last page.
 * @param state Panel catalogs and attributes.
 * @param playlistId Playlist in the current tree.
 */
bool playlistVersionsReady(const PanelState &state, std::int64_t playlistId);

/**
 * @brief Loads playlist names for one project.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the playlist tree. A page with an older id is ignored.
 * @param projectId Project whose playlists are loaded.
 */
void startPlaylistLoad(PanelState &state, std::uint64_t requestId, std::int64_t projectId);

/** @brief Loads the versions of the playlist the user unfolded. */
void onPlaylistExpanded(PanelState &state);

/** @brief Reloads the unfolded playlist after the version filter changes. */
void reloadExpandedPlaylist(PanelState &state);

/**
 * @brief Writes one page of versions into the matching playlist node.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the tree. An older page is ignored.
 * @param payload JSON patch with the playlist id and its versions.
 */
void applyPlaylistVersions(
    PanelState &state, std::uint64_t requestId, const utility::JsonStore &payload);

/**
 * @brief Replaces the playlist tree when the version filter is still the same.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the tree. An older tree is ignored.
 * @param payload JSON tree from the worker.
 */
void applyPlaylistTree(
    PanelState &state,
    std::uint64_t requestId,
    bool hasSequence,
    std::int64_t sequenceId,
    bool hasShot,
    std::int64_t shotId,
    bool hasAsset,
    std::int64_t assetId,
    const utility::JsonStore &payload);

} // namespace xstudio::mikros_shotbrowser
