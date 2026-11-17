// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/thumb_store.hpp
 * @brief Disk path of one version thumbnail.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace xstudio::mikros_shotbrowser::thumb {

/**
 * @brief What the page worker does with one version image.
 */
enum class ThumbKind {
    None,  ///< Show the placeholder. Nothing is written.
    Ready, ///< `path` is a file the card can show.
    Fetch  ///< Download the URL and write `path`.
};

/**
 * @brief Decision for one version image.
 */
struct ThumbPlan {
    ThumbKind kind = ThumbKind::None; ///< Next step for this version.
    std::filesystem::path path;       ///< File to show, or the cache file to write.
};

/**
 * @brief Home directory resolved at call time.
 * @return `HOME`, or `USERPROFILE` on Windows. Empty when neither is set.
 */
std::filesystem::path homeDirectory();

/**
 * @brief Directory that holds every version thumbnail.
 * @return `${HOME}/xStudio/mikros_shotbrowser/thumbs`, or empty when home is missing.
 */
std::filesystem::path thumbRoot();

/**
 * @brief Cache file of one version.
 * @param root Directory from `thumbRoot`.
 * @param projectId Numeric project id.
 * @param versionId Numeric version id.
 * @return `<root>/<projectId>/<versionId>/thumbnail.jpg`.
 */
std::filesystem::path cacheFile(
    const std::filesystem::path &root, std::int64_t projectId, std::int64_t versionId);

/**
 * @brief True when `path` is a non-empty regular file.
 * @param path File to test.
 * @return True when the file exists and its size is greater than zero.
 */
bool fileHasBytes(const std::filesystem::path &path);

/**
 * @brief Chooses placeholder, direct file, cache hit, or download.
 * @param root Cache directory. Empty means an `http` image cannot be stored.
 * @param image URL or filesystem path from the version. Empty means no picture.
 * @param provider `http`, `file`, or anything else.
 * @param projectId Numeric project id.
 * @param versionId Numeric version id.
 * @return The step for this version. A present cache file is `Ready`, not `Fetch`.
 */
ThumbPlan planThumb(
    const std::filesystem::path &root,
    const std::string &image,
    const std::string &provider,
    std::int64_t projectId,
    std::int64_t versionId);

/**
 * @brief Writes `bytes` to `path` when the file is absent or empty.
 * @param path Destination, including `thumbnail.jpg`.
 * @param bytes Image bytes already checked by the caller.
 * @return True when the file is non-empty afterwards. False when `bytes` is empty.
 */
bool writeBytesIfMissing(const std::filesystem::path &path, const std::string &bytes);

} // namespace xstudio::mikros_shotbrowser::thumb
