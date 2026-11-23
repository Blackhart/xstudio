// SPDX-License-Identifier: Apache-2.0
/**
 * @file dal.cpp
 * @brief pms find and count wrappers that return POCO pages or totals.
 */

#include "model/dal/dal.hpp"

#include "model/constants.hpp"
#include "model/context/shotbrowser_state.hpp"
#include "model/log.hpp"

#include "pms/env.hpp"
#include "pms/pms.hpp"
#include "pms/shared_session.hpp"

#include "xstudio/utility/helpers.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace xstudio::mikros_shotbrowser::dal {
namespace {

namespace trace = ::xstudio::mikros_shotbrowser::log;

/**
 * @brief Context created by `initialize`.
 * @return The one shot browser context.
 */
::xstudio::mikros_shotbrowser::ShotbrowserState &shotbrowserContext() {
    static ::xstudio::mikros_shotbrowser::ShotbrowserState state;
    return state;
}

/**
 * @brief Reads one optional string field.
 * @param record Domain row.
 * @param key Field name, such as `"image"`.
 * @return The string, or empty when the field is absent or not a string.
 */
std::string optionalString(const pms::Record &record, const std::string &key);

/**
 * @brief Maps `id`/`name` records to a POCO vector.
 * @tparam Row POCO type with `id` and `name` members.
 * @param records Domain rows.
 * @return POCO rows in the same order.
 */
template <typename Row> std::vector<Row> mapRecordsToRows(const pms::Records &records) {
    std::vector<Row> rows;
    rows.reserve(records.size());
    for (const pms::Record &record : records) {
        rows.push_back(Row{
            std::get<std::int64_t>(record.at("id")),
            std::get<std::string>(record.at("name")),
        });
    }
    return rows;
}

/**
 * @brief Reads one optional string field.
 * @param record Domain row.
 * @param key Field name, such as `"image"`.
 * @return The string, or empty when the field is absent or not a string.
 */
std::string optionalString(const pms::Record &record, const std::string &key) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end() || !std::holds_alternative<std::string>(found->second)) {
        return {};
    }
    return std::get<std::string>(found->second);
}

/**
 * @brief Reads one optional flag stored as an integer.
 * @param record Domain row.
 * @param key Field name, such as `"synced_to_client"`.
 * @return True when the field is a non-zero integer.
 */
bool optionalFlag(const pms::Record &record, const std::string &key) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end() || !std::holds_alternative<std::int64_t>(found->second)) {
        return false;
    }
    return std::get<std::int64_t>(found->second) != 0;
}

/**
 * @brief Reads one optional integer field.
 * @param record Domain row.
 * @param key Field name, such as `"open_notes_count"`.
 * @return The value, or zero when the field is absent or not an integer.
 */
std::int64_t optionalInt64(const pms::Record &record, const std::string &key) {
    const auto found = record.fields.find(key);
    if (found == record.fields.end() || !std::holds_alternative<std::int64_t>(found->second)) {
        return 0;
    }
    return std::get<std::int64_t>(found->second);
}

/**
 * @brief Maps project records, including the filter fields.
 * @param records Domain rows.
 * @return Projects in the same order. Status and type are empty when absent.
 */
