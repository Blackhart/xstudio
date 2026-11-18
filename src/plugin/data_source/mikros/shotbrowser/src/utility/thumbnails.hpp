// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/thumbnails.hpp
 * @brief Disk cache and JPEG download. Cards read the stored URL and do not download.
 */
#pragma once

#include "utility/home.hpp"
#include "model/poco/version.hpp"

#include "xstudio/utility/json_store.hpp"

#include <caf/actor.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
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
 * @brief Stores one page of images without blocking the actor.
 * @param weak Actor that receives the local file URLs.
 * @param projectId Project the page belongs to.
 * @param rows Versions of this page, with their image source.
 */
void scheduleThumbPage(
    const caf::weak_actor_ptr &weak,
    std::int64_t projectId,
    const std::vector<poco::Version> &rows);

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
