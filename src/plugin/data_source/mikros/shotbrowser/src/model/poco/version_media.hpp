// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/version_media.hpp
 * @brief Version open-media paths for one id.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief Frames and movie locations for opening one version.
 *
 * Loaded on demand when the user opens media, so the list find never asks for
 * signed movie URLs.
 */
struct VersionMedia {
    std::int64_t id = 0;                     ///< Domain version id.
    std::string frames;                      ///< Image-sequence path. Empty when none.
    std::string frames_provider;             ///< `fileSeq`, or empty.
    std::string movie;                       ///< On-disk movie path. Empty when none.
    std::string movie_provider;              ///< `file`, or empty.
    std::string uploaded_movie;              ///< Signed movie URL. Empty when none.
    std::string uploaded_movie_provider;     ///< `signedurl`, or empty.
};

} // namespace xstudio::mikros_shotbrowser::poco
