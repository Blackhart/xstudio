// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/sequence_path.hpp
 * @brief Resolve ShotGrid frame-sequence paths into xSTUDIO specs via libfileseq.
 */
#pragma once

#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Result of resolving a ShotGrid `sg_path_to_frames` value.
 */
struct ResolvedFrames {
    bool exists = false;     ///< True when at least one frame is on disk.
    std::string xstudio_path; ///< Spec for `parse_cli_posix_path` (`{:04d}` + optional range).
};

/**
 * @brief True when `path` carries a frame-padding token.
 * @param path ShotGrid or xSTUDIO sequence path.
 * @return True for `{:` specs or fileseq padding (`%04d`, `#`, `@`).
 */
bool isFrameSequencePath(const std::string &path);

/**
 * @brief Parses ShotGrid / fileseq patterns and emits an xSTUDIO sequence path.
 *
 * Accepts `%04d`, `####`, `@`, and `{:` patterns. Prefers frames found on disk;
 * otherwise applies `frameRange` when the pattern is valid.
 * @param path `sg_path_to_frames` value from ShotGrid.
 * @param frameRange Version `frame_range` such as `1-120`. Empty keeps fileseq's range.
 * @return `exists` false when no usable frame is on this site.
 */
ResolvedFrames resolveShotGridFrames(
    const std::string &path, const std::string &frameRange = {});

} // namespace xstudio::mikros_shotbrowser
