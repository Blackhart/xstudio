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
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
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
 * @brief UTC calendar day for the wall clock.
 */
Date todayUtc() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
    gmtime_r(&now, &parts);
    return Date{parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday};
}

/**
 * @brief Builds a UTC time_t for one calendar day and clock time.
 */
std::time_t utcTime(const Date &day, int hour = 0, int minute = 0, int second = 0) {
    std::tm parts{};
    parts.tm_year = day.year - 1900;
    parts.tm_mon = day.month - 1;
    parts.tm_mday = day.day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    parts.tm_isdst = 0;
    return timegm(&parts);
}

/**
 * @brief Adds a signed day delta to a calendar day (UTC).
 */
Date addDays(const Date &day, int delta) {
    const std::time_t shifted = utcTime(day, 12) + static_cast<std::time_t>(delta) * 86400;
    std::tm parts{};
    gmtime_r(&shifted, &parts);
    return Date{parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday};
}

/**
 * @brief Formats a day as `YYYY-MM-DD`.
 */
std::string formatIsoDay(const Date &day) {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << day.year << '-' << std::setw(2) << day.month
        << '-' << std::setw(2) << day.day;
    return out.str();
}

/**
 * @brief Formats a day and hour as `YYYY-MM-DDTHH:00:00Z`.
 */
std::string formatIsoDateTime(const Date &day, int hour) {
    std::ostringstream out;
    out << formatIsoDay(day) << 'T' << std::setfill('0') << std::setw(2) << hour << ":00:00Z";
    return out.str();
}

/**
 * @brief Builds the twenty projects.
 *
 * Ids stay in ascending order so keyset pages keep working.
 */
