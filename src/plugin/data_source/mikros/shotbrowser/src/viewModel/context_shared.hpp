// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/context_shared.hpp
 * @brief Panel state, catalog load actor, page mail, and the label helpers every screen shares.
 *
 * Included only by the view model translation units. The plugin does not see it.
 */

#pragma once

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "model/poco/asset.hpp"
#include "model/poco/project.hpp"
#include "model/poco/sequence.hpp"
#include "model/poco/shot.hpp"
#include "model/poco/version.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/plugin_manager/plugin_base.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace xstudio;

namespace xstudio::mikros_shotbrowser {

namespace trace = log;

/**
 * @brief Which list one detached actor loads.
 */
enum class CatalogKind {
    Project,          ///< Project combo.
    ShotTree,         ///< Sequence names, without their shots.
    SequenceShots,    ///< Shots of one unfolded sequence.
    Asset,            ///< Asset combo.
    Version,          ///< Version list for the current parents.
    PlaylistTree,     ///< Playlist names, without their versions.
    PlaylistVersions, ///< Versions of one unfolded playlist.
};

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
    std::int64_t project_id = 0;             ///< Project, or 0 for the project catalog.
    std::int64_t sequence_id = 0;            ///< Sequence whose shots to load.
    std::int64_t playlist_id = 0;            ///< Playlist whose versions to load.
    std::optional<std::int64_t> sequence;    ///< Version filter: selected sequence.
    std::optional<std::int64_t> shot;        ///< Version filter: selected shot.
    std::optional<std::int64_t> asset;       ///< Version filter: selected asset.
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
 * @brief Home directory and the thumbnail directory, shared by the cache and the movie download.
 */
namespace thumb {

/**
 * @brief Home directory resolved at call time.
 * @return `HOME`, or `USERPROFILE` on Windows. Empty when neither is set.
 */
std::filesystem::path homeDirectory();

/**
 * @brief Directory that holds every version thumbnail.
 * @return `${HOME}/xStudio/mikros_shotbrowser/thumbs`, or empty when home is missing.
 */
std::filesystem::path thumbRoot();

} // namespace thumb

/**
 * @brief Optional parents carried with a catalog page.
 *
 * Versions set whichever parents are selected.
 */
struct PageScope {
    bool has_sequence = false;    ///< True when `sequence_id` filters the page.
    std::int64_t sequence_id = 0; ///< Sequence id when `has_sequence` is true.
    bool has_shot = false;        ///< True when `shot_id` filters the page.
    std::int64_t shot_id = 0;     ///< Shot id when `has_shot` is true.
    bool has_asset = false;       ///< True when `asset_id` filters the page.
    std::int64_t asset_id = 0;    ///< Asset id when `has_asset` is true.
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
 * @param sequence Selected sequence, or empty.
 * @param shot Selected shot, or empty.
 * @param asset Selected asset, or empty.
 * @return Ids safe to capture on a worker.
 */
VersionParents readVersionParents(
    const std::optional<poco::Sequence> &sequence,
    const std::optional<poco::Shot> &shot,
    const std::optional<poco::Asset> &asset);

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
    utility::JsonStore payload);

/**
 * @brief Address of one tree mail. Each message carries the tree built so far.
 */
struct TreeMail {
    caf::weak_actor_ptr weak;     ///< Actor that applies the tree.
    std::uint64_t requestId = 0;  ///< Request that must still be current.
    std::string catalog;          ///< `shot_tree` or `playlist_tree`.
    std::int64_t projectId = 0;   ///< Project the tree belongs to.
    PageScope scope;              ///< Parents that must still match.
};

/**
 * @brief Mails the tree built so far, so the panel can show this page.
 * @param mail Destination and request.
 * @param tree Sequences or playlists accumulated on the worker.
 */
void sendTree(const TreeMail &mail, const nlohmann::json &tree);

/**
 * @brief Address of one tree mail built from a load request.
 * @param request Panel, request id, and project.
 * @param catalog `shot_tree`, `sequence_shots`, `playlist_tree`, or `playlist_versions`.
 * @param scope Parents that must still match when the page is applied.
 * @return Mail the publish functions send.
 */
TreeMail treeMail(const CatalogLoadRequest &request, const std::string &catalog, PageScope scope);

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
 * @brief Stores one page of images without blocking the actor.
 * @param weak Actor that receives the local file URLs.
 * @param projectId Project the page belongs to.
 * @param rows Versions of this page, with their image source.
 */
void scheduleThumbPage(
    const caf::weak_actor_ptr &weak,
    std::int64_t projectId,
    const std::vector<poco::Version> &rows);

/**
 * @brief Catalogs, selection, and attributes shared by every screen.
 *
 * The actor owns one instance. A screen function receives it instead of reading members.
 */
struct PanelState {
    caf::event_based_actor *panel = nullptr; ///< Actor that spawns loads and inserts media.

