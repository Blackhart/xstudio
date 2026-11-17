// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/open_media.hpp
 * @brief Double-click and menu: disk check, signed-movie download, and insert into the playlist.
 */
#pragma once

#include "viewModel/context_shared.hpp"

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Paths that survived the disk check for one version.
 *
 * `frames_spec` is empty when the sequence is not opened. `movie_path` is a local file.
 */
struct OpenReady {
    std::int64_t version_id = 0; ///< ShotGrid version id, for the log line.
    std::uint64_t token = 0;     ///< Matches the QML request that should be selected.
    bool select = false;         ///< True when the new media should become current.
    std::string name;            ///< Media name.
    std::string playlist_uuid;   ///< Playlist actor uuid.
    std::string frames_spec;     ///< Sequence spec accepted by `parse_cli_posix_path`.
    std::string movie_path;      ///< Local movie file. Empty when there is no movie.
};

/**
 * @brief Reads the paths a worker mailed after checking one version on disk.
 * @param payload JSON written by the open thread.
 * @return Paths the panel can add. Empty strings when a field is missing.
 */
OpenReady openReady(const utility::JsonStore &payload);

/**
 * @brief Parses one open request and checks its files off the actor thread.
 * @param state Panel catalogs and attributes.
 * @param text JSON the panel wrote into `OpenVersion`. Empty does nothing.
 */
void startOpen(PanelState &state, const std::string &text);

/**
 * @brief Adds the checked version to its playlist.
 *
 * When the request asked to select the media, writes `OpenedMedia` so the panel can select it.
 * @param state Panel catalogs and attributes.
 * @param ready Paths and the target playlist.
 */
void placeOpenedMedia(PanelState &state, const OpenReady &ready);

/**
 * @brief Creates the movie and frame sources and inserts the media into the playlist.
 * @param state Panel catalogs and attributes.
 * @param ready Paths that survived the disk check.
 * @param playlist Playlist actor that receives the media.
 * @param rate Frame rate of that playlist.
 */
void addOpenedSources(
    PanelState &state,
    const OpenReady &ready,
    caf::actor playlist,
    const utility::FrameRate &rate);

} // namespace xstudio::mikros_shotbrowser