template <>
std::vector<poco::Project> mapRecordsToRows<poco::Project>(const pms::Records &records) {
    std::vector<poco::Project> rows;
    rows.reserve(records.size());
    for (const pms::Record &record : records) {
        poco::Project row;
        row.id = std::get<std::int64_t>(record.at("id"));
        row.name = std::get<std::string>(record.at("name"));
        row.status = optionalString(record, "status"); ///< ShotGrid sg_status_list.
        row.type = optionalString(record, "type");     ///< ShotGrid sg_type.
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief Maps version list records (no image or media).
 * @param records Domain rows.
 * @return List elements in the same order.
 */
template <>
std::vector<poco::VersionListElement> mapRecordsToRows<poco::VersionListElement>(
    const pms::Records &records) {
    std::vector<poco::VersionListElement> rows;
    rows.reserve(records.size());
    for (const pms::Record &record : records) {
        poco::VersionListElement row;
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
        row.open_notes_count = optionalInt64(record, "open_notes_count");
        row.synced_to_client = optionalFlag(record, "synced_to_client");
        row.frames = optionalString(record, "frames");
        row.frames_provider = optionalString(record, "frames_provider");
        row.movie = optionalString(record, "movie");
        row.movie_provider = optionalString(record, "movie_provider");
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief Builds the strong/weak parent scope for a version find.
 */
decltype(pms::Version()) versionScopeQuery(
    std::int64_t projectId,
    std::optional<std::int64_t> sequenceId,
    std::optional<std::int64_t> shotId,
    std::optional<std::int64_t> assetId,
    std::optional<std::int64_t> playlistId) {
    auto query = pms::Version();
    if (shotId) {
        query = query.In(pms::scope::shot == *shotId);
    } else if (sequenceId) {
        query = query.In(pms::scope::sequence == *sequenceId);
    } else if (assetId) {
        query = query.In(pms::scope::asset == *assetId);
    } else {
        query = query.In(pms::scope::project == projectId);
    }
    if (playlistId) {
        query = query.In(pms::scope::playlist == *playlistId);
    }
    return query;
}

/**
 * @brief Trims leading and trailing ASCII whitespace.
 * @param text Raw search box value.
 * @return Trimmed copy.
 */
std::string trimAscii(std::string text) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

/**
 * @brief True when every character is an ASCII digit.
 * @param text Non-empty trimmed text.
 * @return True when the text is an integer id candidate.
 */
bool isAllDigits(const std::string &text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char ch) {
               return std::isdigit(ch) != 0;
           });
}

/**
 * @brief Adds a name-or-id Where when the search box is not empty.
 * @tparam QueryT Fluent query type.
 * @tparam IdField `Field<Entity, int64_t>` for `id`.
 * @tparam NameField `Field<Entity, string>` for `name`.
 * @param query Query with In already applied.
 * @param idField Entity id proxy.
 * @param nameField Entity name proxy.
 * @param nameOrId Search text, or empty.
 * @return Query with an optional Where on id or name contains.
 */
template <typename QueryT, typename IdField, typename NameField>
QueryT applyNameOrId(
    QueryT query,
    const IdField &idField,
    const NameField &nameField,
    const std::optional<std::string> &nameOrId) {
    if (!nameOrId) {
        return query;
    }
    const std::string text = trimAscii(*nameOrId);
    if (text.empty()) {
        return query;
    }
    if (isAllDigits(text)) {
        try {
            return query.Where(idField == static_cast<std::int64_t>(std::stoll(text)));
        } catch (const std::exception &) {
            return query.Where(pms::contains(nameField, text)); ///< Too large for an id: treat as name.
        }
    }
    return query.Where(pms::contains(nameField, text));
}

/**
 * @brief Turns one pms page into a POCO page.
 * @tparam Row POCO type with `id` and `name`.
 * @param page Domain page from `find`.
 * @return Rows and the same continuation cursor.
 */
template <typename Row> RowPage<Row> mapPage(const pms::Page &page) {
    return RowPage<Row>{mapRecordsToRows<Row>(page.rows), page.next_cursor};
}

/**
 * @brief Runs one limited find against the shared session.
 * @tparam QueryT Fluent query type.
 * @param query Built query (In/Where already applied).
 * @param limit Maximum rows in the page.
 * @param cursor Continuation cursor, or empty for the first page.
 * @return Domain page.
 */
template <typename QueryT>
pms::Page findLimitedPage(
    QueryT query, std::size_t limit, const std::optional<std::string> &cursor) {
    auto limited = std::move(query).Limit(limit);
    if (cursor && !cursor->empty()) {
        limited = limited.After(*cursor);
    }
    return limited.find(pms::sharedSession());
}

/**
 * @brief Runs one limited find and maps the page to POCOs, or returns {} on error.
 * @tparam Row POCO type with `id` and `name`.
 * @tparam QueryT Fluent query type.
 * @param query Built query (In/Where already applied).
 * @param limit Maximum rows in the page.
 * @param cursor Continuation cursor, or empty for the first page.
 * @return POCO page, or empty when the lookup fails.
 */
template <typename Row, typename QueryT>
RowPage<Row> fetchPage(
    QueryT query, std::size_t limit, const std::optional<std::string> &cursor) {
    try {
        const pms::Page page = findLimitedPage(std::move(query), limit, cursor);
        return mapPage<Row>(page);
    } catch (const std::exception &error) {
        trace::failure(error);
        return {};
    }
}

/**
 * @brief Copies the shot links stored on one sequence record.
 * @param record Sequence row from find().
 * @return Shots in record order. Empty when the field is absent.
 */
std::vector<poco::Shot> retrieveShotListFromSequence(const pms::Record &record) {
    std::vector<poco::Shot> shots;
    const auto found = record.fields.find("shots");
    if (found == record.fields.end()) {
        return shots;
    }
    const auto *links = std::get_if<std::vector<pms::Link>>(&found->second);
    if (!links) {
        return shots;
    }
    shots.reserve(links->size());
    for (const pms::Link &link : *links) {
        shots.push_back(poco::Shot{link.id, link.name});
    }
    return shots;
}

/**
 * @brief Counts the version links stored on one playlist record.
 * @param record Playlist row from find().
 * @return Size of the `versions` link list, or zero when the field is absent.
 */
std::size_t versionCountFromPlaylist(const pms::Record &record) {
    const auto found = record.fields.find("versions");
    if (found == record.fields.end()) {
        return 0;
    }
    const auto *links = std::get_if<std::vector<pms::Link>>(&found->second);
    if (!links) {
        return 0;
    }
    return links->size();
}

} // namespace

void initialize() {
    pms::setSharedInfoLog([](std::string_view message) {
        ::xstudio::mikros_shotbrowser::log::info(message);
    });
    // Host resolves the installed schema; pms only reads PMS_SCHEMA_PATH.
    if (!pms::schemaPath().set) {
        const std::string path = utility::xstudio_root("/mikros_pms/schema.yaml");
        ::setenv(pms::kSchemaPath, path.c_str(), /*overwrite=*/0);
    }
    shotbrowserContext(); ///< Create the context at plugin launch.
}

/**
 * @brief Reads the `show` list of one project field from the shared schema.
 * @param fieldKey Domain field name, such as `"status"`.
 * @return The values in schema order, or empty when the entity or field is missing.
 */
std::vector<std::string> projectFieldShowList(const std::string &fieldKey) {
    const pms::EntitySpec *project = pms::sharedSession().schema().entity("project");
    if (!project) {
        return {};
    }
    const pms::FieldSpec *field = project->field(fieldKey);
    if (!field) {
        return {};
    }
    return field->show; ///< Empty means the UI starts with no check on this field.
}

std::vector<std::string> defaultShownProjectStatuses() {
    return projectFieldShowList("status");
}

std::vector<std::string> defaultShownProjectTypes() {
    return projectFieldShowList("type");
}

std::int64_t selectedProject() {
    return shotbrowserContext().selectedProject();
}

void setSelectedProject(std::int64_t projectId) {
    shotbrowserContext().setSelectedProject(projectId);
}

std::int64_t selectedSequence() {
    return shotbrowserContext().selectedSequence();
}

void setSelectedSequence(std::int64_t sequenceId) {
    shotbrowserContext().setSelectedSequence(sequenceId);
}

std::int64_t selectedShot() {
    return shotbrowserContext().selectedShot();
}

void setSelectedShot(std::int64_t shotId) {
    shotbrowserContext().setSelectedShot(shotId);
}

std::int64_t selectedAsset() {
    return shotbrowserContext().selectedAsset();
}

void setSelectedAsset(std::int64_t assetId) {
    shotbrowserContext().setSelectedAsset(assetId);
}

BrowserMode browserMode() {
    return shotbrowserContext().browserMode();
}

void setBrowserMode(BrowserMode mode) {
    shotbrowserContext().setBrowserMode(mode);
}

ResultView resultView() {
    return shotbrowserContext().resultView();
}

void setResultView(ResultView view) {
    shotbrowserContext().setResultView(view);
}

std::size_t readPageLimit() {
    return static_cast<std::size_t>(constants::pageSize().value);
}

RowPage<poco::Project> findProjects(std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Project>(pms::Project(), limit, cursor);
}

RowPage<poco::SequenceShots> findSequenceShots(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    try {
        const pms::Page page = findLimitedPage(
            pms::Sequence().In(pms::scope::project == projectId), limit, cursor);
        RowPage<poco::SequenceShots> out;
        out.next_cursor = page.next_cursor;
        out.rows.reserve(page.rows.size());
        for (const pms::Record &record : page.rows) {
            poco::SequenceShots node;
            node.sequence.id = std::get<std::int64_t>(record.at("id"));
            node.sequence.name = std::get<std::string>(record.at("name"));
            node.shots = retrieveShotListFromSequence(record); ///< Links already read with the sequence.
            out.rows.push_back(std::move(node));
        }
        return out;
    } catch (const std::exception &error) {
        trace::failure(error);
        return {};
    }
}

RowPage<poco::Asset> findAssets(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Asset>(
        pms::Asset().In(pms::scope::project == projectId), limit, cursor);
}

RowPage<poco::Playlist> findPlaylists(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor,
    std::optional<std::string> nameOrId) {
    try {
        auto query = applyNameOrId(
            pms::Playlist().In(pms::scope::project == projectId),
            pms::playlist::id,
            pms::playlist::name,
            nameOrId);
        const pms::Page page = findLimitedPage(std::move(query), limit, cursor);
        RowPage<poco::Playlist> out;
        out.next_cursor = page.next_cursor;
        out.rows.reserve(page.rows.size());
        for (const pms::Record &record : page.rows) {
            poco::Playlist row;
            row.id = std::get<std::int64_t>(record.at("id"));
            row.name = std::get<std::string>(record.at("name"));
            row.version_count = versionCountFromPlaylist(record);
            row.open_notes_count = optionalInt64(record, "open_notes_count");
            row.departments = optionalString(record, "departments");
            row.created_at = optionalString(record, "created_at");
            row.category = optionalString(record, "category");
            row.author = optionalString(record, "author");
            out.rows.push_back(std::move(row));
        }
        return out;
    } catch (const std::exception &error) {
        trace::failure(error);
        return {};
    }
}

RowPage<poco::VersionListElement> findVersionListElements(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor,
    std::optional<std::int64_t> sequenceId,
    std::optional<std::int64_t> shotId,
    std::optional<std::int64_t> assetId,
    std::optional<std::int64_t> playlistId,
    std::optional<std::string> nameOrId) {
    using namespace pms::version;
    auto query = applyNameOrId(
                     versionScopeQuery(projectId, sequenceId, shotId, assetId, playlistId),
                     id,
                     name,
                     nameOrId)
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
                         movie);
    return fetchPage<poco::VersionListElement>(std::move(query), limit, cursor);
}