    std::vector<poco::Project> projects_;   ///< Project pages received so far.
    std::vector<poco::Asset> assets_;       ///< Asset pages for `selected_project_`.
    std::vector<poco::Version> versions_;   ///< Version pages for the current selection.
    std::optional<poco::Project> selected_project_;   ///< Current Project combo selection.
    std::optional<poco::Sequence> selected_sequence_; ///< Sequence selected in the shot tree.
    std::optional<poco::Shot> selected_shot_;         ///< Shot selected in the shot tree.
    std::optional<poco::Asset> selected_asset_;       ///< Current Asset selection.

    std::uint64_t project_request_id_ = 0;  ///< Drops stale project worker replies.
    std::uint64_t sequence_request_id_ = 0; ///< Drops stale shot-tree worker replies.
    std::uint64_t asset_request_id_ = 0;    ///< Drops stale asset worker replies.
    std::uint64_t version_request_id_ = 0;  ///< Drops stale version worker replies.
    std::uint64_t playlist_request_id_ = 0; ///< Drops stale playlist-tree worker replies.
    bool aligning_sequence_ = false;        ///< Shot selection updates its sequence without clearing the shot.
    bool sequences_ready_ = false;          ///< Sequences were requested for the current project.
    bool assets_ready_ = false;             ///< Assets were requested for the current project.
    bool playlists_ready_ = false;          ///< Playlist names were requested for the current project.

    module::StringAttribute *browser_mode_ = nullptr;      ///< `shot` or `asset`.
    module::StringAttribute *result_view_ = nullptr;       ///< `versions` or `playlist`.
    module::StringAttribute *expanded_sequence_ = nullptr; ///< Sequence the user unfolded.
    module::StringAttribute *expanded_playlist_ = nullptr; ///< Playlist the user unfolded.

