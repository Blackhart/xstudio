// SPDX-License-Identifier: Apache-2.0
/**
 * @file dal.cpp
 * @brief pms find and count wrappers that return POCO pages or totals.
 */

#include "model/dal/dal.hpp"

#include "model/constants.hpp"
#include "model/context/shotbrowser_state.hpp"
#include "model/log.hpp"

#include "pms/fake/backend.hpp"
#include "pms/pms.hpp"
#include "pms/shotgrid/backend.hpp"

#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
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
 * @brief True when the process asked for the ShotGrid backend.
 * @return True when `MIKROS_PMS_BACKEND` is `shotgrid`.
 */
bool useShotGrid() { return constants::backend().value == "shotgrid"; }

/**
 * @brief Path to schema.yaml at runtime.
 *
 * Prefer `PMS_SCHEMA_PATH`, else `xstudio_root("/mikros_shotbrowser/schema.yaml")`
 * (installed via add_resource; launcher sets XSTUDIO_ROOT to share/xstudio).
 *
 * @return Absolute or override path of the domain schema.
 */
std::string readSchemaPath() { return constants::schemaPath().value; }

/**
 * @brief Escapes a name so it can sit inside a quoted log field.
 * @param text Raw domain name.
 * @return Text with quotes and backslashes escaped.
 */
std::string escapeQuoted(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

/**
 * @brief ShotGrid site values read from the environment.
 */
struct ShotGridEnv {
    std::string url;         ///< `SHOTGRID_URL`.
    std::string scriptName;  ///< `SHOTGRID_CLIENT_ID`.
    std::string scriptKey;   ///< `SHOTGRID_CLIENT_SECRET`.
    std::string mappingPath; ///< `SHOTGRID_MAPPING`.
};

/**
 * @brief Reads the four ShotGrid environment variables.
 * @return Site URL, script credentials, and mapping path.
 * @throw pms::Error When one of them is missing or empty.
 */
ShotGridEnv readShotGridEnv() {
    const constants::TextSetting url = constants::shotGridUrl();
    const constants::TextSetting script = constants::shotGridClientId();
    const constants::TextSetting key = constants::shotGridClientSecret();
    const constants::TextSetting mapping = constants::shotGridMapping();
    if (!url.set || !script.set || !key.set || !mapping.set) {
        throw pms::Error(
            "shotgrid DAL: set SHOTGRID_URL, SHOTGRID_CLIENT_ID, "
            "SHOTGRID_CLIENT_SECRET, SHOTGRID_MAPPING");
    }
    return ShotGridEnv{url.value, script.value, key.value, mapping.value};
}

/**
 * @brief Logs the site URL, script name, redacted key, and mapping path.
 * @param env Values from `readShotGridEnv`.
 */
void logShotGridSite(const ShotGridEnv &env) {
    trace::info(
        "shotgrid site url=\"" + escapeQuoted(env.url) + "\" script=\"" +
        escapeQuoted(env.scriptName) + "\" key=" + constants::keyHint(env.scriptKey) +
        " mapping=\"" + escapeQuoted(env.mappingPath) + "\"");
}

/**
 * @brief Builds a ShotGrid backend and attaches the DAL log sinks.
 * @param env Site values already checked.
 * @return Backend ready to wrap in the cache.
 */
pms::ShotGridBackend openShotGridBackend(const ShotGridEnv &env) {
    pms::ShotGridSite site;
    site.base_url = env.url;
    site.auth = pms::ShotGridScriptAuth{env.scriptName, env.scriptKey};
    pms::ShotGridBackend backend(std::move(site), env.mappingPath);
    backend.setSearchLog([](const std::string &payload) {
        trace::info("shotgrid payload " + payload);
    });
    backend.setAuthLog([](const std::string &line) { trace::info("shotgrid " + line); });
    return backend;
}

/**
 * @brief Drops a trailing newline so the IR block does not end blank.
 * @param text dump() output.
 * @return The same text without a final newline.
 */
std::string trimNewline(std::string text) {
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

/**
 * @brief Renders one record field value for the page log.
 * @param value String, int, date, or list of links.
 * @return Text that fits inside a `{ key: … }` line.
 */
std::string formatFieldValue(const pms::Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return "\"" + escapeQuoted(*text) + "\"";
    }
    if (const auto *number = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*number);
    }
    if (const auto *links = std::get_if<std::vector<pms::Link>>(&value)) {
        std::ostringstream out;
        out << "[ ";
        for (std::size_t i = 0; i < links->size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            const pms::Link &link = (*links)[i];
            out << "{ id: " << link.id << ", name: \"" << escapeQuoted(link.name) << "\" }";
        }
        out << " ]";
        return out.str();
    }
    const pms::Date &date = std::get<pms::Date>(value);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << date.year << '-' << std::setw(2) << date.month
        << '-' << std::setw(2) << date.day;
    return out.str();
}

