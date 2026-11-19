// SPDX-License-Identifier: Apache-2.0
/**
 * @file fake/backend.cpp
 * @brief Fixed catalogs and In/Where matching, including weak playlist memberships.
 */

#include "pms/fake/backend.hpp"

#include "pms/error.hpp"

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace pms {
namespace {

/**
 * @brief Zero-padded label of one project.
 *
 * The label runs opposite to the id (`20` for id 1) so the combo sort is visible.
 * Child rows use the same label, so a project named `paw14` owns `paw14_…` rows.
 * @param projectId Project id, from 1.
 * @return Two digits, for example `14`.
 */
std::string projectLabel(int projectId) {
    const int nameIndex = static_cast<int>(FakeBackend::projectCount) + 1 - projectId;
    return (nameIndex < 10 ? "0" : "") + std::to_string(nameIndex);
}

/**
 * @brief Builds the twenty projects.
 *
 * Ids stay in ascending order so keyset pages keep working.
 */
Records makeProjects() {
    Records rows;
    rows.reserve(FakeBackend::projectCount);
    for (int index = 1; index <= static_cast<int>(FakeBackend::projectCount); ++index) {
        const std::string number = projectLabel(index); ///< Same label the children will use.
        Record row;
        row.fields["id"] = std::int64_t{index};
        row.fields["name"] = "paw" + number;
        row.fields["code"] = "PAW" + number;
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief Builds two sequences per project (`pawNN_sq010`, `pawNN_sq020`).
 *
 * The private `project` int links a sequence to its project id.
 */
Records makeSequences() {
    Records rows;
    rows.reserve(FakeBackend::projectCount * FakeBackend::sequencesPerProject);
    std::int64_t sequenceId = 1;
    for (int projectId = 1; projectId <= static_cast<int>(FakeBackend::projectCount);
         ++projectId) {
        const std::string projectNumber = projectLabel(projectId);
        for (std::size_t index = 1; index <= FakeBackend::sequencesPerProject; ++index) {
            const int shotNumber = static_cast<int>(index) * 10;
            const std::string sequenceNumber =
                (shotNumber < 100 ? "0" : "") + std::to_string(shotNumber);
            Record row;
            row.fields["id"] = sequenceId++;
            row.fields["name"] = "paw" + projectNumber + "_sq" + sequenceNumber;
            row.fields["project"] = std::int64_t{projectId};
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

/**
 * @brief Builds two shots per sequence (`…_sh010`, `…_sh020`).
 *
 * Private `sequence` and `project` ints support In(sequence) and In(project).
 */
Records makeShots(const Records &sequences) {
    Records rows;
    rows.reserve(sequences.size() * FakeBackend::shotsPerSequence);
    std::int64_t shotId = 1;
    for (const Record &sequence : sequences) {
        const std::string sequenceName = std::get<std::string>(sequence.fields.at("name"));
        for (std::size_t index = 1; index <= FakeBackend::shotsPerSequence; ++index) {
            const int shotNumber = static_cast<int>(index) * 10;
            const std::string shotLabel =
                (shotNumber < 100 ? "0" : "") + std::to_string(shotNumber);
            Record row;
            row.fields["id"] = shotId++;
            row.fields["name"] = sequenceName + "_sh" + shotLabel;
            row.fields["sequence"] = sequence.fields.at("id");
            row.fields["project"] = sequence.fields.at("project");
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

/**
 * @brief Builds two assets per project (`pawNN_ast01`, `pawNN_ast02`).
 *
 * The private `project` int links an asset to its project id.
 */
Records makeAssets() {
    Records rows;
    rows.reserve(FakeBackend::projectCount * FakeBackend::assetsPerProject);
    std::int64_t assetId = 1;
    for (int projectId = 1; projectId <= static_cast<int>(FakeBackend::projectCount);
         ++projectId) {
        const std::string projectNumber = projectLabel(projectId);
        for (std::size_t index = 1; index <= FakeBackend::assetsPerProject; ++index) {
            const std::string assetNumber =
                (index < 10 ? "0" : "") + std::to_string(index);
            Record row;
            row.fields["id"] = assetId++;
            row.fields["name"] = "paw" + projectNumber + "_ast" + assetNumber;
            row.fields["project"] = std::int64_t{projectId};
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

/**
 * @brief Builds two playlists per project (`pawNN_pl01`, `pawNN_pl02`).
 *
 * The private `project` int links a playlist to its project id.
 */
Records makePlaylists() {
    Records rows;
    rows.reserve(FakeBackend::projectCount * FakeBackend::playlistsPerProject);
    std::int64_t playlistId = 1;
    for (int projectId = 1; projectId <= static_cast<int>(FakeBackend::projectCount);
         ++projectId) {
        const std::string projectNumber = projectLabel(projectId);
        for (std::size_t index = 1; index <= FakeBackend::playlistsPerProject; ++index) {
            const std::string playlistNumber =
                (index < 10 ? "0" : "") + std::to_string(index);
            Record row;
            row.fields["id"] = playlistId++;
            row.fields["name"] = "paw" + projectNumber + "_pl" + playlistNumber;
            row.fields["project"] = std::int64_t{projectId};
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

/**
 * @brief Builds weak playlist memberships for existing versions.
 *
 * `_v001` versions join `pl01`, `_v002` join `pl02`. The first sequence `_v001`
 * of each project also joins `pl02`, so one version can sit in two playlists.
 *
 * @param versions Catalog of strongly parented versions.
 * @param playlists Playlist catalog.
 * @return version id → playlist ids.
 */
std::map<std::int64_t, std::vector<std::int64_t>> linkVersionsToPlaylists(
    const Records &versions, const Records &playlists) {
    std::map<std::int64_t, std::vector<std::int64_t>> memberships;
    std::map<std::int64_t, std::vector<std::int64_t>> playlistsByProject;
    for (const Record &playlist : playlists) {
        const auto projectId = std::get<std::int64_t>(playlist.fields.at("project"));
        playlistsByProject[projectId].push_back(
            std::get<std::int64_t>(playlist.fields.at("id")));
    }

    for (const Record &version : versions) {
        const auto versionId = std::get<std::int64_t>(version.fields.at("id"));
        const auto projectId = std::get<std::int64_t>(version.fields.at("project"));
        const auto &projectPlaylists = playlistsByProject[projectId];
        if (projectPlaylists.empty()) {
            continue;
        }
        const std::string name = std::get<std::string>(version.fields.at("name"));
        const bool isV001 =
            name.size() >= 5 && name.compare(name.size() - 5, 5, "_v001") == 0;
        const bool isV002 =
            name.size() >= 5 && name.compare(name.size() - 5, 5, "_v002") == 0;
        if (isV001) {
            memberships[versionId].push_back(projectPlaylists.front());
        }
        if (isV002 && projectPlaylists.size() > 1) {
            memberships[versionId].push_back(projectPlaylists[1]);
        }
    }

    // One multi-playlist sample per project: first sequence `_v001` also in pl02.
    for (const auto &[projectId, projectPlaylists] : playlistsByProject) {
        if (projectPlaylists.size() < 2) {
            continue;
        }
        for (const Record &version : versions) {
            if (std::get<std::int64_t>(version.fields.at("project")) != projectId) {
                continue;
            }
            if (version.fields.count("sequence") == 0 || version.fields.count("shot") != 0) {
                continue;
            }
            const std::string name = std::get<std::string>(version.fields.at("name"));
            if (name.size() < 5 || name.compare(name.size() - 5, 5, "_v001") != 0) {
                continue;
            }
            const auto versionId = std::get<std::int64_t>(version.fields.at("id"));
            auto &links = memberships[versionId];
            if (std::find(links.begin(), links.end(), projectPlaylists[1]) == links.end()) {
                links.push_back(projectPlaylists[1]);
            }
            break;
        }
    }
    return memberships;
}

/**
 * @brief 5x7 glyphs for ASCII 32–90. Lowercase maps to uppercase.
 *
 * Our OIIO build has no FreeType, so `render_text` cannot run. Each byte is one
 * row; bits 4..0 are the columns left to right.
 */
const std::uint8_t kGlyph5x7[][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00},
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x00, 0x00},
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03},
    {0x08, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00}, {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x02, 0x04, 0x04, 0x04, 0x04, 0x02, 0x00}, {0x08, 0x04, 0x04, 0x04, 0x04, 0x08, 0x00},
    {0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00}, {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}, {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00}, {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00},
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x0E, 0x00}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00},
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x1F, 0x00}, {0x1F, 0x02, 0x06, 0x01, 0x11, 0x0E, 0x00},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x00}, {0x1F, 0x10, 0x1E, 0x01, 0x11, 0x0E, 0x00},
    {0x06, 0x08, 0x1E, 0x11, 0x11, 0x0E, 0x00}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x00},
    {0x0E, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x0C, 0x00},
    {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00}, {0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x08},
    {0x02, 0x04, 0x08, 0x04, 0x02, 0x00, 0x00}, {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},
    {0x08, 0x04, 0x02, 0x04, 0x08, 0x00, 0x00}, {0x0E, 0x11, 0x01, 0x06, 0x00, 0x04, 0x00},
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}, {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00},
    {0x1E, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00}, {0x0E, 0x11, 0x10, 0x10, 0x11, 0x0E, 0x00},
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00}, {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00},
    {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00}, {0x0E, 0x11, 0x10, 0x13, 0x11, 0x0F, 0x00},
    {0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, {0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00},
    {0x01, 0x01, 0x01, 0x01, 0x11, 0x0E, 0x00}, {0x11, 0x12, 0x1C, 0x12, 0x11, 0x11, 0x00},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00}, {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x00},
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x00}, {0x0E, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00},
    {0x1E, 0x11, 0x11, 0x1E, 0x12, 0x11, 0x00}, {0x0F, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00},
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},
    {0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00}, {0x11, 0x11, 0x11, 0x15, 0x1B, 0x11, 0x00},
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}, {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x00},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00},
};

/**
 * @brief Glyph rows for one printable character.
 * @param ch Character to draw. Unknown glyphs become '?'.
 * @return Seven row bitmasks.
 */
const std::uint8_t *glyphFor(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
    }
    if (ch >= ' ' && ch <= 'Z') {
        return kGlyph5x7[static_cast<std::size_t>(ch - ' ')];
    }
    return kGlyph5x7[static_cast<std::size_t>('?' - ' ')];
}

/**
 * @brief Draws `label` centered into an RGB buffer with the 5x7 font.
 * @param pixels RGB8 buffer of `width` x `height`.
 * @param width Image width.
 * @param height Image height.
 * @param label Text to draw. Truncated when too wide.
 */
void drawCenteredLabel(
    std::vector<std::uint8_t> &pixels, int width, int height, const std::string &label) {
    constexpr int scale = 2;
    constexpr int glyphH = 7 * scale;
    constexpr int advance = 5 * scale + scale;
    const int maxChars = (width - 24) / advance;
    std::string text = label;
    if (maxChars > 0 && static_cast<int>(text.size()) > maxChars) {
        text = text.substr(0, static_cast<std::size_t>(maxChars));
    }
    if (text.empty()) {
        return;
    }
    const int textW = static_cast<int>(text.size()) * advance - scale;
    const int originX = std::max(0, (width - textW) / 2);
    const int originY = std::max(0, (height - glyphH) / 2);
    auto put = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const std::size_t at = static_cast<std::size_t>((y * width + x) * 3);
        pixels[at] = 230;
        pixels[at + 1] = 230;
        pixels[at + 2] = 235;
    };
    for (std::size_t index = 0; index < text.size(); ++index) {
        const std::uint8_t *glyph = glyphFor(text[index]);
        const int baseX = originX + static_cast<int>(index) * advance;
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] & (1 << (4 - col))) == 0) {
                    continue;
                }
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        put(baseX + col * scale + dx, originY + row * scale + dy);
                    }
                }
            }
        }
    }
}

