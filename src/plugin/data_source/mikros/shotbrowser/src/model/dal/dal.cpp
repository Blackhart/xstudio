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
 * @brief Path to schema.yaml at runtime.
 *
 * Prefer `PMS_SCHEMA_PATH`, else `xstudio_root("/mikros_shotbrowser/schema.yaml")`
 * (installed via add_resource; launcher sets XSTUDIO_ROOT to share/xstudio).
 */
std::string schemaPath() {
    if (const char *override_path = std::getenv("PMS_SCHEMA_PATH");
        override_path && *override_path) {
        return override_path;
    }
    return utility::xstudio_root("/mikros_shotbrowser/schema.yaml");
}

/**
 * @brief Shows enough of a script key to recognise it, never the whole secret.
 * @param key `SHOTGRID_CLIENT_SECRET`.
 * @return `abcd...wxyz length=N`, or only the length when the key is short.
 */
std::string keyHint(const std::string &key) {
    const std::string length = "length=" + std::to_string(key.size());
    if (key.size() <= 8) {
        return length;
    }
    return key.substr(0, 4) + "..." + key.substr(key.size() - 4) + " " + length;
}

/**
 * @brief Shared backend + CachingBackend + Session for the plugin process.
 *
 * Backend selected by `MIKROS_PMS_BACKEND` (`fake` default, or `shotgrid`).
 * ShotGrid also requires `SHOTGRID_URL`, `SHOTGRID_CLIENT_ID`,
 * `SHOTGRID_CLIENT_SECRET`, and `SHOTGRID_MAPPING` (path to the YAML, outside
 * the source tree). CachingBackend serializes access. Trace lines are formatted
 * by the DAL, for both FakeBackend and ShotGrid.
 */
class SharedPms {
  public:
    static SharedPms &instance() {
        static SharedPms shared;
        return shared;
    }

    pms::Session &session() { return session_; }

  private:
    SharedPms()
        : fake_(makeFake()),
          shotgrid_(makeShotGrid()),
          cache_(innerBackend(), makeCacheOptions(shotgrid_ ? "shotgrid" : "fake")),
          session_(pms::Schema::load(schemaPath()), cache_) {}

    static std::optional<pms::FakeBackend> makeFake() {
        const char *mode = std::getenv("MIKROS_PMS_BACKEND");
        if (mode && std::string(mode) == "shotgrid") {
            return std::nullopt;
        }
        return pms::FakeBackend(/*simulate_latency=*/true);
    }

    static std::optional<pms::ShotGridBackend> makeShotGrid() {
        const char *mode = std::getenv("MIKROS_PMS_BACKEND");
        if (!mode || std::string(mode) != "shotgrid") {
            return std::nullopt;
        }
        const char *url = std::getenv("SHOTGRID_URL");
        const char *script = std::getenv("SHOTGRID_CLIENT_ID");
        const char *key = std::getenv("SHOTGRID_CLIENT_SECRET");
        const char *mapping = std::getenv("SHOTGRID_MAPPING");
        if (!url || !script || !key || !mapping || !*url || !*script || !*key || !*mapping) {
            throw pms::Error(
                "shotgrid DAL: set SHOTGRID_URL, SHOTGRID_CLIENT_ID, "
                "SHOTGRID_CLIENT_SECRET, SHOTGRID_MAPPING");
        }
        const std::string mappingPath = mapping;
        trace::info(
            "shotgrid site url=\"" + escape(url) + "\" script=\"" + escape(script) +
            "\" key=" + keyHint(key) + " mapping=\"" + escape(mappingPath) + "\"");
        pms::ShotGridSite site;
        site.base_url = url;
        site.auth = pms::ShotGridScriptAuth{script, key};
        pms::ShotGridBackend backend(std::move(site), mappingPath);
        backend.setSearchLog([](const std::string &payload) {
            trace::info("shotgrid payload " + payload);
        });
        backend.setAuthLog([](const std::string &line) { trace::info("shotgrid " + line); });
        return backend;
    }