std::vector<poco::PlaylistVersionLink> findPlaylistVersionLinks(std::int64_t playlistId) {
    try {
        using namespace pms::playlist;
        const pms::Page page = pms::Playlist()
                                   .Where(id == playlistId)
                                   .Limit(1)
                                   .find(pms::sharedSession());
        if (page.rows.empty()) {
            return {};
        }
        const pms::Record &record = page.rows.front();
        const auto found = record.fields.find("versions");
        if (found == record.fields.end()) {
            return {};
        }
        const auto *links = std::get_if<std::vector<pms::Link>>(&found->second);
        if (!links) {
            return {};
        }
        std::vector<poco::PlaylistVersionLink> rows;
        rows.reserve(links->size());
        for (const pms::Link &link : *links) {
            rows.push_back(poco::PlaylistVersionLink{link.id, link.name});
        }
        return rows;
    } catch (const std::exception &error) {
        trace::failure(error);
        return {};
    }
}

std::vector<poco::VersionListElement>
findVersionListElementsByIds(const std::vector<std::int64_t> &versionIds) {
    if (versionIds.empty()) {
        return {};
    }
    try {
        using namespace pms::version;
        auto idFilter = (id == versionIds.front());
        for (std::size_t index = 1; index < versionIds.size(); ++index) {
            idFilter = idFilter || (id == versionIds[index]);
        }
        const pms::Page page = pms::Version()
                                   .Where(std::move(idFilter))
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
                                       movie)
                                   .Limit(versionIds.size())
                                   .find(pms::sharedSession());
        const std::vector<poco::VersionListElement> found =
            mapRecordsToRows<poco::VersionListElement>(page.rows);
        std::map<std::int64_t, poco::VersionListElement> byId;
        for (const poco::VersionListElement &row : found) {
            byId.emplace(row.id, row);
        }
        std::vector<poco::VersionListElement> ordered;
        ordered.reserve(versionIds.size());
        for (const std::int64_t versionId : versionIds) {
            const auto it = byId.find(versionId);
            if (it != byId.end()) {
                ordered.push_back(it->second);
            }
        }
        return ordered;
    } catch (const std::exception &error) {
        trace::failure(error);
        return {};
    }
}

