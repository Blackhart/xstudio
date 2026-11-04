// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/asset.hpp
 * @brief Plain asset row used by the Shot Browser UI context.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One asset row for the UI context.
 *
 * `id` is the unique identity. `name` is the human label and may collide.
 */
struct Asset {
    std::int64_t id = 0; ///< Domain asset id.
    std::string name;    ///< Domain asset name.
};

} // namespace xstudio::mikros_shotbrowser::poco
