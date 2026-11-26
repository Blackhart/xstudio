// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/dal/dal.cpp
 * @brief Unitary pms finds that return Media Details POCOs.
 */

#include "model/dal/dal.hpp"

#include "model/log.hpp"

#include "pms/pms.hpp"
#include "pms/shared_session.hpp"

#include <string>
#include <variant>
#include <vector>

namespace xstudio::mikros_media_details::dal {
namespace {

namespace trace = ::xstudio::mikros_media_details::log;

/**
 * @brief Reads one optional string field.
 * @param record Domain row.
 * @param key Field name.
 * @return The string, or empty when absent or not a string.
 */
std::string optionalString(const pms::Record &record, const std::string &key) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end() || !std::holds_alternative<std::string>(found->second)) {
        return {};
    }
    return std::get<std::string>(found->second);
}

/**
 * @brief Reads one optional integer field.
 * @param record Domain row.
 * @param key Field name.
 * @return The integer, or 0 when absent or not an int.
 */
std::int64_t optionalInt(const pms::Record &record, const std::string &key) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end() || !std::holds_alternative<std::int64_t>(found->second)) {
        return 0;
    }
    return std::get<std::int64_t>(found->second);
}

/**
 * @brief Reads one optional flag stored as an integer.
 * @param record Domain row.
 * @param key Field name.
 * @return True when the field is a non-zero integer.
 */
bool optionalFlag(const pms::Record &record, const std::string &key) {
    return optionalInt(record, key) != 0;
}

/**
 * @brief Copies playlist links from a version record.
 * @param record Domain version row.
 * @return Playlist id/name pairs in record order.
 */
std::vector<poco::PlaylistLink> playlistLinksFromRecord(const pms::Record &record) {
    std::vector<poco::PlaylistLink> playlists;
    const auto found = record.fields.find("playlists");
    if (found == record.fields.end()) {
        return playlists;
    }
    const auto *links = std::get_if<std::vector<pms::Link>>(&found->second);
    if (!links) {
        return playlists;
    }
    playlists.reserve(links->size());
    for (const pms::Link &link : *links) {
        playlists.push_back(poco::PlaylistLink{link.id, link.name});
    }
    return playlists;
}

/**
 * @brief Maps one pms record onto a VersionDetail POCO.
 * @param record Domain row from find.
 * @return Filled detail row.
 */
poco::VersionDetail mapVersion(const pms::Record &record) {
    poco::VersionDetail row;
    row.id = std::get<std::int64_t>(record.at("id"));
    row.name = std::get<std::string>(record.at("name"));
    row.author = optionalString(record, "author");
    row.created_at = optionalString(record, "created_at");
    row.frame_range = optionalString(record, "frame_range");
    row.frame_count = optionalInt(record, "frame_count");
    row.description = optionalString(record, "description");
    row.client_code = optionalString(record, "client_code");
    row.client_reviewed = optionalFlag(record, "client_reviewed");
    row.client_approved = optionalFlag(record, "client_approved");
    row.is_latest = optionalFlag(record, "is_latest");
    row.version_number = optionalInt(record, "version_number");
    row.task = optionalString(record, "task");
    row.step = optionalString(record, "step");
    row.status = optionalString(record, "status");
    row.artistic_status = optionalString(record, "artistic_status");
    row.sites = optionalString(record, "sites");
    row.open_notes_count = optionalInt(record, "open_notes_count");
    row.synced_to_client = optionalFlag(record, "synced_to_client");
    row.work_source_scene = optionalString(record, "work_source_scene");
    row.frames = optionalString(record, "frames");
    row.frames_provider = optionalString(record, "frames_provider");
    row.movie = optionalString(record, "movie");
    row.movie_provider = optionalString(record, "movie_provider");
    row.uploaded_movie = optionalString(record, "uploaded_movie");
    row.uploaded_movie_provider = optionalString(record, "uploaded_movie_provider");
    row.image = optionalString(record, "image");
    row.image_provider = optionalString(record, "image_provider");
    row.playlists = playlistLinksFromRecord(record);
    row.project_id = optionalInt(record, "project_id");
    row.project_name = optionalString(record, "project");
    row.entity_name = optionalString(record, "entity");
    row.entity_id = optionalInt(record, "entity_id");
    row.entity_type = optionalString(record, "entity_type");
    return row;
}

/**
 * @brief Maps one pms record onto a ShotDetail POCO.
 * @param record Domain row from find.
 * @return Filled detail row.
 */
poco::ShotDetail mapShot(const pms::Record &record) {
    poco::ShotDetail row;
    row.id = std::get<std::int64_t>(record.at("id"));
    row.name = std::get<std::string>(record.at("name"));
    row.description = optionalString(record, "description");
    row.status = optionalString(record, "status");
    row.cut_duration_in_sec = optionalInt(record, "cut_duration_in_sec");
    row.cut_in = optionalInt(record, "cut_in");
    row.cut_out = optionalInt(record, "cut_out");
    row.cut_order = optionalInt(record, "cut_order");
    row.sequence_id = optionalInt(record, "sequence_id");
    row.sequence_name = optionalString(record, "sequence");
    return row;
}

/**
 * @brief Maps one pms record onto a SequenceDetail POCO.
 * @param record Domain row from find.
 * @return Filled detail row.
 */
