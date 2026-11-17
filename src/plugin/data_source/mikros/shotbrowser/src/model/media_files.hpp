// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/media_files.hpp
 * @brief Disk checks and the local cache path for one version movie.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief True when `path` is a regular file.
 * @param path Posix path from ShotGrid, or a cache file.
 * @return False when the path is empty, missing, or not a file.
 */
bool regularFile(const std::string &path);

/**
 * @brief True when the sequence has at least one frame on disk.
 *
 * `frameRange` is appended as an xSTUDIO range when it looks like `1001-1100`.
 * Otherwise the directory is scanned.
 *
 * @param path ShotGrid sequence path, with `#`, `@`, or `{frame}`.
 * @param frameRange Version frame range. Empty scans the directory.
 * @return The spec `parse_cli_posix_path` accepted, or empty when no frame exists.
 */
std::string sequenceSpec(const std::string &path, const std::string &frameRange);

/**
 * @brief Cache file for one downloaded movie.
 * @param projectId Project id. Zero when no project is selected.
 * @param versionId Version id.
 * @return `${HOME}/xStudio/mikros_shotbrowser/movies/<projectId>/<versionId>/movie.mp4`.
 *         Empty when the home directory is missing.
 */
std::filesystem::path movieCachePath(std::int64_t projectId, std::int64_t versionId);

/**
 * @brief Writes `bytes` to `path` when the file is not already there.
 * @param path Cache file.
 * @param bytes Movie body.
 * @return True when `path` contains bytes afterwards.
 */
bool writeMovie(const std::filesystem::path &path, const std::string &bytes);

} // namespace xstudio::mikros_shotbrowser