std::vector<poco::VersionThumb> findVersionThumbs(const std::vector<std::int64_t> &versionIds) {
    if (versionIds.empty()) {
        return {};
    }
    try {
        using namespace pms::version;
        auto idFilter = (id == versionIds.front()); ///< First id; later ids OR into this tree.
        for (std::size_t index = 1; index < versionIds.size(); ++index) {
            idFilter = idFilter || (id == versionIds[index]);
        }
        const pms::Page page =
            pms::Version()
                .Where(std::move(idFilter))
                .Fields(id, name, image)
                .Limit(versionIds.size())
                .find(pms::sharedSession());
        std::vector<poco::VersionThumb> rows;
        rows.reserve(page.rows.size());
        for (const pms::Record &record : page.rows) {
            poco::VersionThumb row;
            row.id = std::get<std::int64_t>(record.at("id"));
            row.image = optionalString(record, "image");
            row.image_provider = optionalString(record, "image_provider");
            rows.push_back(std::move(row));
        }
        return rows;
    } catch (const std::exception &error) {
        trace::failure(error);
        return {};
    }
}

poco::VersionThumb findVersionThumb(std::int64_t versionId) {
    const std::vector<poco::VersionThumb> rows = findVersionThumbs({versionId});
    if (rows.empty()) {
        return {};
    }
    return rows.front();
}

