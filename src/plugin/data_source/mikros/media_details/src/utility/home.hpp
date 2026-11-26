// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/home.hpp
 * @brief Home directory for the shared Mikros thumbnail cache.
 */
#pragma once

#include <filesystem>

namespace xstudio::mikros_media_details::thumb {

/**
 * @brief Home directory resolved at call time.
 * @return `HOME`, or `USERPROFILE` on Windows. Empty when neither is set.
 */
std::filesystem::path homeDirectory();

} // namespace xstudio::mikros_media_details::thumb