/**
 * @brief True when `path` is a non-empty regular file.
 * @param path File to test.
 * @return False when missing, empty, or not a file.
 */
bool fileHasBytes(const std::filesystem::path &path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return false;
    }
    return std::filesystem::file_size(path, error) > 0 && !error;
}

/**
 * @brief Writes one RGB JPEG with a centered label via OpenImageIO.
 * @param path Destination file.
 * @param label Text drawn on the image.
 * @param bgR Background red in 0..1.
 * @param bgG Background green in 0..1.
 * @param bgB Background blue in 0..1.
 * @return True when the file was written.
 */
bool writeLabeledJpeg(
    const std::filesystem::path &path,
    const std::string &label,
    float bgR,
    float bgG,
    float bgB) {
    constexpr int width = 320;
    constexpr int height = 180;
    OIIO::ImageSpec spec(width, height, 3, OIIO::TypeDesc::UINT8);
    OIIO::ImageBuf image(spec);
    const float background[3] = {bgR, bgG, bgB};
    if (!OIIO::ImageBufAlgo::fill(image, background)) {
        return false;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 3));
    if (!image.get_pixels(OIIO::ROI(), OIIO::TypeDesc::UINT8, pixels.data())) {
        return false;
    }
    drawCenteredLabel(pixels, width, height, label);
    if (!image.set_pixels(OIIO::ROI(), OIIO::TypeDesc::UINT8, pixels.data())) {
        return false;
    }
    return image.write(path.string());
}

