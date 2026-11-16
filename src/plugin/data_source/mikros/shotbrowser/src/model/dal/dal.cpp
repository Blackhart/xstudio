// SPDX-License-Identifier: Apache-2.0
/**
 * @file dal.cpp
 * @brief pms find wrappers that return one page of POCO rows.
 */

#include "model/dal/dal.hpp"

#include "model/log.hpp"

#include "pms/fake/backend.hpp"
#include "pms/pms.hpp"
#include "pms/shotgrid/backend.hpp"

#include "xstudio/utility/helpers.hpp"

#include <cstdlib>
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
bool useShotGrid() {
    const char *mode = std::getenv("MIKROS_PMS_BACKEND");
    return mode && std::string(mode) == "shotgrid";
}

/**
 * @brief Path to schema.yaml at runtime.
 *
 * Prefer `PMS_SCHEMA_PATH`, else `xstudio_root("/mikros_shotbrowser/schema.yaml")`
 * (installed via add_resource; launcher sets XSTUDIO_ROOT to share/xstudio).
 *
 * @return Absolute or override path of the domain schema.
 */
std::string readSchemaPath() {
    if (const char *overridePath = std::getenv("PMS_SCHEMA_PATH");
        overridePath && *overridePath) {
        return overridePath;
    }
    return utility::xstudio_root("/mikros_shotbrowser/schema.yaml");
}

/**
 * @brief Shows enough of a script key to recognise it, never the whole secret.
 * @param key `SHOTGRID_CLIENT_SECRET`.
 * @return `abcd...wxyz length=N`, or only the length when the key is short.
 */
std::string formatKeyHint(const std::string &key) {
    const std::string length = "length=" + std::to_string(key.size());
    if (key.size() <= 8) {
        return length;
    }
    return key.substr(0, 4) + "..." + key.substr(key.size() - 4) + " " + length;
}

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
    const char *url = std::getenv("SHOTGRID_URL");
    const char *script = std::getenv("SHOTGRID_CLIENT_ID");
    const char *key = std::getenv("SHOTGRID_CLIENT_SECRET");
    const char *mapping = std::getenv("SHOTGRID_MAPPING");
    if (!url || !script || !key || !mapping || !*url || !*script || !*key || !*mapping) {
        throw pms::Error(
            "shotgrid DAL: set SHOTGRID_URL, SHOTGRID_CLIENT_ID, "
            "SHOTGRID_CLIENT_SECRET, SHOTGRID_MAPPING");
    }
    return ShotGridEnv{url, script, key, mapping};
}

/**
 * @brief Logs the site URL, script name, redacted key, and mapping path.
 * @param env Values from `readShotGridEnv`.
 */
void logShotGridSite(const ShotGridEnv &env) {
    trace::info(
        "shotgrid site url=\"" + escapeQuoted(env.url) + "\" script=\"" +
        escapeQuoted(env.scriptName) + "\" key=" + formatKeyHint(env.scriptKey) +
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
 * @brief Cache trace hooks. The DAL formats every line.
 * @param backendName `shotgrid` or `fake`.
 * @return Options passed to `CachingBackend`.
 */
pms::CacheOptions buildCacheOptions(const std::string &backendName) {
    pms::CacheOptions options;
    options.backend_name = backendName;
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
 * @brief Chooses the version parent, then an optional playlist.
 *
 * Strong parent, at most one: shot, else sequence, else asset, else project.
 * A playlist is a second `In` and combines with AND.
 *
 * @param projectId Domain project `id`.
 * @param sequenceId Optional sequence `id`.
 * @param shotId Optional shot `id`.
 * @param assetId Optional asset `id`.
 * @param playlistId Optional playlist `id`.
 * @return Version query ready for `fetchPage`.
 */
auto scopeVersionQuery(
    std::int64_t projectId,
    const std::optional<std::int64_t> &sequenceId,
    const std::optional<std::int64_t> &shotId,
    const std::optional<std::int64_t> &assetId,
    const std::optional<std::int64_t> &playlistId) {
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

} // namespace

std::size_t readPageLimit() {
    if (useShotGrid()) {
        return 500;
    }
    return 5;
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
    auto query = scopeVersionQuery(projectId, sequenceId, shotId, assetId, playlistId);
    return fetchPage<poco::Version>(std::move(query), limit, cursor);
}

} // namespace xstudio::mikros_shotbrowser::dal
