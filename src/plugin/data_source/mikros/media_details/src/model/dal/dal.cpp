// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/dal/dal.cpp
 * @brief Unitary pms finds that return Media Details POCOs.
 */

#include "model/dal/dal.hpp"

#include "model/log.hpp"

#include "pms/pms.hpp"
#include "pms/shared_session.hpp"

#include <algorithm>
#include <map>
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
    if (found == record.fields.end()) {
        return 0;
    }
    if (const auto *number = std::get_if<std::int64_t>(&found->second)) {
        return *number;
    }
    if (const auto *fraction = std::get_if<double>(&found->second)) {
        return static_cast<std::int64_t>(*fraction); ///< A float duration shown as whole seconds.
    }
    return 0;
}

/**
 * @brief Reads one optional flag stored as an integer.
 * @param record Domain row.
 * @param key Field name.
 * @return True when the field is a non-zero integer.
 */
bool optionalFlag(const pms::Record &record, const std::string &key) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end()) {
        return false;
    }
    if (const auto *flag = std::get_if<bool>(&found->second)) {
        return *flag;
    }
    return optionalInt(record, key) != 0; ///< Older rows stored a checkbox as 0 or 1.
}

/**
 * @brief Reads one entity object stored on a record.
 * @param record Domain row.
 * @param key Field name.
 * @return The link, or null when the field is absent or not a link.
 */
const pms::Link *optionalLink(const pms::Record &record, const std::string &key) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end()) {
        return nullptr;
    }
    return std::get_if<pms::Link>(&found->second);
}

/**
 * @brief Reads the name of one entity field.
 * @param record Domain row.
 * @param key Field name.
 * @return The link name, or empty when the field is absent.
 */
std::string linkName(const pms::Record &record, const std::string &key) {
    const pms::Link *link = optionalLink(record, key);
    return link ? link->name : std::string{};
}

/**
 * @brief Reads the id of one entity field.
 * @param record Domain row.
 * @param key Field name.
 * @return The link id, or 0 when the field is absent.
 */
std::int64_t linkId(const pms::Record &record, const std::string &key) {
    const pms::Link *link = optionalLink(record, key);
    return link ? link->id : 0;
}

/**
 * @brief Reads the ShotGrid type of one entity field.
 * @param record Domain row.
 * @param key Field name.
 * @return The link type, or empty when the field is absent.
 */
std::string linkType(const pms::Record &record, const std::string &key) {
    const pms::Link *link = optionalLink(record, key);
    return link ? link->type : std::string{};
}

/**
 * @brief Joins the names of a multi-entity list.
 * @param record Domain row.
 * @param key Field name.
 * @return Names separated by commas. Empty when the field is absent.
 */
std::string joinLinkNames(const pms::Record &record, const std::string &key) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end()) {
        return {};
    }
    const auto *links = std::get_if<std::vector<pms::Link>>(&found->second);
    if (!links) {
        return {};
    }
    std::string joined;
    for (const pms::Link &link : *links) {
        if (link.name.empty()) {
            continue;
        }
        if (!joined.empty()) {
            joined += ',';
        }
        joined += link.name; ///< The card shows labels, not ids.
    }
    return joined;
}

/**
 * @brief Copies playlist links from a version record.
 * @param record Domain version row.
 * @return Playlist id, name, and type in record order.
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
        playlists.push_back(poco::PlaylistLink{link.id, link.name, link.type});
    }
    return playlists;
}

/**
 * @brief Copies attachment links, including the thumbnail the schema link omits.
 * @param record Domain note row.
 * @return Attachment id, name, and image in record order.
 */
std::vector<poco::NoteAttachment> attachmentsFromRecord(const pms::Record &record) {
    std::vector<poco::NoteAttachment> attachments;
    const auto found = record.fields.find("attachments");
    if (found == record.fields.end()) {
        return attachments;
    }
    const auto *links = std::get_if<std::vector<pms::Link>>(&found->second);
    if (!links) {
        return attachments;
    }
    attachments.reserve(links->size());
    for (const pms::Link &link : *links) {
        attachments.push_back(poco::NoteAttachment{link.id, link.name, link.image});
    }
    return attachments;
}

/**
 * @brief Maps one pms record onto a NoteDetail POCO.
 * @param record Domain row from find.
 * @return Filled note row.
 */