constexpr int kFirstFrame = 1001; ///< Inclusive start of the fake sequence.
constexpr int kLastFrame = 1005;  ///< Inclusive end of the fake sequence.

/**
 * @brief File name for one padded frame of a version.
 * @param name Version name prefix.
 * @param frame Frame number, for example 1001.
 * @return `name.1001.jpg`.
 */
std::string frameFileName(const std::string &name, int frame) {
    std::ostringstream fileName;
    fileName << name << '.' << std::setw(4) << std::setfill('0') << frame << ".jpg";
    return fileName.str();
}

/**
 * @brief True when every frame file of the fake sequence is on disk.
 * @param dir Version media folder.
 * @param name Version name prefix.
 * @return False when any frame is missing.
 */
bool sequenceReady(const std::filesystem::path &dir, const std::string &name) {
    for (int frame = kFirstFrame; frame <= kLastFrame; ++frame) {
        if (!fileHasBytes(dir / frameFileName(name, frame))) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Paths for the thumb, the sequence pattern, and the movie of one version.
 */
struct VersionAssetPaths {
    std::string thumb;  ///< Local thumbnail JPEG. Empty when missing.
    std::string frames; ///< Sequence pattern `name.####.jpg`. Empty when incomplete.
    std::string movie;  ///< Local `movie.mov`. Empty when ffmpeg did not write it.
};

/**
 * @brief Ensures thumb, sequence, and movie exist; creates only what is missing.
 *
 * When all three are already on disk, returns their paths with no write and no
 * ffmpeg. Frames stay usable when ffmpeg fails.
 * @param name Version name used as the media folder and frame prefix.
 * @param projectId Project id for the thumb cache folder.
 * @param versionId Version id for the thumb cache folder.
 * @return Absolute paths under `~/xStudio/mikros_shotbrowser/`, or empty fields when home is missing.
 */
VersionAssetPaths ensureVersionAssets(
    const std::string &name, std::int64_t projectId, std::int64_t versionId) {
    VersionAssetPaths paths;
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return paths;
    }
    const std::filesystem::path root =
        std::filesystem::path(home) / "xStudio" / "mikros_shotbrowser";
    const std::filesystem::path thumbPath =
        root / "fake_thumbs" / std::to_string(projectId) / std::to_string(versionId) /
        "thumbnail.jpg";
    const std::filesystem::path mediaDir = root / "fake_media" / name;
    const std::filesystem::path moviePath = mediaDir / "movie.mov";
    const std::string framesPattern = (mediaDir / (name + ".####.jpg")).string();

    const bool thumbOk = fileHasBytes(thumbPath);
    const bool sequenceOk = sequenceReady(mediaDir, name);
    const bool movieOk = fileHasBytes(moviePath);
    if (thumbOk) {
        paths.thumb = thumbPath.string();
    }
    if (sequenceOk) {
        paths.frames = framesPattern;
    }
    if (movieOk) {
        paths.movie = moviePath.string();
    }
    if (thumbOk && sequenceOk && movieOk) {
        return paths; ///< Nothing to generate for this version.
    }

    std::error_code error;
    if (!thumbOk) {
        std::filesystem::create_directories(thumbPath.parent_path(), error);
        if (!error) {
            const float bgR = static_cast<float>(32 + (versionId % 40)) / 255.0f;
            if (writeLabeledJpeg(thumbPath, name, bgR, 36.0f / 255.0f, 48.0f / 255.0f)) {
                paths.thumb = thumbPath.string();
            }
        }
    }

    if (!sequenceOk) {
        error.clear();
        std::filesystem::create_directories(mediaDir, error);
        if (!error) {
            for (int frame = kFirstFrame; frame <= kLastFrame; ++frame) {
                const std::filesystem::path framePath = mediaDir / frameFileName(name, frame);
                if (fileHasBytes(framePath)) {
                    continue;
                }
                const std::string label = name + " " + std::to_string(frame);
                const float bgR = static_cast<float>(32 + (frame % 40)) / 255.0f;
                writeLabeledJpeg(framePath, label, bgR, 40.0f / 255.0f, 52.0f / 255.0f);
            }
            if (sequenceReady(mediaDir, name)) {
                paths.frames = framesPattern;
            }
        }
    }

    if (!movieOk && !paths.frames.empty()) {
        const std::string pattern = (mediaDir / (name + ".%04d.jpg")).string();
        // Drop xstudio/vcpkg LD_LIBRARY_PATH: system ffmpeg then loads a mismatched FreeType.
        const std::string cmd =
            "env -u LD_LIBRARY_PATH -u LD_PRELOAD /usr/bin/ffmpeg -y -loglevel error "
            "-framerate 24 -start_number " +
            std::to_string(kFirstFrame) + " -i '" + pattern + "' -frames:v " +
            std::to_string(kLastFrame - kFirstFrame + 1) +
            " -c:v libx264 -pix_fmt yuv420p '" + moviePath.string() + "'";
        const int ffmpegStatus = std::system(cmd.c_str());
        if (ffmpegStatus == 0 && fileHasBytes(moviePath)) {
            paths.movie = moviePath.string();
        }
    }
    return paths;
}

/**
 * @brief Fills every VersionCard field, including local thumb and open-media paths.
 * @param row Version row that already has id, name, and project.
 * @param index 1-based index among the parent's versions (varies the sample).
 */
void fillVersionCard(Record &row, std::size_t index) {
    const std::int64_t id = std::get<std::int64_t>(row.fields.at("id"));
    const std::string name = std::get<std::string>(row.fields.at("name"));
    const std::int64_t projectId = std::get<std::int64_t>(row.fields.at("project"));
    const std::size_t pick = static_cast<std::size_t>((id + static_cast<std::int64_t>(index)) % 5);

    static const char *authors[] = {"Ada", "Lin", "Sam", "Kai", "Noa"};
    static const char *steps[] = {"Comp", "Light", "Anim", "FX", "Paint"};
    static const char *statuses[] = {"ip", "rev", "apr", "hld", "omt"};
    static const char *artistic[] = {"apr", "wip", "ret", "fin", "na"};
    static const char *sites[] = {
        "Paris",
        "Montreal",
        "Bangalore",
        "Paris,Montreal",
        "Paris,Bangalore"};
    static const char *descriptions[] = {
        "Comp review",
        "Lighting pass",
        "Anim blocking",
        "FX polish",
        "Paint fix"};

    row.fields["author"] = std::string(authors[pick]);
    row.fields["created_at"] =
        std::string("2024-0") + std::to_string(1 + static_cast<int>(pick)) + "-0" +
        std::to_string(1 + static_cast<int>(index));
    row.fields["frame_range"] = std::string("1001-1005");
    row.fields["description"] = std::string(descriptions[pick]);
    row.fields["step"] = std::string(steps[pick]);
    row.fields["status"] = std::string(statuses[pick]);
    row.fields["artistic_status"] = std::string(artistic[pick]);
    row.fields["sites"] = std::string(sites[pick]);
    row.fields["has_notes"] = std::int64_t{(id % 2) == 0 ? 1 : 0};
    row.fields["synced_to_client"] = std::int64_t{(id % 3) == 0 ? 1 : 0};
    row.fields["uploaded_movie"] = "https://example.test/movies/" + name + ".mp4";
    row.fields["uploaded_movie_provider"] = std::string("signedurl");

    const VersionAssetPaths assets = ensureVersionAssets(name, projectId, id);
    if (!assets.frames.empty()) {
        row.fields["frames"] = assets.frames;
        row.fields["frames_provider"] = std::string("fileSeq");
    }
    if (!assets.movie.empty()) {
        row.fields["movie"] = assets.movie;
        row.fields["movie_provider"] = std::string("file");
    }
    if (!assets.thumb.empty()) {
        row.fields["image"] = assets.thumb;
        row.fields["image_provider"] = std::string("file");
    }
}

/**
 * @brief Builds sequence-linked, shot-linked, and asset-linked versions.
 *
 * Sequence versions omit `shot` and `asset`. Shot versions set `shot`,
 * `sequence`, and `project`. Asset versions set `asset` and `project`.
 * Playlist membership is stored separately (weak many-to-many).
 * Every version carries a full VersionCard payload, a local thumb, and open-media files.
 */
Records makeVersions(
    const Records &sequences, const Records &shots, const Records &assets) {
    Records rows;
    rows.reserve(
        sequences.size() * FakeBackend::versionsPerSequence +
        shots.size() * FakeBackend::versionsPerShot +
        assets.size() * FakeBackend::versionsPerAsset);
    std::int64_t versionId = 1;

    for (const Record &sequence : sequences) {
        const std::string sequenceName = std::get<std::string>(sequence.fields.at("name"));
        for (std::size_t index = 1; index <= FakeBackend::versionsPerSequence; ++index) {
            const std::string versionNumber =
                (index < 10 ? "00" : (index < 100 ? "0" : "")) + std::to_string(index);
            Record row;
            row.fields["id"] = versionId++;
            row.fields["name"] = sequenceName + "_v" + versionNumber;
            row.fields["sequence"] = sequence.fields.at("id");
            row.fields["project"] = sequence.fields.at("project");
            fillVersionCard(row, index);
            rows.push_back(std::move(row));
        }
    }

    for (const Record &shot : shots) {
        const std::string shotName = std::get<std::string>(shot.fields.at("name"));
        for (std::size_t index = 1; index <= FakeBackend::versionsPerShot; ++index) {
            const std::string versionNumber =
                (index < 10 ? "00" : (index < 100 ? "0" : "")) + std::to_string(index);
            Record row;
            row.fields["id"] = versionId++;
            row.fields["name"] = shotName + "_v" + versionNumber;
            row.fields["shot"] = shot.fields.at("id");
            row.fields["sequence"] = shot.fields.at("sequence");
            row.fields["project"] = shot.fields.at("project");
            fillVersionCard(row, index);
            rows.push_back(std::move(row));
        }
    }

    for (const Record &asset : assets) {
        const std::string assetName = std::get<std::string>(asset.fields.at("name"));
        for (std::size_t index = 1; index <= FakeBackend::versionsPerAsset; ++index) {
            const std::string versionNumber =
                (index < 10 ? "00" : (index < 100 ? "0" : "")) + std::to_string(index);
            Record row;
            row.fields["id"] = versionId++;
            row.fields["name"] = assetName + "_v" + versionNumber;
            row.fields["asset"] = asset.fields.at("id");
            row.fields["project"] = asset.fields.at("project");
            fillVersionCard(row, index);
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

bool matchesWhere(const Record &row, const ExprNode &node) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("fake backend: not expects one child");
            }
            return !matchesWhere(row, *node.args[0]);
        }
        if (node.op != ExprOp::And && node.op != ExprOp::Or) {
            throw Error("fake backend: invalid group");
        }
        if (node.args.size() < 2) {
            throw Error("fake backend: group expects at least two children");
        }
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("fake backend: empty child");
            }
            const bool childMatches = matchesWhere(row, *child);
            if (node.op == ExprOp::And && !childMatches) {
                return false;
            }
            if (node.op == ExprOp::Or && childMatches) {
                return true;
            }
        }
        return node.op == ExprOp::And;
    }

    if (node.kind != ExprKind::Field) {
        throw Error("fake backend: where expects a field leaf");
    }
    if (!node.value) {
        throw Error("fake backend: field \"" + node.key + "\" has no value");
    }
    const auto found = row.fields.find(node.key);
    const bool equal = found != row.fields.end() && found->second == *node.value;
    if (node.op == ExprOp::Eq) {
        return equal;
    }
    if (node.op == ExprOp::Ne) {
        return !equal;
    }
    throw Error("fake backend: operator not allowed on \"" + node.key + "\"");
}

