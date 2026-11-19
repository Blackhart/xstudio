// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/sequence_path.cpp
 * @brief Resolve ShotGrid frame-sequence paths into xSTUDIO specs via libfileseq.
 */

#include "utility/sequence_path.hpp"

#include "xstudio/utility/helpers.hpp"

#include <fileseq/error.h>
#include <fileseq/fileseq.h>
#include <fileseq/sequence.h>

#include <filesystem>

namespace xstudio::mikros_shotbrowser {
namespace {

namespace fs = std::filesystem;

/**
 * @brief Applies xSTUDIO path remapping when a forward map exists.
 * @param path Posix path from ShotGrid.
 * @return The remapped path, or `path` when remapping is empty.
 */
std::string mappedPath(const std::string &path) {
    const std::string mapped = utility::forward_remap_file_path(path);
    return mapped.empty() ? path : mapped;
}

/**
 * @brief Builds an xSTUDIO sequence path from a fileseq sequence.
 * @param seq Parsed sequence with padding and optional on-disk range.
 * @param frameRange ShotGrid range used when fileseq did not discover one.
 * @return `dirname + basename + {:0Nd} + ext[=range]`.
 */
std::string toXstudioSequencePath(
    const fileseq::FileSequence &seq, const std::string &frameRange) {
    const int pad = seq.zfill();
    const std::string brace =
        pad > 0 ? "{:0" + std::to_string(pad) + "d}" : std::string("{:d}");
    std::string path = seq.dirname() + seq.basename() + brace + seq.ext();
    std::string range = seq.frameRange();
    if (range.empty()) {
        range = frameRange; ///< Fall back to the ShotGrid frame_range field.
    }
    if (!range.empty()) {
        path += "=" + range;
    }
    return path;
}

/**
 * @brief True when the first frame of `seq` is a regular file.
 * @param seq Sequence whose start frame is probed.
 * @return False when the start frame is missing or unreadable.
 */
bool firstFrameOnDisk(const fileseq::FileSequence &seq) {
    try {
        return fs::is_regular_file(seq.frame(seq.start()));
    } catch (...) {
        return false;
    }
}

} // namespace

bool isFrameSequencePath(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    if (path.find("{:") != std::string::npos) {
        return true;
    }
    fileseq::Status ok;
    const fileseq::FileSequence seq(path, &ok);
    return ok && seq.isValid() && !seq.padding().empty();
}

ResolvedFrames resolveShotGridFrames(
    const std::string &path, const std::string &frameRange) {
    ResolvedFrames result;
    result.xstudio_path = path;
    if (path.empty()) {
        return result;
    }

    const std::string check = mappedPath(path);
    fileseq::Status ok;
    fileseq::FileSequence seq = fileseq::findSequenceOnDisk(check, &ok);
    if (ok && seq.isValid() && seq.length() > 0) {
        result.exists = true;
        result.xstudio_path = toXstudioSequencePath(seq, frameRange);
        return result;
    }

    seq = fileseq::FileSequence(check, &ok);
    if (!ok || !seq.isValid()) {
        return result;
    }

    if (!frameRange.empty()) {
        fileseq::Status rangeOk;
        seq.setFrameRange(frameRange, &rangeOk);
        if (!rangeOk) {
            return result;
        }
    }

    result.exists = firstFrameOnDisk(seq);
    result.xstudio_path = toXstudioSequencePath(seq, frameRange);
    return result;
}

} // namespace xstudio::mikros_shotbrowser
