// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/playlist_version_link.hpp
 * @brief One version membership entry from a playlist's `versions` links field.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief Version stub carried on a playlist's `versions` link list.
 *
 * `id` is the domain version id. `name` is the label ShotGrid put on the link.
 */
struct PlaylistVersionLink {
    std::int64_t id = 0; ///< Domain version id.
    std::string name;    ///< Version label from the link.
};

} // namespace xstudio::mikros_shotbrowser::poco
