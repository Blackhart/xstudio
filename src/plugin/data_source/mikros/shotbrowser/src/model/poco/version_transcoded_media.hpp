// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/version_transcoded_media.hpp
 * @brief Signed uploaded-movie URL for one version.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief ShotGrid signed movie URL for one version.
 *
 * Fetched in batch only for versions that cannot open from local frames, local
 * movie, or the on-disk movie cache. Written onto the owning `VersionCard`.
 */
struct VersionTranscodedMedia {
    std::int64_t id = 0;                 ///< Domain version id.
    std::string uploaded_movie;          ///< Signed movie URL. Empty when none.
    std::string uploaded_movie_provider; ///< `signedurl`, or empty.
};

} // namespace xstudio::mikros_shotbrowser::poco
