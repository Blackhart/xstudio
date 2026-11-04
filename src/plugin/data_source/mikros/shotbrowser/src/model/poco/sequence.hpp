// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/sequence.hpp
 * @brief Plain sequence row used by the Shot Browser UI context.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One sequence row for the UI context.
 *
 * `id` is the unique identity. `name` is the human label and may collide.
 */
struct Sequence {
    std::int64_t id = 0; ///< Domain sequence id.
    std::string name;    ///< Domain sequence name.
};

} // namespace xstudio::mikros_shotbrowser::poco