std::vector<poco::VersionTranscodedMedia>
findVersionTranscodedMovies(const std::vector<std::int64_t> &versionIds) {
    if (versionIds.empty()) {
        return {};
    }
    try {
        using namespace pms::version;
        auto idFilter = (id == versionIds.front());
        for (std::size_t index = 1; index < versionIds.size(); ++index) {
            idFilter = idFilter || (id == versionIds[index]);
        }
        const pms::Page page = pms::Version()
                                   .Where(std::move(idFilter))
                                   .Fields(id, uploadedMovie)
                                   .Limit(versionIds.size())
                                   .find(pms::sharedSession());
        std::vector<poco::VersionTranscodedMedia> rows;
        rows.reserve(page.rows.size());
        for (const pms::Record &record : page.rows) {
            poco::VersionTranscodedMedia row;
            row.id = std::get<std::int64_t>(record.at("id"));
            row.uploaded_movie = optionalString(record, "uploaded_movie");
            row.uploaded_movie_provider = optionalString(record, "uploaded_movie_provider");
            rows.push_back(std::move(row));
        }
        return rows;
    } catch (const std::exception &error) {
        trace::failure(error);
        return {};
    }
}

/**
 * @brief Runs one count against the shared session, or returns 0 on error.
 * @tparam QueryT Fluent query type.
 * @param query Built query (In/Where already applied).
 * @return Match count, or 0 when the lookup fails.
 */
template <typename QueryT> int fetchCount(QueryT query) {
    try {
        const std::size_t total = std::move(query).count(pms::sharedSession());
        return static_cast<int>(total);
    } catch (const std::exception &error) {
        trace::failure(error);
        return 0;
    }
}

int getNumberOfSequences(std::int64_t projectId) {
    return fetchCount(pms::Sequence().In(pms::scope::project == projectId));
}

int getNumberOfShots(std::int64_t projectId, std::optional<std::int64_t> sequenceId) {
    if (sequenceId) {
        return fetchCount(pms::Shot().In(pms::scope::sequence == *sequenceId));
    }
    return fetchCount(pms::Shot().In(pms::scope::project == projectId));
}

int getNumberOfAssets(std::int64_t projectId) {
    return fetchCount(pms::Asset().In(pms::scope::project == projectId));
}

int getNumberOfPlaylists(std::int64_t projectId, std::optional<std::string> nameOrId) {
    return fetchCount(applyNameOrId(
        pms::Playlist().In(pms::scope::project == projectId),
        pms::playlist::id,
        pms::playlist::name,
        nameOrId));
}

int getNumberOfVersions(
    std::int64_t projectId,
    std::optional<std::int64_t> sequenceId,
    std::optional<std::int64_t> shotId,
    std::optional<std::int64_t> assetId,
    std::optional<std::int64_t> playlistId,
    std::optional<std::string> nameOrId) {
    return fetchCount(applyNameOrId(
        versionScopeQuery(projectId, sequenceId, shotId, assetId, playlistId),
        pms::version::id,
        pms::version::name,
        nameOrId));
}

} // namespace xstudio::mikros_shotbrowser::dal