std::optional<std::int64_t> idByName(const Records &catalog, const std::string &name) {
    for (const Record &row : catalog) {
        const auto found = row.fields.find("name");
        if (found != row.fields.end() && found->second == Value{name}) {
            return std::get<std::int64_t>(row.fields.at("id"));
        }
    }
    return std::nullopt;
}

/**
 * @brief Resolves a scope leaf to a parent id from the parent catalog.
 */
std::optional<std::int64_t> parentId(
    const Records &parents, const ExprNode &node, const char *parentKey) {
    if (node.key != parentKey) {
        throw Error("fake backend: parent \"" + node.key + "\" is not available");
    }
    if (!node.value) {
        throw Error(std::string("fake backend: parent \"") + parentKey + "\" has no value");
    }
    if (const auto *id = std::get_if<std::int64_t>(&*node.value)) {
        return *id;
    }
    if (const auto *name = std::get_if<std::string>(&*node.value)) {
        return idByName(parents, *name);
    }
    throw Error(std::string("fake backend: parent \"") + parentKey + "\" expects an id or a name");
}

bool matchesIn(
    const Record &row,
    const Records &parents,
    const ExprNode &node,
    const char *parentKey,
    const char *linkField) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("fake backend: not expects one child");
            }
            return !matchesIn(row, parents, *node.args[0], parentKey, linkField);
        }
        if (node.op != ExprOp::And && node.op != ExprOp::Or) {
            throw Error("fake backend: invalid group");
        }
        if (node.args.size() < 2) {
            throw Error("fake backend: group expects at least two children");
        }
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("fake backend: empty child");
            }
            const bool childMatches = matchesIn(row, parents, *child, parentKey, linkField);
            if (node.op == ExprOp::And && !childMatches) {
                return false;
            }
            if (node.op == ExprOp::Or && childMatches) {
                return true;
            }
        }
        return node.op == ExprOp::And;
    }

    if (node.kind != ExprKind::Scope) {
        throw Error("fake backend: in expects a scope leaf");
    }
    const auto expected = parentId(parents, node, parentKey);
    const auto linked = row.fields.find(linkField);
    const bool equal =
        expected && linked != row.fields.end() && linked->second == Value{*expected};
    if (node.op == ExprOp::Eq) {
        return equal;
    }
    if (node.op == ExprOp::Ne) {
        return !equal;
    }
    throw Error("fake backend: operator not allowed on parent \"" + node.key + "\"");
}

