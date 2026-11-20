// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/catalog.hpp
 * @brief Catalog load, the page mail, and the labels a combo builds from those rows.
 */

#pragma once

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "model/poco/asset.hpp"
#include "model/poco/playlist.hpp"
#include "model/poco/project.hpp"
#include "model/poco/sequence.hpp"
#include "model/poco/sequence_shots.hpp"
#include "model/poco/shot.hpp"
#include "model/poco/version.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/plugin_manager/plugin_base.hpp"
#include "utility/thumbnails.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xstudio::mikros_shotbrowser {

namespace trace = log;

/**
 * @brief Which list one detached actor loads.
 */
enum class CatalogKind {
    Project,          ///< Project combo.
    ShotTree,         ///< Sequences of one project, each with its shots.
    ShotTreeCount,    ///< Sequence and shot totals for the shot-tree footer.
    Asset,            ///< Asset combo.
    AssetCount,       ///< Asset total for the asset-list footer.
    Version,          ///< One version page for the current parents (cursor continues).
    VersionCount,     ///< Version total for the version-list footer.
    PlaylistTree,     ///< One playlist-name page (cursor continues), with version counts.
    PlaylistCount,    ///< Playlist total for the playlist-tree footer.
    PlaylistVersions, ///< Versions of one unfolded playlist.
};

/**
 * @brief Request id shared with the worker that fetches the pages.
 *
 * Empty for shot pages and playlist-version pages. The worker reads it before each fetch.
 */
using RequestGeneration = std::shared_ptr<std::atomic<std::uint64_t>>;

/**
 * @brief One catalog load. The actor stops after it finishes this request.
 *
 * Parents that do not apply to `kind` stay empty. The panel still drops a page
 * whose request id is no longer current.
 */
struct CatalogLoadRequest {
    CatalogKind kind = CatalogKind::Project; ///< List this actor loads.
    caf::weak_actor_ptr panel;               ///< Panel that receives the pages.
    std::uint64_t request_id = 0;            ///< Id the panel checks before applying a page.
    RequestGeneration generation;            ///< Stops the next fetch when this id moves. Empty for playlist versions.
    std::int64_t project_id = 0;             ///< Project, or 0 for the project catalog.
    std::int64_t playlist_id = 0;            ///< Playlist whose versions to load.
    std::optional<std::int64_t> sequence;    ///< Version filter: selected sequence.
    std::optional<std::int64_t> shot;        ///< Version filter: selected shot.
    std::optional<std::int64_t> asset;       ///< Version filter: selected asset.
    std::optional<std::string> cursor; ///< Continuation for Version / PlaylistTree pages. Empty for the first page.
    std::optional<std::string> name_or_id; ///< Exact id or name substring for Version / PlaylistTree.
};

/**
 * @brief Loads one catalog on its own thread, then stops.
 *
 * Spawned detached so a blocking find does not occupy the panel actor.
 * It is not linked to the panel: a failed load does not stop the panel.
 */
class CatalogLoadActor : public caf::event_based_actor {
  public:
    /**
     * @brief Builds an actor that waits for one load request.
     * @param cfg CAF actor config.
     */
    explicit CatalogLoadActor(caf::actor_config &cfg);

    /** @brief Name shown in CAF logs. */
    const char *name() const override;

    /**
     * @brief Runs the load, then quits. A failure is logged and does not stop the panel.
     * @return The one-shot behavior.
     */
    caf::behavior make_behavior() override;
};

/**
 * @brief Runs the DAL load and mails each page to the panel.
 * @param request Catalog, request id, project, and parents.
 */
void executeCatalogLoad(const CatalogLoadRequest &request);

/**
 * @brief Registers `CatalogLoadRequest` with CAF. Call once before the first load.
 */
void registerCatalogLoadType();

/**
 * @brief Optional parents carried with a catalog page.
 *
 * Versions set whichever parents are selected. Playlist versions also name the playlist.
 */
struct PageScope {
    bool has_sequence = false;    ///< True when `sequence_id` filters the page.
    std::int64_t sequence_id = 0; ///< Sequence id when `has_sequence` is true.
    bool has_shot = false;        ///< True when `shot_id` filters the page.
    std::int64_t shot_id = 0;     ///< Shot id when `has_shot` is true.
    bool has_asset = false;       ///< True when `asset_id` filters the page.
    std::int64_t asset_id = 0;    ///< Asset id when `has_asset` is true.
    bool has_playlist = false;    ///< True when `playlist_id` names the unfolded playlist.
    std::int64_t playlist_id = 0; ///< Playlist id when `has_playlist` is true.
};

/**
 * @brief Parents that narrow a version or playlist query.
 */
