// SPDX-License-Identifier: Apache-2.0
/**
 * @file dal.cpp
 * @brief pms find wrappers that return one page of POCO rows.
 */

#include "model/dal/dal.hpp"

#include "pms/fake/backend.hpp"
#include "pms/pms.hpp"
#include "pms/shotgrid/backend.hpp"

#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/logging.hpp"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace xstudio::mikros_shotbrowser::dal {
namespace {

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
 * @brief Shared backend + CachingBackend + Session for the plugin process.
 *
 * Backend selected by `MIKROS_PMS_BACKEND` (`fake` default, or `shotgrid`).
 * ShotGrid also requires `SHOTGRID_URL`, `SHOTGRID_CLIENT_ID`,
 * `SHOTGRID_CLIENT_SECRET`, and `SHOTGRID_MAPPING` (path to the YAML, outside
 * the source tree). CachingBackend serializes access and owns the query log.
 */
class SharedPms {
  public:
    static SharedPms &instance() {
        static SharedPms shared;
        return shared;
    }

    pms::Session &session() { return session_; }

    void clearCache() { cache_.clear(); }

  private:
    SharedPms()
        : fake_(makeFake()),
          shotgrid_(makeShotGrid()),
          cache_(innerBackend(), makeCacheOptions()),
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
        pms::ShotGridSite site;
        site.base_url = url;
        site.auth = pms::ShotGridScriptAuth{script, key};
        pms::ShotGridBackend backend(std::move(site), mappingPath);
        backend.setDebug([](const std::string &message) {
            spdlog::info("Mikros Shot Browser shotgrid:\n{}", message);
        });
        return backend;
    }

    pms::Backend &innerBackend() {
        if (shotgrid_) {
            return *shotgrid_;
        }
        return *fake_;
    }

    static pms::CacheOptions makeCacheOptions() {
        pms::CacheOptions options;
        options.debug = [](const std::string &message) {
            spdlog::info("Mikros Shot Browser cache:\n{}", message);
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
        spdlog::error("Mikros Shot Browser DAL: {}", error.what());
        return {};
    }
}

} // namespace

RowPage<poco::Project> projects(std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Project>(pms::Project(), limit, cursor);
}

RowPage<poco::Sequence> sequences(
    std::int64_t projectId, std::size_t limit, std::optional<std::string> cursor) {
    return fetchPage<poco::Sequence>(
        pms::Sequence().In(pms::scope::project == projectId), limit, cursor);
}

RowPage<poco::Shot> shots(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor,
    std::optional<std::int64_t> sequenceId) {
    if (sequenceId) {
        return fetchPage<poco::Shot>(
            pms::Shot().In(pms::scope::sequence == *sequenceId), limit, cursor);
    }
    return fetchPage<poco::Shot>(
        pms::Shot().In(pms::scope::project == projectId), limit, cursor);
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
    } else if (!playlistId) {
        query = query.In(pms::scope::project == projectId);
    }
    if (playlistId) {
        query = query.In(pms::scope::playlist == *playlistId);
    }
    return fetchPage<poco::Version>(std::move(query), limit, cursor);
}

void clearCache() { SharedPms::instance().clearCache(); }

} // namespace xstudio::mikros_shotbrowser::dal
