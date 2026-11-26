// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/home.cpp
 * @brief Home directory for the shared Mikros thumbnail cache.
 */

#include "utility/home.hpp"

#include <cstdlib>

namespace xstudio::mikros_media_details::thumb {

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

} // namespace xstudio::mikros_media_details::thumb
