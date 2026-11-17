// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/media_plan.cpp
 * @brief Chooses image-sequence and movie sources for one version.
 */

#include "model/media_plan.hpp"

namespace xstudio::mikros_shotbrowser {

SourceChoice chooseSources(bool framesOnDisk, bool movieOnDisk, bool hasUploadedUrl) {
    SourceChoice choice;
    choice.frames = framesOnDisk; ///< Keep the sequence when a frame is on this site.
    choice.movieFile = movieOnDisk;
    choice.movieUrl = !movieOnDisk && hasUploadedUrl; ///< Signed URL only replaces a missing movie.
    return choice;
}

} // namespace xstudio::mikros_shotbrowser