struct VersionParents {
    std::optional<std::int64_t> sequenceId; ///< Selected sequence, if any.
    std::optional<std::int64_t> shotId;     ///< Selected shot, if any.
    std::optional<std::int64_t> assetId;    ///< Selected asset, if any.
};

/**
 * @brief Copies the current sequence, shot, and asset ids.
 *
 * Sequence, shot, and asset come from the DAL.
 * @return Ids safe to capture on a worker.
 */
VersionParents readVersionParents();

/**
 * @brief Records which sequence, shot, and asset a version page was loaded for.
 *
 * The panel drops the page when the user has changed any of those three.
 * @param parents Sequence, shot, and asset ids captured when the load started.
 * @return The filter carried with every page of that load.
 */
PageScope pageScope(const VersionParents &parents);

/**
 * @brief Sends one page to the panel, or does nothing when the panel is gone.
 * @param weak Panel actor. A dead panel skips the send.
 * @param requestId Generation of the load. The panel ignores an older id.
 * @param catalog Which list the page belongs to.
 * @param projectId Project the page belongs to, or 0 for the project list.
 * @param scope Sequence, shot, and asset the page was loaded for.
 * @param append False for the first page, which replaces the list.
 * @param payload JSON rows or a tree.
 */
void sendCatalog(
    const caf::weak_actor_ptr &weak,
    std::uint64_t requestId,
    const std::string &catalog,
    std::int64_t projectId,
    const PageScope &scope,
    bool append,
    xstudio::utility::JsonStore payload);

/**
 * @brief Spawns one detached load and sends it the request.
 *
 * The handle is dropped after the send. The actor is not linked to the panel,
 * so a failed load does not stop the panel.
 *
 * @param system Actor system that owns the panel.
 * @param request Catalog, request id, project, and parents.
 */
void launchCatalogLoad(caf::actor_system &system, CatalogLoadRequest request);

/**
 * @brief Fills the fields every catalog request carries.
 * @param panel Actor that receives the pages.
 * @param kind Catalog to load.
 * @param requestId Id the panel checks before applying a page.
 * @param projectId Project, or 0 for the project catalog.
 * @return A request with no sequence, shot, asset, or playlist parent yet.
 */
CatalogLoadRequest makeCatalogRequest(
    caf::event_based_actor *panel,
    CatalogKind kind,
    std::uint64_t requestId,
    std::int64_t projectId);

/**
 * @brief Reads the sequence, shot, and asset the version load was started with.
 * @param request Load whose filter may name those three.
 * @return The filter a later page must still match.
 */
VersionParents versionParentsOf(const CatalogLoadRequest &request);

/**
 * @brief Logs one selector after it has been resolved.
 * @tparam Row POCO with `id` and `name`.
 * @param entity `project`, `sequence`, `shot`, or `asset`.
 * @param row Chosen row, or empty when the selector is cleared.
 */
template <typename Row>
void logSelection(const char *entity, const std::optional<Row> &row) {
    if (!row) {
        trace::select(entity, false, 0, {});
        return;
    }
    trace::select(entity, true, row->id, row->name);
}

/**
 * @brief Builds combo labels, appending the id when a name is repeated.
 * @tparam Row POCO with `id` and `name`.
 * @param rows Catalog rows in display order.
 * @return One label per row.
 */
template <typename Row>
std::vector<std::string> buildUniqueLabels(const std::vector<Row> &rows) {
    std::map<std::string, int> nameCounts;
    for (const Row &row : rows) {
        ++nameCounts[row.name];
    }
    std::vector<std::string> labels;
    labels.reserve(rows.size());
    for (const Row &row : rows) {
        if (nameCounts[row.name] > 1) {
            labels.push_back(row.name + " (" + std::to_string(row.id) + ")");
        } else {
            labels.push_back(row.name);
        }
    }
    return labels;
}

/**
 * @brief Finds the row whose unique label matches.
 * @tparam Row POCO with `id` and `name`.
 * @param rows Catalog rows.
 * @param label Combo text.
 * @return The row, or empty when the label is not in `rows`.
 */
template <typename Row>
std::optional<Row> findRowByLabel(const std::vector<Row> &rows, const std::string &label) {
    const std::vector<std::string> labels = buildUniqueLabels(rows);
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (labels[index] == label) {
            return rows[index];
        }
    }
    return std::nullopt;
}

/**
 * @brief Writes id/name rows as a JSON array.
 * @tparam Row POCO with `id` and `name`.
 * @param rows Rows to send to the actor.
 * @return JSON store the worker message can carry.
 */
