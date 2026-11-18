// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/home.cpp
 * @brief Home directory shared by the thumbnail cache and the movie download.
 */

#include "utility/home.hpp"

#include <cstdlib>

namespace xstudio::mikros_shotbrowser::thumb {

std::filesystem::path homeDirectory() {
    const char *home = std::getenv("HOME");
#ifdef _WIN32
    if (!home || home[0] == '\0') {
        home = std::getenv("USERPROFILE");
    }
#endif
    if (!home || home[0] == '\0') {
        return {};
    }
    return std::filesystem::path(home);
}

} // namespace xstudio::mikros_shotbrowser::thumb
