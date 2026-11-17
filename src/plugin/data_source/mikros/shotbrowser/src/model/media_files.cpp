// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/media_files.cpp
 * @brief Checks that a movie or one frame of a sequence is on this machine.
 */

#include "model/media_files.hpp"

#include "model/thumb_store.hpp"

#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/media_reference.hpp"

#include <fstream>
#include <regex>

namespace xstudio::mikros_shotbrowser {
namespace {

/**
 * @brief True when a ShotGrid frame range can be appended to a sequence path.
 * @param range Text such as `1001-1100` or `1001-1100x2`.
 * @return False for an empty or prose range.
 */
bool usableRange(const std::string &range) {
    static const std::regex pattern(R"(^[0-9]+-[0-9]+(x[0-9]+)?$)");
    return std::regex_match(range, pattern);
}

} // namespace

bool regularFile(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

std::string sequenceSpec(const std::string &path, const std::string &frameRange) {
    if (path.empty()) {
        return {};
    }
    const bool ranged = usableRange(frameRange);
    const std::string spec = ranged && path.find('=') == std::string::npos ? path + "=" + frameRange
                                                                             : path;
    try {
        utility::FrameList frames;
        const caf::uri uri = utility::parse_cli_posix_path(spec, frames, !ranged);
        if (frames.empty()) {
            return {};
        }
        utility::MediaReference reference(uri, frames, false);
        const auto sample = reference.pick_representative_frame();
        if (!sample || !regularFile(utility::uri_to_posix_path(sample->first))) {
            return {}; ///< The pattern parsed, but no frame is on this site.
        }
        return spec;
    } catch (const std::exception &) {
        return {};
    }
}

std::filesystem::path movieCachePath(std::int64_t projectId, std::int64_t versionId) {
    const std::filesystem::path home = thumb::homeDirectory();
    if (home.empty()) {
        return {};
    }
    return home / "xStudio" / "mikros_shotbrowser" / "movies" / std::to_string(projectId) /
           std::to_string(versionId) / "movie.mp4";
}

bool writeMovie(const std::filesystem::path &path, const std::string &bytes) {
    if (path.empty() || bytes.empty()) {
        return false;
    }
    if (regularFile(path.string())) {
        return true; ///< This version was downloaded for an earlier open.
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
    return regularFile(path.string());
}

} // namespace xstudio::mikros_shotbrowser
