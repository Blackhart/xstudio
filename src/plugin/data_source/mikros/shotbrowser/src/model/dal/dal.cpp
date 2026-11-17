// SPDX-License-Identifier: Apache-2.0
/**
 * @file dal.cpp
 * @brief pms find wrappers that return one page of POCO rows.
 */

#include "model/dal/dal.hpp"

#include "model/constants.hpp"
#include "model/log.hpp"

#include "pms/fake/backend.hpp"
#include "pms/pms.hpp"
#include "pms/shotgrid/backend.hpp"

#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <variant>

namespace xstudio::mikros_shotbrowser::dal {
namespace {

namespace trace = ::xstudio::mikros_shotbrowser::log;

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
 * @brief Renders the domain page the caller receives.
 * @param source `cache` or `backend`.
 * @param entity Queried entity name.
 * @param page Rows and continuation cursor.
 * @return One `[mikros] page` body, including the id/name lines.
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
        out << "\n  { id: " << std::get<std::int64_t>(row.at("id")) << ", name: \""
            << escapeQuoted(std::get<std::string>(row.at("name"))) << "\" }";
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
     * @return A latency-simulating backend, or empty in ShotGrid mode.
     */
    static std::optional<pms::FakeBackend> buildFakeBackend() {
        if (useShotGrid()) {
            return std::nullopt;
        }
        return pms::FakeBackend(/*simulate_latency=*/true);
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
 * @brief Maps version records, including the image source and its provider.
 * @param records Domain rows.
 * @return Version rows in the same order. Image fields stay empty when absent.
 */
template <>
std::vector<poco::Version> mapRecordsToRows<poco::Version>(const pms::Records &records) {
    std::vector<poco::Version> rows;
    rows.reserve(records.size());
    for (const pms::Record &record : records) {
        poco::Version row;
        row.id = std::get<std::int64_t>(record.at("id"));
        row.name = std::get<std::string>(record.at("name"));
        row.image = optionalString(record, "image");
        row.image_provider = optionalString(record, "image_provider");
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
        row.frames = optionalString(record, "frames");
        row.frames_provider = optionalString(record, "frames_provider");
        row.movie = optionalString(record, "movie");
        row.movie_provider = optionalString(record, "movie_provider");
        row.uploaded_movie = optionalString(record, "uploaded_movie");
        row.uploaded_movie_provider = optionalString(record, "uploaded_movie_provider");
        rows.push_back(std::move(row));
    }
    return rows;
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

} // namespace

std::size_t readPageLimit() {
    return static_cast<std::size_t>(constants::pageSize().value);
}

RowPage<poco::Project> findProjects(std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Project>(pms::Project(), limit, cursor);
}

RowPage<poco::Sequence> findSequences(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Sequence>(
        pms::Sequence().In(pms::scope::project == projectId), limit, cursor);
}

RowPage<poco::Shot> findShots(
    std::int64_t sequenceId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Shot>(
        pms::Shot().In(pms::scope::sequence == sequenceId), limit, cursor);
}

RowPage<poco::Asset> findAssets(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Asset>(
        pms::Asset().In(pms::scope::project == projectId), limit, cursor);
}

RowPage<poco::Playlist> findPlaylists(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Playlist>(
        pms::Playlist().In(pms::scope::project == projectId), limit, cursor);
}

RowPage<poco::Version> findVersions(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor,
    std::optional<std::int64_t> sequenceId,
    std::optional<std::int64_t> shotId,
    std::optional<std::int64_t> assetId,
    std::optional<std::int64_t> playlistId) {
    auto query = pms::Version();
    if (shotId) {
        query = query.In(pms::scope::shot == *shotId); ///< Narrowest strong parent.
    } else if (sequenceId) {
        query = query.In(pms::scope::sequence == *sequenceId); ///< Sequence when no shot is set.
    } else if (assetId) {
        query = query.In(pms::scope::asset == *assetId); ///< Asset excludes the sequence branch.
    } else {
        query = query.In(pms::scope::project == projectId); ///< Whole project.
    }
    if (playlistId) {
        query = query.In(pms::scope::playlist == *playlistId); ///< Weak parent, combined with AND.
    }
    return fetchPage<poco::Version>(std::move(query), limit, cursor);
}

} // namespace xstudio::mikros_shotbrowser::dal