/**
 * @brief Matches an In tree whose leaves may name different parents.
 *
 * @param row Catalog row with private link fields.
 * @param node In expression root.
 * @param matchLeaf Resolves one scope leaf against the correct parent catalog.
 */
template <typename MatchLeaf>
bool matchesScopedIn(const Record &row, const ExprNode &node, MatchLeaf &&matchLeaf) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("fake backend: not expects one child");
            }
            return !matchesScopedIn(row, *node.args[0], matchLeaf);
        }
        if (node.op != ExprOp::And && node.op != ExprOp::Or) {
            throw Error("fake backend: invalid group");
        }
        if (node.args.size() < 2) {
            throw Error("fake backend: group expects at least two children");
        }
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("fake backend: empty child");
            }
            const bool childMatches = matchesScopedIn(row, *child, matchLeaf);
            if (node.op == ExprOp::And && !childMatches) {
                return false;
            }
            if (node.op == ExprOp::Or && childMatches) {
                return true;
            }
        }
        return node.op == ExprOp::And;
    }
    if (node.kind != ExprKind::Scope) {
        throw Error("fake backend: in expects a scope leaf");
    }
    return matchLeaf(row, node);
}

Record publicNamed(const Record &row) {
    Record out;
    out.fields["id"] = row.fields.at("id");
    out.fields["name"] = row.fields.at("name");
    return out;
}

