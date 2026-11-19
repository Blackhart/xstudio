// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/thumbnails.hpp
 * @brief Disk cache and JPEG download by version id. Cards read the stored URL.
 */
#pragma once

#include "utility/home.hpp"

#include "xstudio/utility/json_store.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace xstudio::mikros_shotbrowser {

namespace thumb {

/**
 * @brief Directory that holds every version thumbnail.
 * @return `${HOME}/xStudio/mikros_shotbrowser/thumbs`, or empty when home is missing.
 */
std::filesystem::path thumbRoot();

} // namespace thumb

/**
 * @brief Resolves many version thumbnails (cache, one batch find, pooled download).
 *
 * Cache hits may call `onBatchReady` on the caller thread. Other work runs off
 * the actor. Callbacks may run on a worker thread; the caller marshals to the UI.
 * @param projectId Project id for the cache path.
 * @param versionIds Version ids to resolve.
 * @param onBatchReady Called with ready `{id, file:// URL}` pairs, coalesced.
 */
void scheduleThumbs(
    std::int64_t projectId,
    std::vector<std::int64_t> versionIds,
    std::function<void(std::vector<std::pair<std::int64_t, std::string>>)> onBatchReady);

/**
 * @brief Resolves one version thumbnail (cache or download by id).
 *
 * Thin wrapper over `scheduleThumbs`. `onReady` may run on a worker thread.
 * @param projectId Project id for the cache path.
 * @param versionId Version id for the cache path and the image fetch.
 * @param onReady Called with a `file://` URL when an image is ready. Not called when none.
 */
void scheduleOneThumb(
    std::int64_t projectId,
    std::int64_t versionId,
    std::function<void(std::string fileUrl)> onReady);

/**
 * @brief Thumbnail URL already stored for one version.
 * @param projectId Project id.
 * @param versionId Version id.
 * @return `file://` URL, or empty when the image is not ready.
 */
std::string thumbFor(std::int64_t projectId, std::int64_t versionId);

/**
 * @brief Remembers thumbnail file URLs for one project.
 * @param projectId Project the images belong to.
 * @param payload JSON list of version id and `file://` URL.
 */
void rememberThumbs(std::int64_t projectId, const xstudio::utility::JsonStore &payload);

} // namespace xstudio::mikros_shotbrowser