    pms::Backend &innerBackend() {
        if (shotgrid_) {
            return *shotgrid_;
        }
        return *fake_;
    }

    /**
     * @brief Escapes a name so it can sit inside a quoted log field.
     * @param text Raw domain name.
     * @return Text with quotes and backslashes escaped.
     */
    static std::string escape(const std::string &text) {
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
     * @brief Renders the domain page the caller receives.
     * @param source `cache` or `backend`.
     * @param entity Queried entity name.
     * @param page Rows and continuation cursor.
     * @return One `[mikros] page` body, including the id/name lines.
     */
    static std::string formatPage(
        const char *source, const std::string &entity, const pms::Page &page) {
        std::ostringstream out;
        out << "page source=" << source << " entity=" << entity << " rows=" << page.rows.size()
            << " next_cursor=";
        if (page.next_cursor) {
            out << '"' << escape(*page.next_cursor) << '"';
        } else {
            out << '-';
        }
        for (const pms::Record &row : page.rows) {
            out << "\n  { id: " << std::get<std::int64_t>(row.at("id")) << ", name: \""
                << escape(std::get<std::string>(row.at("name"))) << "\" }";
        }
        return out.str();
    }

    /**
     * @brief Drops a trailing newline so the IR block does not end blank.
     * @param text dump() output.
     * @return The same text without a final newline.
     */
    static std::string trimNewline(std::string text) {
        if (!text.empty() && text.back() == '\n') {
            text.pop_back();
        }
        return text;
    }

    static pms::CacheOptions makeCacheOptions(const std::string &backend) {
        pms::CacheOptions options;
        options.backend_name = backend;
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

    std::optional<pms::FakeBackend> fake_;
    std::optional<pms::ShotGridBackend> shotgrid_;
    pms::CachingBackend cache_;
    pms::Session session_;
};

/**
 * @brief Maps `id`/`name` records to a POCO vector.
 * @tparam Row POCO type with `id` and `name` members.
 * @param rows Domain rows.
 * @return POCO rows in the same order.
 */
template <typename Row>
std::vector<Row> toPocos(const pms::Records &rows) {
    std::vector<Row> out;
    out.reserve(rows.size());
    for (const pms::Record &row : rows) {
        out.push_back(Row{
            std::get<std::int64_t>(row.at("id")),
            std::get<std::string>(row.at("name")),
        });
    }
    return out;
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
RowPage<Row>
fetchPage(QueryT query, std::size_t limit, const std::optional<std::string> &cursor) {
    try {
        auto limited = std::move(query).Limit(limit);
        if (cursor && !cursor->empty()) {
            limited = limited.After(*cursor);
        }
        const pms::Page page = limited.find(SharedPms::instance().session());
        return RowPage<Row>{toPocos<Row>(page.rows), page.next_cursor};
    } catch (const std::exception &error) {
        trace::failure(error);
        return {};
    }
}

} // namespace

std::size_t pageLimit() {
    const char *mode = std::getenv("MIKROS_PMS_BACKEND");
    if (mode && std::string(mode) == "shotgrid") {
        return 500;
    }
    return 5;
}

RowPage<poco::Project> projects(std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Project>(pms::Project(), limit, cursor);
}

RowPage<poco::Sequence> sequences(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Sequence>(
        pms::Sequence().In(pms::scope::project == projectId), limit, cursor);
}

RowPage<poco::Shot> shots(
    std::int64_t sequenceId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Shot>(
        pms::Shot().In(pms::scope::sequence == sequenceId), limit, cursor);
}

RowPage<poco::Asset> assets(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Asset>(
        pms::Asset().In(pms::scope::project == projectId), limit, cursor);
}

RowPage<poco::Playlist> playlists(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Playlist>(
        pms::Playlist().In(pms::scope::project == projectId), limit, cursor);
}

RowPage<poco::Version> versions(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor,
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
    return fetchPage<poco::Version>(std::move(query), limit, cursor);
}

} // namespace xstudio::mikros_shotbrowser::dal