/**
 * @brief Copies one field when the catalog row has it.
 * @param out Public row being built.
 * @param row Catalog row.
 * @param key Domain field name.
 */
void copyPresent(Record &out, const Record &row, const std::string &key) {
    const auto found = row.fields.find(key);
    if (found == row.fields.end()) {
        return;
    }
    out.fields[key] = found->second; ///< The card reads this. A missing key stays N/A.
}

/**
 * @brief Returns a version without its private parent links.
 * @param row Catalog version, including the card fields and thumb path.
 * @return Id, name, and every VersionCard field already filled.
 */
Record publicVersion(const Record &row) {
    Record out = publicNamed(row);
    copyPresent(out, row, "image");
    copyPresent(out, row, "image_provider");
    copyPresent(out, row, "author");
    copyPresent(out, row, "created_at");
    copyPresent(out, row, "frame_range");
    copyPresent(out, row, "description");
    copyPresent(out, row, "step");
    copyPresent(out, row, "status");
    copyPresent(out, row, "artistic_status");
    copyPresent(out, row, "sites");
    copyPresent(out, row, "has_notes");
    copyPresent(out, row, "synced_to_client");
    copyPresent(out, row, "frames");
    copyPresent(out, row, "frames_provider");
    copyPresent(out, row, "movie");
    copyPresent(out, row, "movie_provider");
    copyPresent(out, row, "uploaded_movie");
    copyPresent(out, row, "uploaded_movie_provider");
    return out;
}

