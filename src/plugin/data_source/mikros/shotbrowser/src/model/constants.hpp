// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/constants.hpp
 * @brief Shot Browser UI settings (catalog page size).
 *
 * Shared pms / cache / ShotGrid env lives in `pms/env.hpp`.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::constants {

inline constexpr const char *kPageSize = "MIKROS_PMS_PAGE_SIZE";
inline constexpr std::uint64_t kPageSizeDefault = 50;
inline constexpr std::uint64_t kPageSizeMax = 500;

/**
 * @brief A numeric setting and whether the environment supplied it.
 */
struct NumberSetting {
    std::uint64_t value = 0; ///< Environment value, or the default.
    bool set = false;        ///< True when the environment variable is non-empty.
};

/**
 * @brief Rows requested in one catalog page.
 * @return The environment value, or 50.
 * @throw pms::Error When the variable is set but invalid, or greater than 500.
 */
NumberSetting pageSize();

} // namespace xstudio::mikros_shotbrowser::constants
