// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_card.hpp
 * @brief Fields one playlist version row shows, and the cached image stamped onto that row.
 */
#pragma once

#include "viewModel/context_shared.hpp"

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Mails one playlist's versions. The card writes the fields the row shows.
 * @param mail Destination. `catalog` is `playlist_versions`.
 * @param playlistId Playlist the user unfolded.
 * @param versions Versions received for that playlist so far.
 * @param complete True on the last page.
 */
void sendPlaylistVersions(
    const TreeMail &mail,
    std::int64_t playlistId,
    const std::vector<poco::Version> &versions,
    bool complete);

/** @brief Copies stored thumbnail URLs onto the version rows of the playlist tree. */
void stampPlaylistThumbs(PanelState &state);

} // namespace xstudio::mikros_shotbrowser