Records selectWhere(const Records &catalog, const std::shared_ptr<const ExprNode> &where) {
    if (!where) {
        return catalog;
    }
    Records rows;
    for (const Record &row : catalog) {
        if (matchesWhere(row, *where)) {
            rows.push_back(row);
        }
    }
    return rows;
}

} // namespace

FakeBackend::FakeBackend(bool simulate_latency)
    : simulate_latency_(simulate_latency),
      projects_(makeProjects()),
      sequences_(makeSequences()),
      shots_(makeShots(sequences_)),
      assets_(makeAssets()),
      playlists_(makePlaylists()),
      versions_(makeVersions(sequences_, shots_, assets_)),
      version_playlists_(linkVersionsToPlaylists(versions_, playlists_)) {}

void FakeBackend::maybeSleep() const {
    if (!simulate_latency_) {
        return;
    }
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> delay_ms(1500, 2500); ///< Visible Loading… in the Shot Browser.
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms(rng)));
}

Page FakeBackend::find(const Ir &ir) {
    if (!ir.op || *ir.op != Op::Find) {
        throw Error("fake backend: missing operation");
    }
    Records matched = matchAll(ir);
    Page page;
    if (!ir.limit) {
        page.rows = std::move(matched);
        maybeSleep();
        return page;
    }
    std::int64_t afterId = 0;
    if (ir.cursor) {
        try {
            afterId = std::stoll(*ir.cursor);
        } catch (const std::exception &) {
            throw Error("fake backend: cursor must be an integer id");
        }
    }
    for (const Record &row : matched) {
        const auto id = std::get<std::int64_t>(row.at("id"));
        if (id <= afterId) {
            continue;
        }
        page.rows.push_back(row);
        if (page.rows.size() == *ir.limit) {
            break;
        }
    }
    if (!page.rows.empty()) {
        const auto lastId = std::get<std::int64_t>(page.rows.back().at("id"));
        const bool more = std::any_of(
            matched.begin(),
            matched.end(),
            [lastId](const Record &row) {
                return std::get<std::int64_t>(row.at("id")) > lastId;
            });
        if (more) {
            page.next_cursor = std::to_string(lastId);
        }
    }
    maybeSleep();
    return page;
}

std::size_t FakeBackend::count(const Ir &ir) {
    if (!ir.op || *ir.op != Op::Count) {
        throw Error("fake backend: missing operation");
    }
    const std::size_t total = matchAll(ir).size();
    maybeSleep();
    return total;
}

