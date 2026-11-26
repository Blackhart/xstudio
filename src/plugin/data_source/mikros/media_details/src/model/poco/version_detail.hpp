// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/version_detail.hpp
 * @brief Version row for the Media Details panel, including parents and paths.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_media_details::poco {

/**
 * @brief One version as shown in Media Details.
 *
 * Parents come from ShotGrid `project` and polymorphic `entity`. At most one of
 * shot / sequence / asset is filled from `entity_type`.
 */
struct VersionDetail {
    std::int64_t id = 0;           ///< Domain version id.
    std::string name;              ///< Domain version name.
    std::string author;            ///< Person who created the version.
    std::string created_at;        ///< Creation time as ShotGrid sent it.
    std::string frame_range;       ///< Frame range text.
    std::string description;       ///< Version description.
    std::string step;              ///< Pipeline step name.
    std::string status;            ///< Technical status.
    std::string artistic_status;   ///< Artistic status.
    std::string sites;             ///< ShotGrid site names, joined by commas.
    std::int64_t open_notes_count = 0; ///< Open notes count.
    bool synced_to_client = false;     ///< True when sent to the client.
    std::string frames;                ///< Image-sequence path.
    std::string frames_provider;       ///< `fileSeq`, or empty.
    std::string movie;                 ///< On-disk movie path.
    std::string movie_provider;        ///< `file`, or empty.
    std::string uploaded_movie;        ///< Signed uploaded movie URL.
    std::string uploaded_movie_provider; ///< `signedurl`, or empty.
    std::string image;                 ///< Thumbnail URL or path.
    std::string image_provider;        ///< `http` / `file`, or empty.
    std::int64_t project_id = 0;       ///< Linked project id. 0 when none.
    std::string project_name;          ///< Linked project name.
    std::string entity_type;           ///< `Shot`, `Sequence`, or `Asset`.
    std::int64_t entity_id = 0;        ///< Linked entity id. 0 when none.
    std::string entity_name;           ///< Linked entity name.
};

} // namespace xstudio::mikros_media_details::poco
