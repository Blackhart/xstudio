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
    row.description = optionalString(record, "description");
    row.step = optionalString(record, "step");
    row.status = optionalString(record, "status");
    row.artistic_status = optionalString(record, "artistic_status");
    row.sites = optionalString(record, "sites");
    row.open_notes_count = optionalInt(record, "open_notes_count");
    row.synced_to_client = optionalFlag(record, "synced_to_client");
    row.frames = optionalString(record, "frames");
    row.frames_provider = optionalString(record, "frames_provider");
    row.movie = optionalString(record, "movie");
    row.movie_provider = optionalString(record, "movie_provider");
    row.uploaded_movie = optionalString(record, "uploaded_movie");
    row.uploaded_movie_provider = optionalString(record, "uploaded_movie_provider");
    row.image = optionalString(record, "image");
    row.image_provider = optionalString(record, "image_provider");
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
                                   .Fields(
                                       id,
                                       name,
                                       author,
                                       createdAt,
                                       frameRange,
                                       description,
                                       step,
                                       status,
                                       artisticStatus,
                                       sites,
                                       openNotesCount,
                                       syncedToClient,
                                       frames,
                                       movie,
                                       uploadedMovie,
                                       image,
                                       project,
                                       entity)
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
        const pms::Page page =
            pms::Shot().Where(id == shotId).Fields(id, name, sequence).Limit(1).find(
                pms::sharedSession());
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
        const pms::Page page =
            pms::Sequence().Where(id == sequenceId).Fields(id, name, project).Limit(1).find(
                pms::sharedSession());
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
        const pms::Page page =
            pms::Asset().Where(id == assetId).Fields(id, name, project).Limit(1).find(
                pms::sharedSession());
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