template <typename Row>
xstudio::utility::JsonStore rowsToJson(const std::vector<Row> &rows) {
    nlohmann::json out = nlohmann::json::array();
    for (const Row &row : rows) {
        out.push_back({{"id", row.id}, {"name", row.name}});
    }
    return xstudio::utility::JsonStore(out);
}

/**
 * @brief Reads id/name rows from a worker payload.
 * @tparam Row POCO with `id` and `name`.
 * @param json Payload from `rowsToJson`.
 * @return Rows, or empty when `json` is not an array.
 */
template <typename Row>
std::vector<Row> rowsFromJson(const xstudio::utility::JsonStore &json) {
    std::vector<Row> rows;
    if (!json.is_array()) {
        return rows;
    }
    rows.reserve(json.size());
    for (const auto &entry : json) {
        rows.push_back(Row{
            entry.at("id").template get<std::int64_t>(),
            entry.at("name").template get<std::string>(),
        });
    }
    return rows;
}

/**
 * @brief Writes playlist rows with their version counts.
 * @param rows Playlists to send to the panel.
 * @return JSON the panel message can carry.
 */
template <>
inline xstudio::utility::JsonStore rowsToJson<poco::Playlist>(
    const std::vector<poco::Playlist> &rows) {
    nlohmann::json out = nlohmann::json::array();
    for (const poco::Playlist &row : rows) {
        out.push_back(
            {{"id", row.id},
             {"name", row.name},
             {"version_count", row.version_count},
             {"open_notes_count", row.open_notes_count},
             {"departments", row.departments},
             {"created_at", row.created_at},
             {"category", row.category},
             {"author", row.author}});
    }
    return xstudio::utility::JsonStore(out);
}

/**
 * @brief Reads playlist rows with their version counts.
 * @param json Payload from `rowsToJson`.
 * @return Playlists, or empty when `json` is not an array.
 */
