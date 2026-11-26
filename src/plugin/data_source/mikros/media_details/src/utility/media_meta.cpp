// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/media_meta.cpp
 * @brief Stamp and MediaReference helpers for Media Details.
 */

#include "utility/media_meta.hpp"

#include "xstudio/utility/helpers.hpp"

#include <filesystem>

namespace xstudio::mikros_media_details {
namespace {

/**
 * @brief Builds a `start-end` range string from a media reference.
 * @param media_ref On-screen media reference.
 * @return Frame range text, or empty when unknown.
 */
std::string frameRangeText(const utility::MediaReference &media_ref) {
    if (!media_ref.frame_list().empty()) {
        return to_string(media_ref.frame_list());
    }
    if (media_ref.frame_count() <= 0) {
        return {};
    }
    const int frameIn = media_ref.start_frame_offset();
    const int frameOut = frameIn + media_ref.frame_count() - 1;
    return std::to_string(frameIn) + "-" + std::to_string(frameOut);
}

/**
 * @brief Reads a container label from the media path extension.
 * @param path Posix path of the media.
 * @return Extension without the leading dot, or empty.
 */
std::string containerFromPath(const std::string &path) {
    if (path.empty()) {
        return {};
    }
    const std::filesystem::path file(path);
    std::string extension = file.extension().string();
    if (extension.empty() || extension == ".") {
        return {};
    }
    if (extension.front() == '.') {
        extension.erase(extension.begin());
    }
    return extension;
}

/**
 * @brief Builds the Media section for an image sequence.
 * @param media_ref On-screen media reference.
 * @param path Posix path already resolved from the URI.
 * @return Sequence-shaped JSON.
 */
nlohmann::json loadSequenceMediaDetails(
    const utility::MediaReference &media_ref, const std::string &path) {
    return nlohmann::json{
        {"kind", "sequence"},
        {"path", path},
        {"frameRange", frameRangeText(media_ref)},
        {"frameCount", media_ref.frame_count()}};
}

/**
 * @brief Builds the Media section for a video container.
 * @param media_ref On-screen media reference.
 * @param path Posix path already resolved from the URI.
 * @return Video-shaped JSON.
 */
nlohmann::json loadVideoMediaDetails(
    const utility::MediaReference &media_ref, const std::string &path) {
    return nlohmann::json{
        {"kind", "video"},
        {"path", path},
        {"container", containerFromPath(path)},
        {"codec", ""}, ///< Not on MediaReference; filled later if metadata is wired.
        {"fps", media_ref.rate().to_fps()},
        {"frameCount", media_ref.frame_count()},
        {"duration", media_ref.seconds()}};
}

} // namespace

std::optional<std::int64_t> versionIdFromStamp(const utility::JsonStore &meta) {
    if (!meta.is_object()) {
        return std::nullopt;
    }
    const std::int64_t id = meta.value("id", std::int64_t{0});
    if (id <= 0) {
        return std::nullopt;
    }
    return id;
}

nlohmann::json loadMediaDetails(const utility::MediaReference &media_ref) {
    const std::string path = utility::uri_to_posix_path(media_ref.uri());
    if (media_ref.container()) {
        return loadVideoMediaDetails(media_ref, path);
    }
    return loadSequenceMediaDetails(media_ref, path);
}

} // namespace xstudio::mikros_media_details