poco::SequenceDetail mapSequence(const pms::Record &record) {
    poco::SequenceDetail row;
    row.id = std::get<std::int64_t>(record.at("id"));
    row.name = std::get<std::string>(record.at("name"));
    row.description = optionalString(record, "description");
    row.status = optionalString(record, "status");
    row.cut_duration_in_sec = optionalInt(record, "cut_duration_in_sec");
    row.estimated_frames_count = optionalInt(record, "estimated_frames_count");
    row.estimated_shots_count = optionalInt(record, "estimated_shots_count");
    row.shot_count = optionalInt(record, "shot_count");
    row.sequence_order = optionalInt(record, "sequence_order");
    row.project_id = optionalInt(record, "project_id");
    row.project_name = optionalString(record, "project");
    return row;
}

/**
 * @brief Maps one pms record onto an AssetDetail POCO.
 * @param record Domain row from find.
 * @return Filled detail row.
 */
poco::AssetDetail mapAsset(const pms::Record &record) {
    poco::AssetDetail row;
    row.id = std::get<std::int64_t>(record.at("id"));
    row.name = std::get<std::string>(record.at("name"));
    row.description = optionalString(record, "description");
    row.status = optionalString(record, "status");
    row.project_id = optionalInt(record, "project_id");
    row.project_name = optionalString(record, "project");
    return row;
}

/**
 * @brief Maps one pms record onto a ProjectDetail POCO.
 * @param record Domain row from find.
 * @return Filled detail row.
 */
poco::ProjectDetail mapProject(const pms::Record &record) {
    poco::ProjectDetail row;
    row.id = std::get<std::int64_t>(record.at("id"));
    row.name = std::get<std::string>(record.at("name"));
    row.code = optionalString(record, "code");
    row.status = optionalString(record, "status");
    row.type = optionalString(record, "type");
    return row;
}

} // namespace

void initialize() {
    pms::sharedSession(); ///< Process-wide session; Shot Browser may already own it.
}

std::optional<poco::VersionDetail> findVersionDetailById(std::int64_t versionId) {
    if (versionId <= 0) {
        return std::nullopt;
    }
    try {
        using namespace pms::version;
        const pms::Page page = pms::Version()
                                   .Where(id == versionId)
                                   .Fields(std::vector<std::string>{
                                       "id",
                                       "name",
                                       "author",
                                       "created_at",
                                       "frame_range",
                                       "frame_count",
                                       "description",
                                       "client_code",
                                       "client_reviewed",
                                       "client_approved",
                                       "is_latest",
                                       "version_number",
                                       "task",
                                       "step",
                                       "status",
                                       "artistic_status",
                                       "sites",
                                       "open_notes_count",
                                       "synced_to_client",
                                       "work_source_scene",
                                       "frames",
                                       "movie",
                                       "uploaded_movie",
                                       "image",
                                       "playlists",
                                       "project",
                                       "entity"})
                                   .Limit(1)
                                   .find(pms::sharedSession());
        if (page.rows.empty()) {
            return std::nullopt;
        }
        return mapVersion(page.rows.front());
    } catch (const std::exception &error) {
        trace::failure(error);
        return std::nullopt;
    }
}

std::optional<poco::ShotDetail> findShotDetailById(std::int64_t shotId) {
    if (shotId <= 0) {
        return std::nullopt;
    }
    try {
        using namespace pms::shot;
        const pms::Page page = pms::Shot()
                                   .Where(id == shotId)
                                   .Fields(
                                       id,
                                       name,
                                       description,
                                       status,
                                       cutDurationInSec,
                                       cutIn,
                                       cutOut,
                                       cutOrder,
                                       sequence)
                                   .Limit(1)
                                   .find(pms::sharedSession());
        if (page.rows.empty()) {
            return std::nullopt;
        }
        return mapShot(page.rows.front());
    } catch (const std::exception &error) {
        trace::failure(error);
        return std::nullopt;
    }
}

std::optional<poco::SequenceDetail> findSequenceDetailById(std::int64_t sequenceId) {
    if (sequenceId <= 0) {
        return std::nullopt;
    }
    try {
        using namespace pms::sequence;
        const pms::Page page = pms::Sequence()
                                   .Where(id == sequenceId)
                                   .Fields(
                                       id,
                                       name,
                                       description,
                                       status,
                                       cutDurationInSec,
                                       estimatedFramesCount,
                                       estimatedShotsCount,
                                       shotCount,
                                       sequenceOrder,
                                       project)
                                   .Limit(1)
                                   .find(pms::sharedSession());
        if (page.rows.empty()) {
            return std::nullopt;
        }
        return mapSequence(page.rows.front());
    } catch (const std::exception &error) {
        trace::failure(error);
        return std::nullopt;
    }
}

std::optional<poco::AssetDetail> findAssetDetailById(std::int64_t assetId) {
    if (assetId <= 0) {
        return std::nullopt;
    }
    try {
        using namespace pms::asset;
        const pms::Page page = pms::Asset()
                                   .Where(id == assetId)
                                   .Fields(id, name, description, status, project)
                                   .Limit(1)
                                   .find(pms::sharedSession());
        if (page.rows.empty()) {
            return std::nullopt;
        }
        return mapAsset(page.rows.front());
    } catch (const std::exception &error) {
        trace::failure(error);
        return std::nullopt;
    }
}

std::optional<poco::ProjectDetail> findProjectDetailById(std::int64_t projectId) {
    if (projectId <= 0) {
        return std::nullopt;
    }
    try {
        using namespace pms::project;
        const pms::Page page = pms::Project()
                                   .Where(id == projectId)
                                   .Fields(id, name, code, status, type)
                                   .Limit(1)
                                   .find(pms::sharedSession());
        if (page.rows.empty()) {
            return std::nullopt;
        }
        return mapProject(page.rows.front());
    } catch (const std::exception &error) {
        trace::failure(error);
        return std::nullopt;
    }
}

} // namespace xstudio::mikros_media_details::dal