template <>
inline std::vector<poco::Playlist> rowsFromJson<poco::Playlist>(
    const xstudio::utility::JsonStore &json) {
    std::vector<poco::Playlist> rows;
    if (!json.is_array()) {
        return rows;
    }
    rows.reserve(json.size());
    for (const auto &entry : json) {
        poco::Playlist row;
        row.id = entry.at("id").get<std::int64_t>();
        row.name = entry.at("name").get<std::string>();
        row.version_count = entry.value("version_count", static_cast<std::size_t>(0));
        row.open_notes_count = entry.value("open_notes_count", static_cast<std::int64_t>(0));
        row.departments = entry.value("departments", std::string());
        row.created_at = entry.value("created_at", std::string());
        row.category = entry.value("category", std::string());
        row.author = entry.value("author", std::string());
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief Writes one version list page (metadata only).
 * @param rows List elements to send to the panel.
 * @return JSON the panel message can carry.
 */
template <>
inline xstudio::utility::JsonStore rowsToJson<poco::VersionListElement>(
    const std::vector<poco::VersionListElement> &rows) {
    nlohmann::json out = nlohmann::json::array();
    for (const poco::VersionListElement &row : rows) {
        out.push_back(
            {{"id", row.id},
             {"name", row.name},
             {"author", row.author},
             {"created_at", row.created_at},
             {"frame_range", row.frame_range},
             {"description", row.description},
             {"step", row.step},
             {"status", row.status},
             {"artistic_status", row.artistic_status},
             {"sites", row.sites},
             {"open_notes_count", row.open_notes_count},
             {"synced_to_client", row.synced_to_client},
             {"frames", row.frames},
             {"frames_provider", row.frames_provider},
             {"movie", row.movie},
             {"movie_provider", row.movie_provider}});
    }
    return xstudio::utility::JsonStore(out);
}

/**
 * @brief Reads one version list page.
 * @param json Payload from `rowsToJson`.
 * @return List elements, or empty when `json` is not an array.
 */
template <>
inline std::vector<poco::VersionListElement> rowsFromJson<poco::VersionListElement>(
    const xstudio::utility::JsonStore &json) {
    std::vector<poco::VersionListElement> rows;
    if (!json.is_array()) {
        return rows;
    }
    rows.reserve(json.size());
    for (const auto &entry : json) {
        poco::VersionListElement row;
        row.id = entry.at("id").get<std::int64_t>();
        row.name = entry.at("name").get<std::string>();
        row.author = entry.value("author", std::string());
        row.created_at = entry.value("created_at", std::string());
        row.frame_range = entry.value("frame_range", std::string());
        row.description = entry.value("description", std::string());
        row.step = entry.value("step", std::string());
        row.status = entry.value("status", std::string());
        row.artistic_status = entry.value("artistic_status", std::string());
        row.sites = entry.value("sites", std::string());
        row.open_notes_count = entry.value("open_notes_count", static_cast<std::int64_t>(0));
        row.synced_to_client = entry.value("synced_to_client", false);
        row.frames = entry.value("frames", std::string());
        row.frames_provider = entry.value("frames_provider", std::string());
        row.movie = entry.value("movie", std::string());
        row.movie_provider = entry.value("movie_provider", std::string());
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief Writes one page of sequences, including the shots attached to each.
 * @param rows Sequences to send to the panel.
 * @return JSON the panel message can carry.
 */
template <>
inline xstudio::utility::JsonStore rowsToJson<poco::SequenceShots>(
    const std::vector<poco::SequenceShots> &rows) {
    nlohmann::json out = nlohmann::json::array();
    for (const poco::SequenceShots &node : rows) {
        nlohmann::json shots = nlohmann::json::array();
        for (const poco::Shot &shot : node.shots) {
            shots.push_back({{"id", shot.id}, {"name", shot.name}});
        }
        out.push_back(
            {{"id", node.sequence.id}, {"name", node.sequence.name}, {"shots", std::move(shots)}});
    }
    return xstudio::utility::JsonStore(out);
}

/**
 * @brief Reads one page of sequences and the shots stored on each.
 * @param json Payload from `rowsToJson`.
 * @return Nodes, or empty when `json` is not an array.
 */
template <>
inline std::vector<poco::SequenceShots> rowsFromJson<poco::SequenceShots>(
    const xstudio::utility::JsonStore &json) {
    std::vector<poco::SequenceShots> rows;
    if (!json.is_array()) {
        return rows;
    }
    rows.reserve(json.size());
    for (const auto &entry : json) {
        poco::SequenceShots node;
        node.sequence.id = entry.at("id").get<std::int64_t>();
        node.sequence.name = entry.at("name").get<std::string>();
        if (entry.contains("shots") && entry.at("shots").is_array()) {
            for (const auto &shot : entry.at("shots")) {
                node.shots.push_back(poco::Shot{
                    shot.at("id").get<std::int64_t>(),
                    shot.at("name").get<std::string>(),
                });
            }
        }
        rows.push_back(std::move(node));
    }
    return rows;
}

/**
 * @brief Replaces or extends a catalog and refreshes its combo labels.
 *
 * The first page clears the combo value. Later pages keep a selection made
 * while earlier pages were already visible.
 *
 * @tparam Row POCO with `id` and `name`.
 * @param stored Rows accumulated for this combo.
 * @param attr Combo attribute.
 * @param rows Rows from the latest page.
 * @param append False for the first page of a request.
 */
template <typename Row>
void applyCatalogPage(
    std::vector<Row> &stored,
    xstudio::module::StringChoiceAttribute *attr,
    std::vector<Row> rows,
    bool append) {
    if (!append) {
        stored = std::move(rows);
        attr->set_role_data(xstudio::module::Attribute::StringChoices, buildUniqueLabels(stored));
        attr->set_value("");
        return;
    }
    if (rows.empty()) {
        return;
    }
    stored.insert(stored.end(), rows.begin(), rows.end());
    attr->set_role_data(xstudio::module::Attribute::StringChoices, buildUniqueLabels(stored));
}

/**
 * @brief Loads every page of one list and sends each page to the panel.
 *
 * Version pages also start a thumbnail download that does not block this load.
 * @tparam Row POCO returned by `fetch`.
 * @tparam Fetch Callable taking a cursor and returning one DAL page.
 * @param weak Panel that receives the pages.
 * @param requestId Generation of the load. The panel ignores an older id.
 * @param catalog Which list the pages belong to.
 * @param projectId Project the pages belong to, or 0 for the project list.
 * @param fetch One DAL page call.
 * @param scope Sequence, shot, and asset the pages were loaded for.
 * @param generation When set, the loop stops before the next fetch if this id moved.
 */
template <typename Row, typename Fetch>
void publishPages(
    const caf::weak_actor_ptr &weak,
    std::uint64_t requestId,
    const std::string &catalog,
    std::int64_t projectId,
    Fetch fetch,
    PageScope scope = {},
    RequestGeneration generation = {}) {
    std::optional<std::string> cursor;
    bool append = false;
    for (;;) {
        if (generation && generation->load() != requestId) {
            break; ///< A newer load started. Do not call the DAL again.
        }
        const dal::RowPage<Row> page = fetch(cursor);
        sendCatalog(weak, requestId, catalog, projectId, scope, append, rowsToJson(page.rows));
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
        append = true;
    }
}

} // namespace xstudio::mikros_shotbrowser