/**
 * @brief Renders the domain page the caller receives.
 * @param source `cache` or `backend`.
 * @param entity Queried entity name.
 * @param page Rows and continuation cursor.
 * @return One `[mikros] page` body, with every field on each row.
 */
std::string formatPage(const char *source, const std::string &entity, const pms::Page &page) {
    std::ostringstream out;
    out << "page source=" << source << " entity=" << entity << " rows=" << page.rows.size()
        << " next_cursor=";
    if (page.next_cursor) {
        out << '"' << escapeQuoted(*page.next_cursor) << '"';
    } else {
        out << '-';
    }
    for (const pms::Record &row : page.rows) {
        out << "\n  { ";
        bool first = true;
        for (const auto &[key, value] : row.fields) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << key << ": " << formatFieldValue(value);
        }
        out << " }";
    }
    return out.str();
}

/**
 * @brief Cache trace hooks, lifetime, and byte budget.
 *
 * Defaults are five minutes and 64 MiB. `MIKROS_PMS_CACHE_TTL_SECONDS` and
 * `MIKROS_PMS_CACHE_BYTES` replace them. Eviction follows the byte budget only.
 *
 * @param backendName `shotgrid` or `fake`.
 * @return Options passed to `CachingBackend`.
 */
pms::CacheOptions buildCacheOptions(const std::string &backendName) {
    pms::CacheOptions options;
    options.ttl = std::chrono::seconds(constants::cacheTtlSeconds().value);
    options.max_bytes = static_cast<std::size_t>(constants::cacheMaxBytes().value);
    options.backend_name = backendName;
    trace::info(
        "cache ttl_seconds=" + std::to_string(options.ttl.count()) +
        " max_bytes=" + std::to_string(options.max_bytes));
    options.trace_ir = [](const pms::Ir &ir) {
        trace::info("request intermediate representation\n" + trimNewline(pms::dump(ir)));
    };
    options.trace_cache = [](const char *verdict, const std::string &entity) {
        trace::info(std::string("cache ") + verdict + " entity=" + entity);
    };
    options.trace_fallback = [](const std::string &name, const std::string &entity) {
        trace::info("fallback backend=" + name + " entity=" + entity);
    };
    options.trace_page = [](const char *source, const std::string &entity, const pms::Page &page) {
        trace::info(formatPage(source, entity, page));
    };
    return options;
}

/**
 * @brief Shared backend + CachingBackend + Session for the plugin process.
 *
 * Backend selected by `MIKROS_PMS_BACKEND` (`fake` default, or `shotgrid`).
 * CachingBackend serializes access. Trace lines are formatted by the DAL.
 */
class SharedPms {
  public:
    /**
     * @brief Returns the process-wide pms session holder.
     * @return The single shared instance.
     */
    static SharedPms &getInstance() {
        static SharedPms shared;
        return shared;
    }

    /**
     * @brief Returns the session used by every catalog find.
     * @return Session bound to the schema and the cache.
     */
    pms::Session &getSession() { return session_; }

  private:
    SharedPms()
        : fake_(buildFakeBackend()),
          shotgrid_(buildShotGridBackend()),
          cache_(getActiveBackend(), buildCacheOptions(shotgrid_ ? "shotgrid" : "fake")),
          session_(pms::Schema::load(readSchemaPath()), cache_) {}

