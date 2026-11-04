// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/playlist.hpp
 * @brief Plain playlist row used by the Shot Browser UI context.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One playlist row for the UI context.
 *
 * `id` is the unique identity. `name` is the human label and may collide.
 */
struct Playlist {
    std::int64_t id = 0; ///< Domain playlist id.
    std::string name;    ///< Domain playlist name.
};

} // namespace xstudio::mikros_shotbrowser::poco
