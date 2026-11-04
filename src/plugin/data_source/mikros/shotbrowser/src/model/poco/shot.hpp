// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/shot.hpp
 * @brief Plain shot row used by the Shot Browser UI context.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One shot row for the UI context.
 *
 * `id` is the unique identity. `name` is the human label and may collide.
 */
struct Shot {
    std::int64_t id = 0; ///< Domain shot id.
    std::string name;    ///< Domain shot name.
};

} // namespace xstudio::mikros_shotbrowser::poco
