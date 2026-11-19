// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/version_thumb.hpp
 * @brief Version thumbnail source for one id.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief Image source for one version thumbnail.
 *
 * Fetched lazily after the list page. `image` may be a remote URL or a local
 * path; `image_provider` tells the thumb util which.
 */
struct VersionThumb {
    std::int64_t id = 0;        ///< Domain version id.
    std::string image;          ///< Remote URL or local path. Empty when none.
    std::string image_provider; ///< `http`, `file`, or empty.
};

} // namespace xstudio::mikros_shotbrowser::poco
