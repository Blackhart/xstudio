// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/version.hpp
 * @brief Plain version row used by the Shot Browser UI context.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One version row for the UI context.
 *
 * `id` is the unique identity. `name` is the human label and may collide.
 */
struct Version {
    std::int64_t id = 0;        ///< Domain version id.
    std::string name;           ///< Domain version name.
    std::string image;          ///< Remote URL or local path. Empty when the version has no picture.
    std::string image_provider; ///< `http`, `file`, or empty. Comes from the studio mapping.
    std::string author;         ///< Person who created the version. Empty shows N/A.
    std::string created_at;     ///< Creation time as ShotGrid sent it. Empty shows N/A.
    std::string frame_range;   ///< Frame range text. Empty shows N/A.
    std::string description;   ///< Version description. Empty shows N/A.
    std::string step;           ///< Pipeline step name. Empty shows N/A.
    std::string status;         ///< Step status. Empty shows N/A.
    std::string artistic_status; ///< Artistic status. Empty shows N/A.
    std::string sites;          ///< ShotGrid site names, joined by commas.
    bool has_notes = false;     ///< True when the version has at least one note.
    bool synced_to_client = false; ///< True when the version was sent to the client.
    std::string frames;            ///< Image-sequence path. Empty when ShotGrid has none.
    std::string frames_provider;   ///< `fileSeq`, or empty.
    std::string movie;             ///< On-disk movie path. Empty when ShotGrid has none.
    std::string movie_provider;    ///< `file`, or empty.
    std::string uploaded_movie;    ///< Signed movie URL. Empty when ShotGrid has none.
    std::string uploaded_movie_provider; ///< `url`, or empty.
};

} // namespace xstudio::mikros_shotbrowser::poco
