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
    std::int64_t id = 0;      ///< Domain version id.
    std::string name;         ///< Domain version name.
    std::string image;        ///< Remote URL or local path. Empty when the version has no picture.
    std::string image_provider; ///< `http`, `file`, or empty. Comes from the studio mapping.
};

} // namespace xstudio::mikros_shotbrowser::poco