poco::NoteDetail mapNote(const pms::Record &record) {
    poco::NoteDetail row;
    row.id = std::get<std::int64_t>(record.at("id"));
    row.name = std::get<std::string>(record.at("name"));
    row.content = optionalString(record, "content");
    row.author = linkName(record, "author");
    row.created_at = optionalString(record, "created_at");
    row.status = optionalString(record, "status");
    row.to = joinLinkNames(record, "to");
    row.department = linkName(record, "department");
    row.attachments = attachmentsFromRecord(record);
    return row;
}

/**
 * @brief Status field of the note entity, when the schema declares it.
 * @return The field, or null when note or status is missing.
 */
const pms::FieldSpec *noteStatusField() {
    const pms::EntitySpec *note = pms::sharedSession().schema().entity("note");
    if (!note) {
        return nullptr;
    }
    return note->field("status");
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
    row.author = linkName(record, "author");
    row.created_at = optionalString(record, "created_at");
    row.frame_range = optionalString(record, "frame_range");
    row.frame_count = optionalInt(record, "frame_count");
    row.description = optionalString(record, "description");
    row.client_code = optionalString(record, "client_code");
    row.client_reviewed = optionalFlag(record, "client_reviewed");
    row.client_approved = optionalFlag(record, "client_approved");
    row.is_latest = optionalFlag(record, "is_latest");
    row.version_number = optionalInt(record, "version_number");
    row.task = linkName(record, "task");
    row.step = linkName(record, "step");
    row.status = optionalString(record, "status");
    row.artistic_status = optionalString(record, "artistic_status");
    row.sites = joinLinkNames(record, "sites");
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
    row.project_id = linkId(record, "project");
    row.project_name = linkName(record, "project");
    row.entity_name = linkName(record, "entity");
    row.entity_id = linkId(record, "entity");
    row.entity_type = linkType(record, "entity");
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
    row.sequence_id = linkId(record, "sequence");
    row.sequence_name = linkName(record, "sequence");
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
    row.project_id = linkId(record, "project");
    row.project_name = linkName(record, "project");
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
    row.project_id = linkId(record, "project");
    row.project_name = linkName(record, "project");
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

std::vector<poco::NoteDetail> findNotesByVersionId(std::int64_t versionId) {
    if (versionId <= 0) {
        return {};
    }
    try {
        const pms::Page page = pms::Note()
                                   .In(pms::version::id == versionId)
                                   .Fields(std::vector<std::string>{
                                       "content",
                                       "author",
                                       "created_at",
                                       "status",
                                       "to",
                                       "department",
                                       "attachments"})
                                   .find(pms::sharedSession());
        std::vector<poco::NoteDetail> notes;
        notes.reserve(page.rows.size());
        for (const pms::Record &record : page.rows) {
            notes.push_back(mapNote(record));
        }
        std::sort(
            notes.begin(),
            notes.end(),
            [](const poco::NoteDetail &left, const poco::NoteDetail &right) {
                if (left.created_at != right.created_at) {
                    return left.created_at > right.created_at; ///< Newest first. ISO strings sort.
                }
                return left.id > right.id;
            });
        return notes;
    } catch (const std::exception &error) {
        trace::failure(error); ///< The tab stays ready with an empty list.
        return {};
    }
}

StatusStyle statusStyle(const std::string &entityName, const std::string &fieldKey) {
    StatusStyle style;
    const pms::EntitySpec *entity = pms::sharedSession().schema().entity(entityName);
    if (!entity) {
        return style; ///< Unknown entity: the screen keeps an empty map.
    }
    const pms::FieldSpec *field = entity->field(fieldKey);
    if (!field) {
        return style; ///< Unknown field: same empty style.
    }
    style.colors = field->colors;
    style.labels = field->labels;
    style.defaultColor = field->defaultColor; ///< Colour for a code the map does not name.
    return style;
}

std::map<std::string, std::string> noteStatusColors() {
    const pms::FieldSpec *status = noteStatusField();
    if (!status) {
        return {};
    }
    return status->colors;
}

std::map<std::string, std::string> noteStatusLabels() {
    const pms::FieldSpec *status = noteStatusField();
    if (!status) {
        return {};
    }
    return status->labels;
}

std::string noteStatusDefaultColor() {
    return statusStyle("note", "status").defaultColor;
}

} // namespace xstudio::mikros_media_details::dal
