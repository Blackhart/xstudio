// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/project.hpp
 * @brief Plain project row used by the Shot Browser UI context.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One project row for the UI context.
 *
 * `id` is the unique identity. `name` is the human label and may collide.
 */
struct Project {
    std::int64_t id = 0; ///< Domain project id.
    std::string name;    ///< Domain project name.
};

} // namespace xstudio::mikros_shotbrowser::poco
