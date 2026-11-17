// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/media_plan.hpp
 * @brief Which media sources a version can open.
 */
#pragma once

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Sources kept for one version after the disk check.
 *
 * A movie file on disk wins over the signed URL. The URL is only a fallback.
 */
struct SourceChoice {
    bool frames = false;    ///< Image sequence is on disk.
    bool movieFile = false; ///< `sg_path_to_movie` is on disk.
    bool movieUrl = false;  ///< Download `sg_uploaded_movie_mp4` because the movie file is not.
};

/**
 * @brief Picks the sources that can be opened.
 * @param framesOnDisk True when at least one frame of the sequence exists.
 * @param movieOnDisk True when the movie path is a regular file.
 * @param hasUploadedUrl True when ShotGrid sent a signed movie URL.
 * @return The sources to add. All three stay false when nothing can be opened.
 */
SourceChoice chooseSources(bool framesOnDisk, bool movieOnDisk, bool hasUploadedUrl);

} // namespace xstudio::mikros_shotbrowser
