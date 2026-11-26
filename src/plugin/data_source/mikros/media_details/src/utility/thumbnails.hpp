// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/thumbnails.hpp
 * @brief Disk cache and JPEG download sharing Shot Browser's thumb root.
 */
#pragma once

#include "utility/home.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace xstudio::mikros_media_details {

namespace thumb {

/**
 * @brief Directory that holds every version thumbnail.
 *
 * Same path as Shot Browser so both plugins share the cache:
 * `${HOME}/xStudio/mikros_shotbrowser/thumbs`.
 * @return The cache root, or empty when home is missing.
 */
std::filesystem::path thumbRoot();

} // namespace thumb

/**
 * @brief Resolves one version thumbnail (cache hit, local file, or download).
 *
 * Uses the shared Shot Browser cache directory. `onReady` may run on a worker
 * thread; the caller marshals to the panel actor.
 * @param projectId Project id for the cache path.
 * @param versionId Version id for the cache path.
 * @param image URL or filesystem path from the version row. Empty means none.
 * @param imageProvider `http`, `file`, or empty.
 * @param onReady Called with a `file://` URL when an image is ready. Not called when none.
 */
void scheduleOneThumb(
    std::int64_t projectId,
    std::int64_t versionId,
    std::string image,
    std::string imageProvider,
    std::function<void(std::string fileUrl)> onReady);

} // namespace xstudio::mikros_media_details
