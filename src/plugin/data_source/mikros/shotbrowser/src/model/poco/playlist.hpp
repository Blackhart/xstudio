// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/playlist.hpp
 * @brief Plain playlist row used by the Shot Browser UI context.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One playlist row for the UI context.
 *
 * `id` is the unique identity. `name` is the human label and may collide.
 * `version_count` is the size of the playlist's `versions` link list from the
 * catalog find (ShotGrid stubs or fake memberships). Full version cards still
 * load when the playlist is unfolded. The remaining fields feed the playlist
 * header card.
 */
struct Playlist {
    std::int64_t id = 0;               ///< Domain playlist id.
    std::string name;                  ///< Domain playlist name.
    std::size_t version_count = 0;     ///< Linked version count from the playlist find.
    std::int64_t open_notes_count = 0; ///< Open notes count for the N indicator.
    std::string departments;           ///< Joined department names. Empty shows N/A.
    std::string created_at;            ///< Creation time as ShotGrid sent it. Empty shows N/A.
    std::string category;              ///< Playlist category / type. Empty shows N/A.
    std::string author;                ///< Person who created the playlist. Empty shows N/A.
};

} // namespace xstudio::mikros_shotbrowser::poco