    /**
     * @brief Builds the fake backend when ShotGrid is not selected.
     * @return A fake backend with no simulated latency, or empty in ShotGrid mode.
     */
    static std::optional<pms::FakeBackend> buildFakeBackend() {
        if (useShotGrid()) {
            return std::nullopt;
        }
        return pms::FakeBackend(/*simulate_latency=*/false);
    }

    /**
     * @brief Builds the ShotGrid backend when that mode is selected.
     * @return A connected-on-demand backend, or empty in fake mode.
     * @throw pms::Error When a ShotGrid environment variable is missing.
     */
    static std::optional<pms::ShotGridBackend> buildShotGridBackend() {
        if (!useShotGrid()) {
            return std::nullopt;
        }
        const ShotGridEnv env = readShotGridEnv(); ///< URL, script, key, and mapping.
        logShotGridSite(env);                      ///< Recognise the site without printing the key.
        return openShotGridBackend(env);
    }

    /**
     * @brief Returns the backend selected at construction.
     * @return ShotGrid when present, otherwise the fake backend.
     */
    pms::Backend &getActiveBackend() {
        if (shotgrid_) {
            return *shotgrid_;
        }
        return *fake_;
    }

    std::optional<pms::FakeBackend> fake_;       ///< Set when ShotGrid mode is off.
    std::optional<pms::ShotGridBackend> shotgrid_; ///< Set when ShotGrid mode is on.
    pms::CachingBackend cache_;                  ///< Serializes finds and traces them.
    pms::Session session_;                       ///< Schema plus the cache.
};

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
 * @param key Field name, such as `"has_notes"`.
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
        row.has_notes = optionalFlag(record, "has_notes");
        row.synced_to_client = optionalFlag(record, "synced_to_client");
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
    return limited.find(SharedPms::getInstance().getSession());
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
    shotbrowserContext(); ///< Create the context at plugin launch.
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
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    try {
        const pms::Page page = findLimitedPage(
            pms::Playlist().In(pms::scope::project == projectId), limit, cursor);
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
    std::optional<std::int64_t> playlistId) {
    using namespace pms::version;
    auto query = versionScopeQuery(projectId, sequenceId, shotId, assetId, playlistId)
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
                         hasNotes,
                         syncedToClient);
    return fetchPage<poco::VersionListElement>(std::move(query), limit, cursor);
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
                .find(SharedPms::getInstance().getSession());
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

poco::VersionMedia findVersionMedia(std::int64_t versionId) {
    try {
        using namespace pms::version;
        const pms::Page page =
            pms::Version()
                .Where(id == versionId)
                .Fields(id, name, frames, movie, uploadedMovie)
                .Limit(1)
                .find(SharedPms::getInstance().getSession());
        if (page.rows.empty()) {
            return {};
        }
        const pms::Record &record = page.rows.front();
        poco::VersionMedia row;
        row.id = std::get<std::int64_t>(record.at("id"));
        row.frames = optionalString(record, "frames");
        row.frames_provider = optionalString(record, "frames_provider");
        row.movie = optionalString(record, "movie");
        row.movie_provider = optionalString(record, "movie_provider");
        row.uploaded_movie = optionalString(record, "uploaded_movie");
        row.uploaded_movie_provider = optionalString(record, "uploaded_movie_provider");
        return row;
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
        const std::size_t total = std::move(query).count(SharedPms::getInstance().getSession());
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

int getNumberOfPlaylists(std::int64_t projectId) {
    return fetchCount(pms::Playlist().In(pms::scope::project == projectId));
}

int getNumberOfVersions(
    std::int64_t projectId,
    std::optional<std::int64_t> sequenceId,
    std::optional<std::int64_t> shotId,
    std::optional<std::int64_t> assetId,
    std::optional<std::int64_t> playlistId) {
    return fetchCount(versionScopeQuery(projectId, sequenceId, shotId, assetId, playlistId));
}

} // namespace xstudio::mikros_shotbrowser::dal