Records makeProjects() {
    const char *statuses[] = {"Active", "delivered", "Bidding", "Hold"};
    const char *types[] = {"Episodic", "Feature Film", "Test"};
    Records rows;
    rows.reserve(FakeBackend::projectCount);
    for (int index = 1; index <= static_cast<int>(FakeBackend::projectCount); ++index) {
        const std::string number = projectLabel(index); ///< Same label the children will use.
        Record row;
        row.fields["id"] = std::int64_t{index};
        row.fields["name"] = "paw" + number;
        row.fields["code"] = "PAW" + number;
        row.fields["status"] = std::string(statuses[(index - 1) % 4]);
        row.fields["type"] = std::string(types[(index - 1) % 3]);
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
            const std::int64_t id = sequenceId++;
            row.fields["id"] = id;
            row.fields["name"] = "paw" + projectNumber + "_sq" + sequenceNumber;
            row.fields["description"] = "Sequence " + sequenceNumber;
            row.fields["status"] = std::string((index % 2) == 0 ? "ip" : "apr");
            row.fields["cut_duration_in_sec"] = std::int64_t{120 + static_cast<std::int64_t>(index) * 10};
            row.fields["estimated_frames_count"] = std::int64_t{2400};
            row.fields["estimated_shots_count"] = std::int64_t{12};
            row.fields["shot_count"] = std::int64_t{FakeBackend::shotsPerSequence};
            row.fields["sequence_order"] = std::int64_t{static_cast<std::int64_t>(index) * 10};
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
            const std::int64_t id = shotId++;
            const std::int64_t cutIn = 1001;
            const std::int64_t cutOut = 1100;
            row.fields["id"] = id;
            row.fields["name"] = sequenceName + "_sh" + shotLabel;
            row.fields["description"] = "Shot " + shotLabel;
            row.fields["status"] = std::string((index % 2) == 0 ? "ip" : "rev");
            row.fields["cut_duration_in_sec"] = std::int64_t{4};
            row.fields["cut_in"] = cutIn;
            row.fields["cut_out"] = cutOut;
            row.fields["cut_order"] = std::int64_t{static_cast<std::int64_t>(index) * 10};
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
            row.fields["description"] = "Asset " + assetNumber;
            row.fields["status"] = std::string((index % 2) == 0 ? "ip" : "apr");
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
            const std::int64_t id = playlistId++;
            row.fields["id"] = id;
            row.fields["name"] = "paw" + projectNumber + "_pl" + playlistNumber;
            row.fields["project"] = std::int64_t{projectId};
            static const char *authors[] = {"Ada", "Lin", "Sam", "Kai", "Noa"};
            static const char *departments[] = {"Animation", "Lighting", "Comp", "FX"};
            static const char *categories[] = {"Dailies", "Client", "Internal", "Temp"};
            row.fields["author"] = std::string(authors[id % 5]);
            row.fields["departments"] = std::string(departments[id % 4]);
            row.fields["category"] = std::string(categories[id % 4]);
            const Date createdDay = addDays(todayUtc(), -static_cast<int>(id % 60));
            row.fields["created_at"] = formatIsoDateTime(createdDay, 12);
            row.fields["open_notes_count"] = std::int64_t{(id % 3) == 0 ? 2 : 0};
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
        formatIsoDay(addDays(todayUtc(), -static_cast<int>(index - 1)));
    row.fields["frame_range"] = std::string("1001-1005");
    row.fields["frame_count"] = std::int64_t{5};
    row.fields["description"] = std::string(descriptions[pick]);
    row.fields["client_code"] = name + "_CLIENT";
    row.fields["client_reviewed"] = std::int64_t{(id % 4) == 0 ? 1 : 0};
    row.fields["client_approved"] = std::int64_t{(id % 5) == 0 ? 1 : 0};
    row.fields["is_latest"] = std::int64_t{(index == 1) ? 1 : 0};
    row.fields["version_number"] = std::int64_t{static_cast<std::int64_t>(index)};
    row.fields["task"] = std::string(steps[pick]) + " Task";
    row.fields["step"] = std::string(steps[pick]);
    row.fields["status"] = std::string(statuses[pick]);
    row.fields["artistic_status"] = std::string(artistic[pick]);
    row.fields["sites"] = std::string(sites[pick]);
    row.fields["open_notes_count"] = std::int64_t{(id % 2) == 0 ? 2 : 0};
    row.fields["synced_to_client"] = std::int64_t{(id % 3) == 0 ? 1 : 0};
    row.fields["work_source_scene"] = "/work/" + name + ".ma";
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

/**
 * @brief Lowercases ASCII text for case-insensitive string ops.
 */
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

/**
 * @brief Reports whether `left` is on or before `right` by year, month, day.
 */
bool dayOnOrBefore(const Date &left, const Date &right) {
    if (left.year != right.year) {
        return left.year < right.year;
    }
    if (left.month != right.month) {
        return left.month < right.month;
    }
    return left.day <= right.day;
}

/**
 * @brief Reads a calendar day from a Date or an ISO / date string.
 */
std::optional<Date> asDay(const Value &value) {
    if (const auto *date = std::get_if<Date>(&value)) {
        return *date;
    }
    const auto *text = std::get_if<std::string>(&value);
    if (!text || text->size() < 10) {
        return std::nullopt;
    }
    try {
        return parseDate(text->substr(0, 10));
    } catch (const Error &) {
        return std::nullopt;
    }
}

/**
 * @brief Instant for a field value (date-only → midnight UTC).
 */
std::optional<std::chrono::system_clock::time_point> asInstant(const Value &value) {
    if (const auto *date = std::get_if<Date>(&value)) {
        return std::chrono::system_clock::from_time_t(utcTime(*date));
    }
    const auto *text = std::get_if<std::string>(&value);
    if (!text || text->size() < 10) {
        return std::nullopt;
    }
    Date day;
    try {
        day = parseDate(text->substr(0, 10));
    } catch (const Error &) {
        return std::nullopt;
    }
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (text->size() >= 19 && (*text)[10] == 'T') {
        try {
            hour = std::stoi(text->substr(11, 2));
            minute = std::stoi(text->substr(14, 2));
            second = std::stoi(text->substr(17, 2));
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }
    return std::chrono::system_clock::from_time_t(utcTime(day, hour, minute, second));
}

/**
 * @brief Monday (UTC) of the week that contains `day`.
 */
Date weekStartMonday(const Date &day) {
    const std::time_t noon = utcTime(day, 12);
    std::tm parts{};
    gmtime_r(&noon, &parts);
    const int daysFromMonday = parts.tm_wday == 0 ? 6 : parts.tm_wday - 1;
    return addDays(day, -daysFromMonday);
}

/**
 * @brief Inclusive calendar-day window for an in_calendar_* leaf.
 */
std::pair<Date, Date> calendarWindow(ExprOp op, std::int64_t offset, const Date &today) {
    if (op == ExprOp::InCalendarDay) {
        const Date day = addDays(today, static_cast<int>(offset));
        return {day, day};
    }
    if (op == ExprOp::InCalendarWeek) {
        const Date start = addDays(weekStartMonday(today), static_cast<int>(offset) * 7);
        return {start, addDays(start, 6)};
    }
    if (op == ExprOp::InCalendarMonth) {
        int month = today.month + static_cast<int>(offset);
        int year = today.year;
        while (month < 1) {
            month += 12;
            --year;
        }
        while (month > 12) {
            month -= 12;
            ++year;
        }
        const Date start{year, month, 1};
        const Date end{year, month, daysInMonth(year, month)};
        return {start, end};
    }
    throw Error("fake backend: not a calendar operator");
}

/**
 * @brief Whether a field instant falls in the past period ending at `now`.
 */
bool matchesInLast(
    const Value &field,
    const Period &period,
    std::chrono::system_clock::time_point now) {
    const auto instant = asInstant(field);
    if (!instant || *instant > now) {
        return false;
    }
    using namespace std::chrono;
    system_clock::time_point start = now;
    if (period.unit == "HOUR") {
        start -= hours(period.count);
    } else if (period.unit == "DAY") {
        start -= hours(period.count * 24);
    } else if (period.unit == "WEEK") {
        start -= hours(period.count * 24 * 7);
    } else if (period.unit == "MONTH") {
        start -= hours(period.count * 24 * 30);
    } else if (period.unit == "YEAR") {
        start -= hours(period.count * 24 * 365);
    } else {
        throw Error("fake backend: unknown in_last unit \"" + period.unit + "\"");
    }
    return *instant >= start;
}

/**
 * @brief Reads inclusive int bounds from IntBounds or a two-int shelf list.
 */
bool intBounds(const Value &value, std::int64_t &low, std::int64_t &high) {
    if (const auto *bounds = std::get_if<IntBounds>(&value)) {
        low = bounds->low;
        high = bounds->high;
        return true;
    }
    const auto *items = std::get_if<std::vector<std::int64_t>>(&value);
    if (!items || items->size() != 2) {
        return false;
    }
    low = (*items)[0];
    high = (*items)[1];
    return true;
}

/**
 * @brief Reads inclusive day bounds from DateBounds or a two-string shelf list.
 */
bool dayBounds(const Value &value, Date &low, Date &high) {
    if (const auto *bounds = std::get_if<DateBounds>(&value)) {
        low = bounds->low;
        high = bounds->high;
        return true;
    }
    const auto *items = std::get_if<std::vector<std::string>>(&value);
    if (!items || items->size() != 2) {
        return false;
    }
    const auto left = asDay(Value{(*items)[0]});
    const auto right = asDay(Value{(*items)[1]});
    if (!left || !right) {
        return false;
    }
    low = *left;
    high = *right;
    return true;
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
    const Value &leaf = *node.value;

    const auto fieldString = [&]() -> const std::string * {
        if (found == row.fields.end()) {
            return nullptr;
        }
        return std::get_if<std::string>(&found->second);
    };
    const auto fieldInt = [&]() -> const std::int64_t * {
        if (found == row.fields.end()) {
            return nullptr;
        }
        return std::get_if<std::int64_t>(&found->second);
    };

    if (node.op == ExprOp::Contains || node.op == ExprOp::NotContains ||
        node.op == ExprOp::StartsWith || node.op == ExprOp::EndsWith ||
        node.op == ExprOp::NameContains || node.op == ExprOp::NameStartsWith) {
        const auto *needle = std::get_if<std::string>(&leaf);
        if (!needle) {
            throw Error("fake backend: string op expects a string on \"" + node.key + "\"");
        }
        const std::string *haystack = fieldString();
        if (!haystack) {
            return node.op == ExprOp::NotContains;
        }
        const std::string left = toLower(*haystack);
        const std::string right = toLower(*needle);
        if (node.op == ExprOp::Contains || node.op == ExprOp::NameContains) {
            return left.find(right) != std::string::npos;
        }
        if (node.op == ExprOp::NotContains) {
            return left.find(right) == std::string::npos;
        }
        if (node.op == ExprOp::StartsWith || node.op == ExprOp::NameStartsWith) {
            return left.size() >= right.size() && left.compare(0, right.size(), right) == 0;
        }
        return left.size() >= right.size() &&
               left.compare(left.size() - right.size(), right.size(), right) == 0;
    }

    if (node.op == ExprOp::NameIs) {
        const auto *needle = std::get_if<std::string>(&leaf);
        if (!needle) {
            throw Error("fake backend: name_is expects a string on \"" + node.key + "\"");
        }
        const std::string *haystack = fieldString();
        return haystack && *haystack == *needle;
    }

    if (node.op == ExprOp::In || node.op == ExprOp::NotIn) {
        if (const auto *items = std::get_if<std::vector<std::string>>(&leaf)) {
            const std::string *text = fieldString();
            if (!text) {
                return node.op == ExprOp::NotIn;
            }
            const bool member =
                std::find(items->begin(), items->end(), *text) != items->end();
            return node.op == ExprOp::In ? member : !member;
        }
        if (const auto *items = std::get_if<std::vector<std::int64_t>>(&leaf)) {
            const std::int64_t *number = fieldInt();
            if (!number) {
                return node.op == ExprOp::NotIn;
            }
            const bool member =
                std::find(items->begin(), items->end(), *number) != items->end();
            return node.op == ExprOp::In ? member : !member;
        }
        throw Error("fake backend: in expects a list on \"" + node.key + "\"");
    }

    if (node.op == ExprOp::Between) {
        std::int64_t lowInt = 0;
        std::int64_t highInt = 0;
        if (intBounds(leaf, lowInt, highInt)) {
            const std::int64_t *number = fieldInt();
            return number && *number >= lowInt && *number <= highInt;
        }
        Date lowDay;
        Date highDay;
        if (dayBounds(leaf, lowDay, highDay)) {
            if (found == row.fields.end()) {
                return false;
            }
            const auto day = asDay(found->second);
            return day && dayOnOrBefore(lowDay, *day) && dayOnOrBefore(*day, highDay);
        }
        throw Error("fake backend: between expects bounds on \"" + node.key + "\"");
    }

    if (node.op == ExprOp::Gt || node.op == ExprOp::Lt) {
        if (const auto *number = std::get_if<std::int64_t>(&leaf)) {
            const std::int64_t *field = fieldInt();
            if (!field) {
                return false;
            }
            return node.op == ExprOp::Gt ? *field > *number : *field < *number;
        }
        const auto leafDay = asDay(leaf);
        if (!leafDay) {
            throw Error("fake backend: gt/lt expects int or day on \"" + node.key + "\"");
        }
        if (found == row.fields.end()) {
            return false;
        }
        const auto fieldDay = asDay(found->second);
        if (!fieldDay) {
            return false;
        }
        if (node.op == ExprOp::Gt) {
            return !dayOnOrBefore(*fieldDay, *leafDay) && !(*fieldDay == *leafDay);
        }
        return !dayOnOrBefore(*leafDay, *fieldDay) && !(*fieldDay == *leafDay);
    }

    if (node.op == ExprOp::InLast) {
        const auto *period = std::get_if<Period>(&leaf);
        if (!period) {
            throw Error("fake backend: in_last expects a period on \"" + node.key + "\"");
        }
        if (found == row.fields.end()) {
            return false;
        }
        return matchesInLast(found->second, *period, std::chrono::system_clock::now());
    }

    if (node.op == ExprOp::InCalendarDay || node.op == ExprOp::InCalendarWeek ||
        node.op == ExprOp::InCalendarMonth) {
        const auto *offset = std::get_if<std::int64_t>(&leaf);
        if (!offset) {
            throw Error("fake backend: calendar op expects an int on \"" + node.key + "\"");
        }
        if (found == row.fields.end()) {
            return false;
        }
        const auto day = asDay(found->second);
        if (!day) {
            return false;
        }
        const auto window = calendarWindow(node.op, *offset, todayUtc());
        return dayOnOrBefore(window.first, *day) && dayOnOrBefore(*day, window.second);
    }

    if (node.op == ExprOp::Eq || node.op == ExprOp::Ne) {
        if (std::holds_alternative<Date>(leaf)) {
            if (found == row.fields.end()) {
                return node.op == ExprOp::Ne;
            }
            const auto fieldDay = asDay(found->second);
            const auto leafDay = asDay(leaf);
            const bool equal = fieldDay && leafDay && *fieldDay == *leafDay;
            return node.op == ExprOp::Eq ? equal : !equal;
        }
        const bool equal = found != row.fields.end() && found->second == leaf;
        return node.op == ExprOp::Eq ? equal : !equal;
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
 * @brief Finds a parent catalog row by id.
 */
const Record *parentById(const Records &parents, std::int64_t id) {
    for (const Record &row : parents) {
        if (std::get<std::int64_t>(row.fields.at("id")) == id) {
            return &row;
        }
    }
    return nullptr;
}

/**
 * @brief Resolves an In leaf that filters by parent id to that id.
 */
std::optional<std::int64_t> parentId(
    const Records &parents, const ExprNode &node, const char *parentKey) {
    if (node.parent != parentKey) {
        throw Error("fake backend: parent \"" + node.parent + "\" is not available");
    }
    if (node.key != "id") {
        throw Error(
            "fake backend: parentId expects id field on \"" + node.parent + "\"");
    }
    if (!node.value) {
        throw Error(std::string("fake backend: parent \"") + parentKey + "\" has no value");
    }
    if (const auto *id = std::get_if<std::int64_t>(&*node.value)) {
        return *id;
    }
    throw Error(std::string("fake backend: parent \"") + parentKey + "\" id expects an int");
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

    if (node.kind != ExprKind::Field) {
        throw Error("fake backend: in expects a field leaf");
    }
    if (node.parent != parentKey) {
        throw Error("fake backend: parent \"" + node.parent + "\" is not available");
    }
    // Parent id eq/ne: compare the private link field to the expected id.
    if (node.key == "id" && (node.op == ExprOp::Eq || node.op == ExprOp::Ne)) {
        const auto expected = parentId(parents, node, parentKey);
        const auto linked = row.fields.find(linkField);
        const bool equal =
            expected && linked != row.fields.end() && linked->second == Value{*expected};
        return node.op == ExprOp::Eq ? equal : !equal;
    }
    // Other parent fields: resolve the linked parent row, then match the field on it.
    const auto linked = row.fields.find(linkField);
    if (linked == row.fields.end() || !std::holds_alternative<std::int64_t>(linked->second)) {
        return false;
    }
    const Record *parent = parentById(parents, std::get<std::int64_t>(linked->second));
    if (!parent) {
        return false;
    }
    ExprNode fieldOnly = node;
    fieldOnly.parent.clear(); ///< matchesWhere ignores parent.
    return matchesWhere(*parent, fieldOnly);
}

/**
 * @brief Matches an In tree whose leaves may name different parents.
 *
 * @param row Catalog row with private link fields.
 * @param node In expression root.
 * @param matchLeaf Resolves one parent field leaf against the correct catalog.
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
    if (node.kind != ExprKind::Field) {
        throw Error("fake backend: in expects a field leaf");
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
 * @brief Attaches a linked entity name, `{key}_id`, and `{key}_type` on a public row.
 * @param out Public row being built.
 * @param row Private catalog row that holds the parent int under `linkKey`.
 * @param parents Catalog used to resolve the parent name.
 * @param linkKey Private int field on `row` (e.g. `project`, `sequence`).
 * @param fieldKey Domain entity field name written on `out`.
 * @param typeName ShotGrid-style type label (e.g. `Project`, `Sequence`).
 */
void attachParentEntity(
    Record &out,
    const Record &row,
    const Records &parents,
    const std::string &linkKey,
    const std::string &fieldKey,
    const char *typeName) {
    const auto linked = row.fields.find(linkKey);
    if (linked == row.fields.end() || !std::holds_alternative<std::int64_t>(linked->second)) {
        return;
    }
    const std::int64_t parentId = std::get<std::int64_t>(linked->second);
    out.fields[fieldKey + "_id"] = parentId;
    out.fields[fieldKey + "_type"] = std::string(typeName);
    if (const Record *parent = parentById(parents, parentId)) {
        out.fields[fieldKey] = std::get<std::string>(parent->fields.at("name"));
    }
}

/**
 * @brief Returns a sequence with public `project` / `project_id` for Media Details.
 * @param row Private sequence row.
 * @param projects Project catalog for name lookup.
 * @return Id, name, and project label fields.
 */
Record publicSequence(const Record &row, const Records &projects) {
    Record out = publicNamed(row);
    copyPresent(out, row, "description");
    copyPresent(out, row, "status");
    copyPresent(out, row, "cut_duration_in_sec");
    copyPresent(out, row, "estimated_frames_count");
    copyPresent(out, row, "estimated_shots_count");
    copyPresent(out, row, "shot_count");
    copyPresent(out, row, "sequence_order");
    attachParentEntity(out, row, projects, "project", "project", "Project");
    return out;
}

/**
 * @brief Returns a shot with public `sequence` / `sequence_id` for Media Details.
 * @param row Private shot row.
 * @param sequences Sequence catalog for name lookup.
 * @return Id, name, and sequence label fields.
 */
Record publicShot(const Record &row, const Records &sequences) {
    Record out = publicNamed(row);
    copyPresent(out, row, "description");
    copyPresent(out, row, "status");
    copyPresent(out, row, "cut_duration_in_sec");
    copyPresent(out, row, "cut_in");
    copyPresent(out, row, "cut_out");
    copyPresent(out, row, "cut_order");
    attachParentEntity(out, row, sequences, "sequence", "sequence", "Sequence");
    return out;
}

/**
 * @brief Returns an asset with public `project` / `project_id` for Media Details.
 * @param row Private asset row.
 * @param projects Project catalog for name lookup.
 * @return Id, name, and project label fields.
 */
Record publicAsset(const Record &row, const Records &projects) {
    Record out = publicNamed(row);
    copyPresent(out, row, "description");
    copyPresent(out, row, "status");
    attachParentEntity(out, row, projects, "project", "project", "Project");
    return out;
}

/**
 * @brief Returns a version without its private parent link ints.
 *
 * Fills public `project` / `project_id` and polymorphic `entity` /
 * `entity_id` / `entity_type` for Media Details from the private links.
 *
 * @param row Catalog version, including the card fields and thumb path.
 * @param projects Project catalog for name lookup.
 * @param sequences Sequence catalog for entity lookup.
 * @param shots Shot catalog for entity lookup.
 * @param assets Asset catalog for entity lookup.
 * @return Id, name, card fields, and parent labels.
 */
Record publicVersion(
    const Record &row,
    const Records &projects,
    const Records &sequences,
    const Records &shots,
    const Records &assets) {
    Record out = publicNamed(row);
    copyPresent(out, row, "image");
    copyPresent(out, row, "image_provider");
    copyPresent(out, row, "author");
    copyPresent(out, row, "created_at");
    copyPresent(out, row, "frame_range");
    copyPresent(out, row, "frame_count");
    copyPresent(out, row, "description");
    copyPresent(out, row, "client_code");
    copyPresent(out, row, "client_reviewed");
    copyPresent(out, row, "client_approved");
    copyPresent(out, row, "is_latest");
    copyPresent(out, row, "version_number");
    copyPresent(out, row, "task");
    copyPresent(out, row, "step");
    copyPresent(out, row, "status");
    copyPresent(out, row, "artistic_status");
    copyPresent(out, row, "sites");
    copyPresent(out, row, "open_notes_count");
    copyPresent(out, row, "synced_to_client");
    copyPresent(out, row, "work_source_scene");
    copyPresent(out, row, "frames");
    copyPresent(out, row, "frames_provider");
    copyPresent(out, row, "movie");
    copyPresent(out, row, "movie_provider");
    copyPresent(out, row, "uploaded_movie");
    copyPresent(out, row, "uploaded_movie_provider");

    const auto projectField = row.fields.find("project");
    if (projectField != row.fields.end() &&
        std::holds_alternative<std::int64_t>(projectField->second)) {
        const std::int64_t projectId = std::get<std::int64_t>(projectField->second);
        out.fields["project_id"] = projectId;
        if (const Record *project = parentById(projects, projectId)) {
            out.fields["project"] = std::get<std::string>(project->fields.at("name"));
        }
    }

    const auto attachEntity =
        [&out](const char *type, const Records &catalog, const std::string &linkKey, const Record &src) {
            const auto linked = src.fields.find(linkKey);
            if (linked == src.fields.end() ||
                !std::holds_alternative<std::int64_t>(linked->second)) {
                return false;
            }
            const std::int64_t entityId = std::get<std::int64_t>(linked->second);
            const Record *entity = parentById(catalog, entityId);
            if (!entity) {
                return false;
            }
            out.fields["entity_type"] = std::string(type);
            out.fields["entity_id"] = entityId;
            out.fields["entity"] = std::get<std::string>(entity->fields.at("name"));
            return true;
        };

    if (!attachEntity("Shot", shots, "shot", row) &&
        !attachEntity("Asset", assets, "asset", row)) {
        attachEntity("Sequence", sequences, "sequence", row);
    }
    return out;
}

/**
 * @brief Returns a playlist without its private project link.
 * @param row Catalog playlist, including the header card fields.
 * @return Id, name, and every PlaylistCard field already filled.
 */
Record publicPlaylist(const Record &row) {
    Record out = publicNamed(row);
    copyPresent(out, row, "open_notes_count");
    copyPresent(out, row, "departments");
    copyPresent(out, row, "created_at");
    copyPresent(out, row, "category");
    copyPresent(out, row, "author");
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
            Record row = publicSequence(sequence, projects_);
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
                    if (node.parent == "sequence") {
                        return matchesIn(row, sequences_, node, "sequence", "sequence");
                    }
                    if (node.parent == "project") {
                        return matchesIn(row, projects_, node, "project", "project");
                    }
                    throw Error(
                        "fake backend: parent \"" + node.parent + "\" is not available");
                };
                if (!matchesScopedIn(shot, *ir.in, matchShotLeaf)) {
                    continue;
                }
            }
            if (ir.where && !matchesWhere(shot, *ir.where)) {
                continue;
            }
            rows.push_back(publicShot(shot, sequences_));
        }
    } else if (ir.entity == "asset") {
        for (const Record &asset : assets_) {
            if (ir.in && !matchesIn(asset, projects_, *ir.in, "project", "project")) {
                continue;
            }
            if (ir.where && !matchesWhere(asset, *ir.where)) {
                continue;
            }
            rows.push_back(publicAsset(asset, projects_));
        }
    } else if (ir.entity == "playlist") {
        for (const Record &playlist : playlists_) {
            if (ir.in && !matchesIn(playlist, projects_, *ir.in, "project", "project")) {
                continue;
            }
            if (ir.where && !matchesWhere(playlist, *ir.where)) {
                continue;
            }
            Record row = publicPlaylist(playlist);
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
                    if (node.parent == "shot") {
                        return matchesIn(row, shots_, node, "shot", "shot");
                    }
                    if (node.parent == "sequence") {
                        return matchesIn(row, sequences_, node, "sequence", "sequence");
                    }
                    if (node.parent == "asset") {
                        return matchesIn(row, assets_, node, "asset", "asset");
                    }
                    if (node.parent == "playlist") {
                        const auto versionId = std::get<std::int64_t>(row.fields.at("id"));
                        const auto found = version_playlists_.find(versionId);
                        if (found == version_playlists_.end()) {
                            return node.op == ExprOp::Ne && node.key == "id";
                        }
                        if (node.key == "id" &&
                            (node.op == ExprOp::Eq || node.op == ExprOp::Ne)) {
                            const auto expected = parentId(playlists_, node, "playlist");
                            const bool member =
                                expected &&
                                std::find(
                                    found->second.begin(), found->second.end(), *expected) !=
                                    found->second.end();
                            return node.op == ExprOp::Eq ? member : !member;
                        }
                        for (const std::int64_t playlistId : found->second) {
                            const Record *playlist = parentById(playlists_, playlistId);
                            if (!playlist) {
                                continue;
                            }
                            ExprNode fieldOnly = node;
                            fieldOnly.parent.clear();
                            if (matchesWhere(*playlist, fieldOnly)) {
                                return true;
                            }
                        }
                        return false;
                    }
                    if (node.parent == "project") {
                        return matchesIn(row, projects_, node, "project", "project");
                    }
                    throw Error(
                        "fake backend: parent \"" + node.parent + "\" is not available");
                };
                if (!matchesScopedIn(version, *ir.in, matchVersionLeaf)) {
                    continue;
                }
            }
            if (ir.where && !matchesWhere(version, *ir.where)) {
                continue;
            }
            Record row =
                publicVersion(version, projects_, sequences_, shots_, assets_);
            const auto versionId = std::get<std::int64_t>(version.fields.at("id"));
            const auto membership = version_playlists_.find(versionId);
            if (membership != version_playlists_.end()) {
                std::vector<Link> playlists;
                for (const std::int64_t playlistId : membership->second) {
                    const Record *playlist = parentById(playlists_, playlistId);
                    if (!playlist) {
                        continue;
                    }
                    Link link;
                    link.id = playlistId;
                    link.name = std::get<std::string>(playlist->fields.at("name"));
                    playlists.push_back(std::move(link));
                }
                if (!playlists.empty()) {
                    row.fields["playlists"] = std::move(playlists);
                }
            }
            rows.push_back(std::move(row));
        }
    } else {
        throw Error("fake backend: entity \"" + ir.entity + "\" is not available");
    }

    return rows;
}

} // namespace pms
