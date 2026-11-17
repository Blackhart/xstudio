// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/thumb_store.cpp
 * @brief Decides whether a version image is already on disk.
 */

#include "model/thumb_store.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace xstudio::mikros_shotbrowser::thumb {
namespace {

/**
 * @brief Plan for an image that is already a local file.
 * @param image Filesystem path from the version.
 * @return `Ready` when the file has bytes, otherwise `None`.
 */
ThumbPlan planFile(const std::string &image) {
    const std::filesystem::path path(image);
    if (!fileHasBytes(path)) {
        return {};
    }
    ThumbPlan plan;
    plan.kind = ThumbKind::Ready;
    plan.path = path;
    return plan;
}

/**
 * @brief Plan for an image that must be downloaded once.
 * @param root Cache directory.
 * @param projectId Numeric project id.
 * @param versionId Numeric version id.
 * @return `Ready` when `thumbnail.jpg` is already there, otherwise `Fetch`.
 */
ThumbPlan planHttp(
    const std::filesystem::path &root, std::int64_t projectId, std::int64_t versionId) {
    if (root.empty()) {
        return {};
    }
    ThumbPlan plan;
    plan.path = cacheFile(root, projectId, versionId);
    plan.kind = fileHasBytes(plan.path) ? ThumbKind::Ready : ThumbKind::Fetch;
    return plan;
}

} // namespace

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

std::filesystem::path thumbRoot() {
    const std::filesystem::path home = homeDirectory();
    if (home.empty()) {
        return {};
    }
    return home / "xStudio" / "mikros_shotbrowser" / "thumbs";
}

std::filesystem::path cacheFile(
    const std::filesystem::path &root, std::int64_t projectId, std::int64_t versionId) {
    return root / std::to_string(projectId) / std::to_string(versionId) / "thumbnail.jpg";
}

bool fileHasBytes(const std::filesystem::path &path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return false;
    }
    return std::filesystem::file_size(path, error) > 0 && !error;
}

ThumbPlan planThumb(
    const std::filesystem::path &root,
    const std::string &image,
    const std::string &provider,
    std::int64_t projectId,
    std::int64_t versionId) {
    if (image.empty()) {
        return {};
    }
    if (provider == "file") {
        return planFile(image);
    }
    if (provider == "http") {
        return planHttp(root, projectId, versionId);
    }
    return {};
}

bool writeBytesIfMissing(const std::filesystem::path &path, const std::string &bytes) {
    if (fileHasBytes(path)) {
        return true; ///< Another page already stored this version.
    }
    if (bytes.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return fileHasBytes(path);
}

} // namespace xstudio::mikros_shotbrowser::thumb