    module::StringChoiceAttribute *project_ = nullptr;    ///< Project combo.
    module::StringChoiceAttribute *sequence_ = nullptr;   ///< Sequence combo.
    module::StringChoiceAttribute *shot_ = nullptr;       ///< Shot combo.
    module::JsonAttribute *shot_tree_ = nullptr;          ///< Sequences with their shot names.
    module::StringChoiceAttribute *asset_ = nullptr;      ///< Asset combo.
    module::StringChoiceAttribute *version_ = nullptr;    ///< Version combo.
    module::BooleanAttribute *versions_loaded_ = nullptr; ///< True once the current version request has delivered a page.
    module::JsonAttribute *version_cards_ = nullptr;      ///< Visible versions: name and local image.
    module::JsonAttribute *playlist_tree_ = nullptr;      ///< Playlists with their version rows.
    module::StringAttribute *open_version_ = nullptr;     ///< JSON request from a double-click or the menu.
    module::StringAttribute *opened_media_ = nullptr;     ///< JSON result the panel selects when asked.
    std::map<std::pair<std::int64_t, std::int64_t>, std::string> thumb_paths_; ///< `(projectId, versionId)` to `file://`.
};

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
utility::JsonStore rowsToJson(const std::vector<Row> &rows) {
    nlohmann::json out = nlohmann::json::array();
    for (const Row &row : rows) {
        out.push_back({{"id", row.id}, {"name", row.name}});
    }
    return utility::JsonStore(out);
}

/**
 * @brief Reads id/name rows from a worker payload.
 * @tparam Row POCO with `id` and `name`.
 * @param json Payload from `rowsToJson`.
 * @return Rows, or empty when `json` is not an array.
 */
template <typename Row>
std::vector<Row> rowsFromJson(const utility::JsonStore &json) {
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
 * @brief Writes one version page, including the image the card and the open action need.
 * @param rows Versions to send to the panel.
 * @return JSON the panel message can carry.
 */
template <>
inline
utility::JsonStore rowsToJson<poco::Version>(const std::vector<poco::Version> &rows) {
    nlohmann::json out = nlohmann::json::array();
    for (const poco::Version &row : rows) {
        out.push_back(
            {{"id", row.id},
             {"name", row.name},
             {"image", row.image},
             {"image_provider", row.image_provider},
             {"author", row.author},
             {"created_at", row.created_at},
             {"frame_range", row.frame_range},
             {"description", row.description},
             {"step", row.step},
             {"status", row.status},
             {"artistic_status", row.artistic_status},
             {"sites", row.sites},
             {"has_notes", row.has_notes},
             {"synced_to_client", row.synced_to_client},
             {"frames", row.frames},
             {"movie", row.movie},
             {"uploaded_movie", row.uploaded_movie}});
    }
    return utility::JsonStore(out);
}

/**
 * @brief Reads one version page back into rows the panel can show and open.
 * @param json Payload from `rowsToJson`.
 * @return Versions, or empty when `json` is not an array.
 */
template <>
inline
std::vector<poco::Version> rowsFromJson<poco::Version>(const utility::JsonStore &json) {
    std::vector<poco::Version> rows;
    if (!json.is_array()) {
        return rows;
    }
    rows.reserve(json.size());
    for (const auto &entry : json) {
        poco::Version row;
        row.id = entry.at("id").get<std::int64_t>();
        row.name = entry.at("name").get<std::string>();
        row.image = entry.value("image", std::string());
        row.image_provider = entry.value("image_provider", std::string());
        row.author = entry.value("author", std::string());
        row.created_at = entry.value("created_at", std::string());
        row.frame_range = entry.value("frame_range", std::string());
        row.description = entry.value("description", std::string());
        row.step = entry.value("step", std::string());
        row.status = entry.value("status", std::string());
        row.artistic_status = entry.value("artistic_status", std::string());
        row.sites = entry.value("sites", std::string());
        row.has_notes = entry.value("has_notes", false);
        row.synced_to_client = entry.value("synced_to_client", false);
        row.frames = entry.value("frames", std::string());
        row.movie = entry.value("movie", std::string());
        row.uploaded_movie = entry.value("uploaded_movie", std::string());
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief True when the reply's optional id matches the current selection.
 * @tparam Row POCO with an `id` field.
 * @param hasId Whether the worker sent an id for this filter.
 * @param id Id from the worker when `hasId` is true.
 * @param selected Current selection, or empty when none.
 * @return True when both sides are empty, or both name the same id.
 */
template <typename Row>
bool selectionMatches(bool hasId, std::int64_t id, const std::optional<Row> &selected) {
    if (hasId) {
        return selected && selected->id == id;
    }
    return !selected;
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
    module::StringChoiceAttribute *attr,
    std::vector<Row> rows,
    bool append) {
    if (!append) {
        stored = std::move(rows);
        attr->set_role_data(module::Attribute::StringChoices, buildUniqueLabels(stored));
        attr->set_value("");
        return;
    }
    if (rows.empty()) {
        return;
    }
    stored.insert(stored.end(), rows.begin(), rows.end());
    attr->set_role_data(module::Attribute::StringChoices, buildUniqueLabels(stored));
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
 */
template <typename Row, typename Fetch>
void publishPages(
    const caf::weak_actor_ptr &weak,
    std::uint64_t requestId,
    const std::string &catalog,
    std::int64_t projectId,
    Fetch fetch,
    PageScope scope = {}) {
    std::optional<std::string> cursor;
    bool append = false;
    for (;;) {
        const dal::RowPage<Row> page = fetch(cursor);
        sendCatalog(weak, requestId, catalog, projectId, scope, append, rowsToJson(page.rows));
        if constexpr (std::is_same_v<Row, poco::Version>) {
            scheduleThumbPage(weak, projectId, page.rows); ///< Names are already on the way.
        }
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
        append = true;
    }
}

} // namespace xstudio::mikros_shotbrowser