Records FakeBackend::matchAll(const Ir &ir) const {
    if (!ir.op || (*ir.op != Op::Find && *ir.op != Op::Count)) {
        throw Error("fake backend: missing operation");
    }

    Records rows;
    if (ir.entity == "project") {
        if (ir.in) {
            throw Error("fake backend: project has no parents");
        }
        rows = selectWhere(projects_, ir.where);
    } else if (ir.entity == "sequence") {
        for (const Record &sequence : sequences_) {
            if (ir.in && !matchesIn(sequence, projects_, *ir.in, "project", "project")) {
                continue;
            }
            if (ir.where && !matchesWhere(sequence, *ir.where)) {
                continue;
            }
            Record row = publicNamed(sequence);
            const auto sequenceId = std::get<std::int64_t>(sequence.fields.at("id"));
            std::vector<Link> shots;
            for (const Record &shot : shots_) {
                if (std::get<std::int64_t>(shot.fields.at("sequence")) != sequenceId) {
                    continue;
                }
                Link link;
                link.id = std::get<std::int64_t>(shot.fields.at("id"));
                link.name = std::get<std::string>(shot.fields.at("name"));
                shots.push_back(std::move(link));
            }
            if (!shots.empty()) {
                row.fields["shots"] = std::move(shots); ///< Same shape as the ShotGrid link list.
            }
            rows.push_back(std::move(row));
        }
    } else if (ir.entity == "shot") {
        for (const Record &shot : shots_) {
            if (ir.in) {
                const auto matchShotLeaf = [&](const Record &row, const ExprNode &node) {
                    if (node.key == "sequence") {
                        return matchesIn(row, sequences_, node, "sequence", "sequence");
                    }
                    if (node.key == "project") {
                        return matchesIn(row, projects_, node, "project", "project");
                    }
                    throw Error("fake backend: parent \"" + node.key + "\" is not available");
                };
                if (!matchesScopedIn(shot, *ir.in, matchShotLeaf)) {
                    continue;
                }
            }
            if (ir.where && !matchesWhere(shot, *ir.where)) {
                continue;
            }
            rows.push_back(publicNamed(shot));
        }
    } else if (ir.entity == "asset") {
        for (const Record &asset : assets_) {
            if (ir.in && !matchesIn(asset, projects_, *ir.in, "project", "project")) {
                continue;
            }
            if (ir.where && !matchesWhere(asset, *ir.where)) {
                continue;
            }
            rows.push_back(publicNamed(asset));
        }
    } else if (ir.entity == "playlist") {
        for (const Record &playlist : playlists_) {
            if (ir.in && !matchesIn(playlist, projects_, *ir.in, "project", "project")) {
                continue;
            }
            if (ir.where && !matchesWhere(playlist, *ir.where)) {
                continue;
            }
            Record row = publicNamed(playlist);
            const auto playlistId = std::get<std::int64_t>(playlist.fields.at("id"));
            std::vector<Link> versions;
            for (const auto &[versionId, playlistIds] : version_playlists_) {
                if (std::find(playlistIds.begin(), playlistIds.end(), playlistId) ==
                    playlistIds.end()) {
                    continue;
                }
                Link link;
                link.id = versionId;
                for (const Record &version : versions_) {
                    if (std::get<std::int64_t>(version.fields.at("id")) != versionId) {
                        continue;
                    }
                    link.name = std::get<std::string>(version.fields.at("name"));
                    break;
                }
                versions.push_back(std::move(link));
            }
            if (!versions.empty()) {
                row.fields["versions"] = std::move(versions); ///< Same shape as ShotGrid links.
            }
            rows.push_back(std::move(row));
        }
    } else if (ir.entity == "version") {
        for (const Record &version : versions_) {
            if (ir.in) {
                const auto matchVersionLeaf = [&](const Record &row, const ExprNode &node) {
                    if (node.key == "shot") {
                        return matchesIn(row, shots_, node, "shot", "shot");
                    }
                    if (node.key == "sequence") {
                        return matchesIn(row, sequences_, node, "sequence", "sequence");
                    }
                    if (node.key == "asset") {
                        return matchesIn(row, assets_, node, "asset", "asset");
                    }
                    if (node.key == "playlist") {
                        const auto expected = parentId(playlists_, node, "playlist");
                        const auto versionId = std::get<std::int64_t>(row.fields.at("id"));
                        const auto found = version_playlists_.find(versionId);
                        const bool member =
                            expected && found != version_playlists_.end() &&
                            std::find(found->second.begin(), found->second.end(), *expected) !=
                                found->second.end();
                        if (node.op == ExprOp::Eq) {
                            return member;
                        }
                        if (node.op == ExprOp::Ne) {
                            return !member;
                        }
                        throw Error(
                            "fake backend: operator not allowed on parent \"" + node.key + "\"");
                    }
                    if (node.key == "project") {
                        return matchesIn(row, projects_, node, "project", "project");
                    }
                    throw Error("fake backend: parent \"" + node.key + "\" is not available");
                };
                if (!matchesScopedIn(version, *ir.in, matchVersionLeaf)) {
                    continue;
                }
            }
            if (ir.where && !matchesWhere(version, *ir.where)) {
                continue;
            }
            rows.push_back(publicVersion(version));
        }
    } else {
        throw Error("fake backend: entity \"" + ir.entity + "\" is not available");
    }

    return rows;
}

} // namespace pms
