// SPDX-License-Identifier: Apache-2.0
/**
 * @file dal.cpp
 * @brief pms find wrappers that return POCO rows or name lists.
 */

#include "model/dal/dal.hpp"

#include "pms/fake/backend.hpp"
#include "pms/pms.hpp"
#include "pms/shotgrid/backend.hpp"

#include "xstudio/utility/logging.hpp"

#include <cstdlib>
#include <memory>
#include <optional>
#include <variant>

namespace xstudio::mikros_shotbrowser::dal {
namespace {

constexpr std::size_t kDefaultPageLimit = 100;

/**
 * @brief Shared backend + CachingBackend + Session for the plugin process.
 *
 * Backend selected by `MIKROS_PMS_BACKEND` (`fake` default, or `shotgrid`).
 * CachingBackend serializes access and owns the query log.
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
          session_(pms::Schema::load(PMS_SCHEMA_PATH), cache_) {}

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
        const char *url = std::getenv("MIKROS_SG_URL");
        const char *script = std::getenv("MIKROS_SG_SCRIPT_NAME");
        const char *key = std::getenv("MIKROS_SG_SCRIPT_KEY");
        if (!url || !script || !key || !*url || !*script || !*key) {
            throw pms::Error(
                "shotgrid DAL: set MIKROS_SG_URL, MIKROS_SG_SCRIPT_NAME, MIKROS_SG_SCRIPT_KEY");
        }
        const char *mapping = std::getenv("MIKROS_SG_MAPPING");
        const std::string mappingPath =
            mapping && *mapping ? mapping : std::string(PMS_SHOTGRID_MAPPING_PATH);
        pms::ShotGridSite site;
        site.base_url = url;
        site.auth = pms::ShotGridScriptAuth{script, key};
        return pms::ShotGridBackend(std::move(site), mappingPath);
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
 * @brief Loads every page for a query through limited find() calls.
 * @tparam QueryT Fluent query type from a generated factory.
 * @param query Built query (In/Where already applied).
 * @return All matching rows in catalog order.
 */
template <typename QueryT>
pms::Records findAllPages(QueryT query) {
    pms::Session &session = SharedPms::instance().session();
    pms::Records rows;
    std::optional<std::string> cursor;
    for (;;) {
        auto pageQuery = query.Limit(kDefaultPageLimit);
        if (cursor) {
            pageQuery = pageQuery.After(*cursor);
        }
        const pms::Page page = pageQuery.find(session);
        rows.insert(rows.end(), page.rows.begin(), page.rows.end());
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
    }
    return rows;
}

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
 * @brief Runs a catalog query and maps rows to POCOs, or returns {} on error.
 * @tparam Row POCO type with `id` and `name`.
 * @tparam QueryT Fluent query type.
 * @param query Built query (In/Where already applied).
 * @return POCO rows, or empty when the lookup fails.
 */
template <typename Row, typename QueryT>
std::vector<Row> fetchCatalog(QueryT query) {
    try {
        return toPocos<Row>(findAllPages(std::move(query)));
    } catch (const std::exception &error) {
        spdlog::error("Mikros Shot Browser DAL: {}", error.what());
        return {};
    }
}

/**
 * @brief Extracts the `name` field from each record.
 * @param rows Domain rows.
 * @return Names in the same order as `rows`.
 */
std::vector<std::string> namesOf(const pms::Records &rows) {
    std::vector<std::string> names;
    names.reserve(rows.size());
    for (const pms::Record &row : rows) {
        names.push_back(std::get<std::string>(row.at("name")));
    }
    return names;
}

/**
 * @brief Builds the version query with the strong parent and optional playlist.
 */
decltype(pms::Version()) versionQuery(
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
    } else if (!playlistId) {
        query = query.In(pms::scope::project == projectId);
    }
    if (playlistId) {
        query = query.In(pms::scope::playlist == *playlistId);
    }
    return query;
}

} // namespace

std::vector<poco::Project> projects() {
    return fetchCatalog<poco::Project>(pms::Project());
}

std::vector<poco::Sequence> sequences(std::int64_t projectId) {
    return fetchCatalog<poco::Sequence>(pms::Sequence().In(pms::scope::project == projectId));
}

std::vector<poco::Shot> shots(std::int64_t sequenceId) {
    return fetchCatalog<poco::Shot>(pms::Shot().In(pms::scope::sequence == sequenceId));
}

std::vector<poco::Asset> assets(std::int64_t projectId) {
    return fetchCatalog<poco::Asset>(pms::Asset().In(pms::scope::project == projectId));
}

std::vector<poco::Playlist> playlists(std::int64_t projectId) {
    return fetchCatalog<poco::Playlist>(pms::Playlist().In(pms::scope::project == projectId));
}

NamePage versionNames(
    std::int64_t projectId,
    std::size_t limit,
    std::optional<std::string> cursor,
    std::optional<std::int64_t> sequenceId,
    std::optional<std::int64_t> shotId,
    std::optional<std::int64_t> assetId,
    std::optional<std::int64_t> playlistId) {
    try {
        auto query =
            versionQuery(projectId, sequenceId, shotId, assetId, playlistId).Limit(limit);
        if (cursor && !cursor->empty()) {
            query = query.After(*cursor);
        }
        const pms::Page page = query.find(SharedPms::instance().session());
        return NamePage{namesOf(page.rows), page.next_cursor};
    } catch (const std::exception &error) {
        spdlog::error("Mikros Shot Browser DAL: {}", error.what());
        return {};
    }
}

void clearCache() { SharedPms::instance().clearCache(); }

} // namespace xstudio::mikros_shotbrowser::dal
