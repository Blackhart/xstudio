// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/thumb_fetch.hpp
 * @brief Downloads one image and turns a disk path into a `file://` URL.
 */
#pragma once

#include <filesystem>
#include <string>

namespace xstudio::mikros_shotbrowser::thumb {

/**
 * @brief Downloads `url` and returns JPEG bytes.
 * @param url Address from the version `image` field.
 * @return JPEG bytes, or empty when the request fails or the body is not an image.
 */
std::string fetchJpeg(const std::string &url);

/**
 * @brief Builds the URL a QML `Image` can load.
 * @param path Local file, either the cache or a `file` provider path.
 * @return `file://` URL. Empty when `path` is empty.
 */
std::string toFileUrl(const std::filesystem::path &path);

} // namespace xstudio::mikros_shotbrowser::thumb
