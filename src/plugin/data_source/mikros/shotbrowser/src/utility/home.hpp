// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/home.hpp
 * @brief Home directory shared by the thumbnail cache and the movie download.
 */
#pragma once

#include <filesystem>

namespace xstudio::mikros_shotbrowser::thumb {

/**
 * @brief Home directory resolved at call time.
 * @return `HOME`, or `USERPROFILE` on Windows. Empty when neither is set.
 */
std::filesystem::path homeDirectory();

} // namespace xstudio::mikros_shotbrowser::thumb
