// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/media_open.hpp
 * @brief Resolves one version's files, then asks the panel to add the media.
 */
#pragma once

#include "xstudio/atoms.hpp"

#include <caf/actor.hpp>

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief What the panel asked to open, before the disk check.
 */
struct OpenRequest {
    std::int64_t version_id = 0;  ///< ShotGrid version id.
    std::int64_t project_id = 0;  ///< Selected project, used in the movie cache path.
    std::uint64_t token = 0;      ///< Matches the QML request that should be selected.
    bool select = false;          ///< True when the new media should become current.
    std::string name;             ///< Version code, used as the media name.
    std::string playlist_uuid;    ///< xSTUDIO playlist that receives the media.
    std::string frames;           ///< `sg_path_to_frames`.
    std::string frame_range;      ///< Range that bounds the sequence when it is present.
    std::string movie;            ///< `sg_path_to_movie`.
    std::string uploaded_movie;   ///< Signed `sg_uploaded_movie_mp4` URL.
};

/**
 * @brief Paths that survived the disk check.
 *
 * `frames_spec` is empty when the sequence is not opened. `movie_path` is a
 * local file: either the ShotGrid path or the downloaded mp4.
 */
struct OpenReady {
    std::int64_t version_id = 0; ///< ShotGrid version id, for the log line.
    std::uint64_t token = 0;     ///< Copied from the request.
    bool select = false;         ///< Copied from the request.
    std::string name;            ///< Media name.
    std::string playlist_uuid;   ///< Playlist actor uuid.
    std::string frames_spec;     ///< Sequence spec accepted by `parse_cli_posix_path`.
    std::string movie_path;      ///< Local movie file. Empty when there is no movie.
};

/**
 * @brief Checks the files and downloads the signed movie when needed.
 *
 * The download uses a `QEventLoop`, so this runs on a detached thread. The
 * result is a `put_data` message named `open_media`. `OpenReady` is not a CAF
 * message: registering a type after the actor system exists is undefined.
 *
 * @param panel Panel actor that receives the result. A dead panel is skipped.
 * @param request Paths and the target playlist.
 */
void resolveOpen(caf::weak_actor_ptr panel, OpenRequest request);

} // namespace xstudio::mikros_shotbrowser
