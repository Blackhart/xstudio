// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_shotbrowser_context.cpp
 * @brief Async DAL loads and combo updates for the Mikros Shot Browser panel.
 */

#include "model/context/mikros_shotbrowser_context.hpp"

#include "model/log.hpp"

#include "xstudio/atoms.hpp"

namespace trace = xstudio::mikros_shotbrowser::log;

#include <map>
#include <thread>

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace {

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
    const std::optional<poco::Asset> &asset) {
    return VersionParents{
        sequence ? std::optional<std::int64_t>{sequence->id} : std::nullopt,
        shot ? std::optional<std::int64_t>{shot->id} : std::nullopt,
        asset ? std::optional<std::int64_t>{asset->id} : std::nullopt,
    };
}

/**
 * @brief Turns version parents into the flags carried by a worker message.
 * @param parents Sequence, shot, and asset ids.
 * @return Scope the actor checks before applying the reply.
 */
PageScope pageScope(const VersionParents &parents) {
    PageScope scope;
    scope.has_sequence = parents.sequenceId.has_value();
    scope.sequence_id = parents.sequenceId.value_or(0);
    scope.has_shot = parents.shotId.has_value();
    scope.shot_id = parents.shotId.value_or(0);
    scope.has_asset = parents.assetId.has_value();
    scope.asset_id = parents.assetId.value_or(0);
    return scope;
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
 * @brief Loads every page of one catalog.
 * @tparam Row POCO returned by `fetch`.
 * @tparam Fetch Callable taking a cursor and returning `dal::RowPage<Row>`.
 * @param fetch One DAL page call.
 * @return Every row, in page order.
 */
template <typename Row, typename Fetch>
std::vector<Row> collectPages(Fetch fetch) {
    std::vector<Row> rows;
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<Row> page = fetch(cursor);
        rows.insert(rows.end(), page.rows.begin(), page.rows.end());
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
    }
    return rows;
}

/**
 * @brief Mails one catalog payload to the actor when it is still alive.
 * @param weak Actor to receive the page.
 * @param requestId Request that must still be current when the page is applied.
 * @param catalog Catalog name stored on the message.
 * @param projectId Parent project id, or 0 for the project catalog.
 * @param scope Parents that must still match when the page is applied.
 * @param append False for the first page of a request.
 * @param payload JSON rows or a tree.
 */
void sendCatalog(
    const caf::weak_actor_ptr &weak,
    std::uint64_t requestId,
    const std::string &catalog,
    std::int64_t projectId,
    const PageScope &scope,
    bool append,
    utility::JsonStore payload) {
    if (auto handle = weak.lock()) {
        anon_mail(
            data_source::put_data_atom_v,
            requestId,
            catalog,
            projectId,
            scope.has_sequence,
            scope.sequence_id,
            scope.has_shot,
            scope.shot_id,
            scope.has_asset,
            scope.asset_id,
            append,
            std::move(payload))
            .send(caf::actor_cast<caf::actor>(handle));
    }
}

/**
 * @brief Loads every page of a catalog and mails each one to the actor.
 * @tparam Row POCO returned by `fetch`.
 * @tparam Fetch Callable taking a cursor and returning `dal::RowPage<Row>`.
 * @param weak Actor to receive the pages.
 * @param requestId Request that must still be current when the page is applied.
 * @param catalog Catalog name stored on the message.
 * @param projectId Parent project id, or 0 for the project catalog.
 * @param fetch One DAL page call.
 * @param scope Optional parents that must still match when the page is applied.
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
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
        append = true;
    }
}

/**
 * @brief One sequence node: its name, id, shot labels, and shot ids.
 * @param sequence Sequence the shots belong to.
 * @return JSON object stored in the shot tree.
 */
nlohmann::json buildSequenceNode(const poco::Sequence &sequence) {
    const std::vector<poco::Shot> shots = collectPages<poco::Shot>(
        [sequenceId = sequence.id](const std::optional<std::string> &cursor) {
            return dal::findShots(sequenceId, dal::readPageLimit(), cursor);
        });
    nlohmann::json names = nlohmann::json::array();
    nlohmann::json ids = nlohmann::json::array();
    const std::vector<std::string> labels = buildUniqueLabels(shots);
    for (std::size_t index = 0; index < shots.size(); ++index) {
        names.push_back(labels[index]);
        ids.push_back(shots[index].id);
    }
    return {
        {"name", sequence.name},
        {"id", sequence.id},
        {"shots", std::move(names)},
        {"shotIds", std::move(ids)}};
}

/**
 * @brief Sequences of one project, each with its shots.
 * @param projectId Domain project id.
 * @return JSON array the shot tree attribute stores.
 */
nlohmann::json buildShotTree(std::int64_t projectId) {
    const std::vector<poco::Sequence> sequences = collectPages<poco::Sequence>(
        [projectId](const std::optional<std::string> &cursor) {
            return dal::findSequences(projectId, dal::readPageLimit(), cursor);
        });
    nlohmann::json tree = nlohmann::json::array();
    for (const poco::Sequence &sequence : sequences) {
        tree.push_back(buildSequenceNode(sequence));
    }
    return tree;
}

/**
 * @brief One playlist node: its name, id, and version labels.
 * @param playlist Playlist the versions belong to.
 * @param projectId Domain project id.
 * @param parents Sequence, shot, and asset that also filter the versions.
 * @return JSON object stored in the playlist tree.
 */
nlohmann::json buildPlaylistNode(
    const poco::Playlist &playlist, std::int64_t projectId, const VersionParents &parents) {
    const std::vector<poco::Version> versions = collectPages<poco::Version>(
        [projectId, parents, playlistId = playlist.id](const std::optional<std::string> &cursor) {
            return dal::findVersions(
                projectId,
                dal::readPageLimit(),
                cursor,
                parents.sequenceId,
                parents.shotId,
                parents.assetId,
                playlistId);
        });
    nlohmann::json names = nlohmann::json::array();
    for (const std::string &label : buildUniqueLabels(versions)) {
        names.push_back(label);
    }
    return {{"name", playlist.name}, {"id", playlist.id}, {"versions", std::move(names)}};
}

/**
 * @brief Playlists of one project, each with the versions that match the parents.
 * @param projectId Domain project id.
 * @param parents Sequence, shot, and asset filter shared with the version list.
 * @return JSON array the playlist tree attribute stores.
 */
nlohmann::json buildPlaylistTree(std::int64_t projectId, const VersionParents &parents) {
    const std::vector<poco::Playlist> playlists = collectPages<poco::Playlist>(
        [projectId](const std::optional<std::string> &cursor) {
            return dal::findPlaylists(projectId, dal::readPageLimit(), cursor);
        });
    nlohmann::json tree = nlohmann::json::array();
    for (const poco::Playlist &playlist : playlists) {
        tree.push_back(buildPlaylistNode(playlist, projectId, parents));
    }
    return tree;
}

} // namespace

MikrosShotBrowserContext::MikrosShotBrowserContext(
    caf::actor_config &cfg, const utility::JsonStore &init_settings)
    : plugin::StandardPlugin(cfg, "MikrosShotBrowserPlugin", init_settings) {

    addPanelAttributes(); ///< Combos and trees the panel binds to.
    make_behavior();
    connect_to_ui();
    loadProjectsAsync(); ///< First catalog, before the panel asks.
    register_ui_panel_qml(
        "Mikros Shot Browser",
        R"(
            import MikrosShotBrowser 1.0
            MikrosShotBrowserPanel {
                anchors.fill: parent
            }
        )",
        100.0f,
        "qrc:/mikros_shotbrowser_icons/mikros_shotbrowser.svg",
        5.0f);
}

void MikrosShotBrowserContext::addPanelAttributes() {
    project_ = add_string_choice_attribute("Project", "Project", "", {}, {});
    project_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    sequence_ = add_string_choice_attribute("Sequence", "Sequence", "", {}, {});
    sequence_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    shot_ = add_string_choice_attribute("Shot", "Shot", "", {}, {});
    shot_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    shot_tree_ = add_json_attribute("ShotTree", "ShotTree", nlohmann::json::array());
    shot_tree_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    asset_ = add_string_choice_attribute("Asset", "Asset", "", {}, {});
    asset_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    version_ = add_string_choice_attribute("Version", "Version", "", {}, {});
    version_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    playlist_tree_ = add_json_attribute("PlaylistTree", "PlaylistTree", nlohmann::json::array());
    playlist_tree_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

std::optional<poco::Project> MikrosShotBrowserContext::findProjectByLabel(
    const std::string &label) const {
    return findRowByLabel(projects_, label);
}

std::optional<poco::Sequence> MikrosShotBrowserContext::findSequenceByLabel(
    const std::string &label) const {
    const nlohmann::json tree = shot_tree_ ? shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("name") || !node.contains("id")) {
            continue;
        }
        if (!node["name"].is_string() || node["name"].get<std::string>() != label) {
            continue;
        }
        return poco::Sequence{node["id"].get<std::int64_t>(), label};
    }
    return std::nullopt;
}

std::optional<poco::Shot> MikrosShotBrowserContext::findShotByLabel(
    const std::string &label) const {
    const nlohmann::json tree = shot_tree_ ? shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("shots") || !node.contains("shotIds")) {
            continue;
        }
        const nlohmann::json &labels = node["shots"];
        const nlohmann::json &ids = node["shotIds"];
        if (!labels.is_array() || !ids.is_array()) {
            continue;
        }
        const std::size_t count = std::min(labels.size(), ids.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (!labels[index].is_string() || labels[index].get<std::string>() != label) {
                continue;
            }
            return poco::Shot{ids[index].get<std::int64_t>(), label};
        }
    }
    return std::nullopt;
}

std::optional<poco::Sequence> MikrosShotBrowserContext::findSequenceOwningShot(
    const std::string &shotLabel) const {
    const nlohmann::json tree = shot_tree_ ? shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return std::nullopt;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("name") || !node.contains("id") ||
            !node.contains("shots")) {
            continue;
        }
        const nlohmann::json &labels = node["shots"];
        if (!labels.is_array()) {
            continue;
        }
        for (const auto &entry : labels) {
            if (!entry.is_string() || entry.get<std::string>() != shotLabel) {
                continue;
            }
            return poco::Sequence{
                node["id"].get<std::int64_t>(), node["name"].get<std::string>()};
        }
    }
    return std::nullopt;
}

std::optional<poco::Asset> MikrosShotBrowserContext::findAssetByLabel(
    const std::string &label) const {
    return findRowByLabel(assets_, label);
}

void MikrosShotBrowserContext::clearSequenceSelection() {
    const bool hadSelection = selected_sequence_.has_value() || !sequence_->value().empty();
    selected_sequence_ = std::nullopt;
    if (!sequence_->value().empty()) {
        sequence_->set_value("", false); ///< Do not re-enter the sequence handler.
    }
    if (hadSelection) {
        trace::select("sequence", false, 0, {});
    }
}

void MikrosShotBrowserContext::clearShotSelection() {
    const bool hadSelection = selected_shot_.has_value() || !shot_->value().empty();
    selected_shot_ = std::nullopt;
    if (!shot_->value().empty()) {
        shot_->set_value("", false); ///< Do not re-enter the shot handler.
    }
    if (hadSelection) {
        trace::select("shot", false, 0, {});
    }
}

void MikrosShotBrowserContext::clearAssetSelection() {
    const bool hadSelection = selected_asset_.has_value() || !asset_->value().empty();
    selected_asset_ = std::nullopt;
    if (!asset_->value().empty()) {
        asset_->set_value("", false); ///< Do not re-enter the asset handler.
    }
    if (hadSelection) {
        trace::select("asset", false, 0, {});
    }
}

void MikrosShotBrowserContext::clearSequenceBranch() {
    clearSequenceSelection(); ///< Drop the sequence before its shots.
    clearShotSelection();
}

void MikrosShotBrowserContext::resetProjectCatalog() {
    ++project_request_id_; ///< Drop pages from the previous project load.
    projects_.clear();
    selected_project_ = std::nullopt;
    project_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    project_->set_value(""); ///< Notifies, which clears the child catalogs.
}

void MikrosShotBrowserContext::loadProjectsAsync() {
    resetProjectCatalog();
    startProjectLoad(project_request_id_);
}

void MikrosShotBrowserContext::startProjectLoad(std::uint64_t requestId) {
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId]() {
        publishPages<poco::Project>(
            weak,
            requestId,
            "project",
            0,
            [](const std::optional<std::string> &cursor) {
                return dal::findProjects(dal::readPageLimit(), cursor);
            });
    }).detach();
}

void MikrosShotBrowserContext::refreshShotTree() {
    ++sequence_request_id_; ///< Drop a shot tree that is still loading.
    clearSequenceSelection();
    shot_tree_->set_value(nlohmann::json::array()); ///< Hide the previous project's tree.
    if (!selected_project_) {
        return;
    }
    startShotTreeLoad(sequence_request_id_, selected_project_->id);
}

void MikrosShotBrowserContext::startShotTreeLoad(std::uint64_t requestId, std::int64_t projectId) {
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId, projectId]() {
        const nlohmann::json tree = buildShotTree(projectId); ///< Sequences, each with its shots.
        sendCatalog(
            weak, requestId, "shot_tree", projectId, {}, false, utility::JsonStore(tree));
    }).detach();
}

void MikrosShotBrowserContext::resetAssetCatalog() {
    ++asset_request_id_; ///< Drop pages from the previous asset load.
    assets_.clear();
    selected_asset_ = std::nullopt;
    asset_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    asset_->set_value(""); ///< Notifies, which may log `select asset none`.
}

void MikrosShotBrowserContext::refreshAssets() {
    resetAssetCatalog();
    if (!selected_project_) {
        return;
    }
    startAssetLoad(asset_request_id_, selected_project_->id);
}

void MikrosShotBrowserContext::startAssetLoad(std::uint64_t requestId, std::int64_t projectId) {
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId, projectId]() {
        publishPages<poco::Asset>(
            weak,
            requestId,
            "asset",
            projectId,
            [projectId](const std::optional<std::string> &cursor) {
                return dal::findAssets(projectId, dal::readPageLimit(), cursor);
            });
    }).detach();
}

void MikrosShotBrowserContext::resetVersionCatalog() {
    ++version_request_id_; ///< Drop pages from the previous version load.
    versions_.clear();
    version_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    version_->set_value("");
}

void MikrosShotBrowserContext::refreshVersions() {
    resetVersionCatalog();
    refreshPlaylistTree(); ///< Playlists use the same parents as the version list.
    if (!selected_project_) {
        return;
    }
    startVersionLoad(version_request_id_, selected_project_->id);
}

void MikrosShotBrowserContext::startVersionLoad(std::uint64_t requestId, std::int64_t projectId) {
    const VersionParents parents =
        readVersionParents(selected_sequence_, selected_shot_, selected_asset_);
    const PageScope scope = pageScope(parents);
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId, projectId, parents, scope]() {
        publishPages<poco::Version>(
            weak,
            requestId,
            "version",
            projectId,
            [projectId, parents](const std::optional<std::string> &cursor) {
                return dal::findVersions(
                    projectId,
                    dal::readPageLimit(),
                    cursor,
                    parents.sequenceId,
                    parents.shotId,
                    parents.assetId);
            },
            scope);
    }).detach();
}

void MikrosShotBrowserContext::refreshPlaylistTree() {
    ++playlist_request_id_; ///< Drop a playlist tree that is still loading.
    playlist_tree_->set_value(nlohmann::json::array()); ///< Hide playlists from the previous selection.
    if (!selected_project_) {
        return;
    }
    startPlaylistLoad(playlist_request_id_, selected_project_->id);
}

void MikrosShotBrowserContext::startPlaylistLoad(
    std::uint64_t requestId, std::int64_t projectId) {
    const VersionParents parents =
        readVersionParents(selected_sequence_, selected_shot_, selected_asset_);
    const PageScope scope = pageScope(parents);
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId, projectId, parents, scope]() {
        const nlohmann::json tree = buildPlaylistTree(projectId, parents);
        sendCatalog(
            weak,
            requestId,
            "playlist_tree",
            projectId,
            scope,
            false,
            utility::JsonStore(tree));
    }).detach();
}

void MikrosShotBrowserContext::onProjectChosen() {
    selected_project_ = findProjectByLabel(project_->value());
    logSelection("project", selected_project_);
    refreshShotTree();    ///< Sequences and shots of this project.
    clearShotSelection(); ///< The new tree has no shot selected yet.
    refreshAssets();
    refreshVersions();
}

void MikrosShotBrowserContext::onSequenceChosen() {
    selected_sequence_ = findSequenceByLabel(sequence_->value());
    logSelection("sequence", selected_sequence_);
    if (selected_sequence_) {
        clearAssetSelection(); ///< Sequence and asset are mutually exclusive.
    }
    if (!selected_asset_) {
        clearShotSelection();
    }
    refreshVersions();
}

void MikrosShotBrowserContext::onShotChosen() {
    selected_shot_ = findShotByLabel(shot_->value());
    logSelection("shot", selected_shot_);
    if (selected_shot_) {
        clearAssetSelection(); ///< A shot belongs to the sequence branch.
        alignSequenceWithShot(shot_->value());
    }
    refreshVersions();
}

void MikrosShotBrowserContext::alignSequenceWithShot(const std::string &shotLabel) {
    const std::optional<poco::Sequence> parent = findSequenceOwningShot(shotLabel);
    if (!parent) {
        return;
    }
    const bool realign = sequence_->value() != parent->name;
    selected_sequence_ = parent;
    if (!realign) {
        return;
    }
    aligning_sequence_ = true;
    sequence_->set_value(parent->name); ///< Skip the sequence handler so the shot stays.
    aligning_sequence_ = false;
    logSelection("sequence", selected_sequence_); ///< Log after the guard is cleared.
}

void MikrosShotBrowserContext::onAssetChosen() {
    selected_asset_ = findAssetByLabel(asset_->value());
    logSelection("asset", selected_asset_);
    if (selected_asset_) {
        clearSequenceBranch(); ///< Asset and the shot tree are mutually exclusive.
    } else {
        clearShotSelection();
    }
    refreshVersions();
}

void MikrosShotBrowserContext::attribute_changed(
    const utility::Uuid &attr_uuid, const int role) {
    if (role != module::Attribute::Value) {
        plugin::StandardPlugin::attribute_changed(attr_uuid, role);
        return;
    }
    if (attr_uuid == project_->uuid()) {
        onProjectChosen();
    } else if (aligning_sequence_) {
        plugin::StandardPlugin::attribute_changed(attr_uuid, role);
        return; ///< The shot handler is aligning the sequence. Do not clear the shot.
    } else if (attr_uuid == sequence_->uuid()) {
        onSequenceChosen();
    } else if (attr_uuid == shot_->uuid()) {
        onShotChosen();
    } else if (attr_uuid == asset_->uuid()) {
        onAssetChosen();
    }
    plugin::StandardPlugin::attribute_changed(attr_uuid, role);
}

bool MikrosShotBrowserContext::projectStillSelected(std::int64_t projectId) const {
    return selected_project_ && selected_project_->id == projectId;
}

bool MikrosShotBrowserContext::versionParentsStillSelected(
    bool hasSequence,
    std::int64_t sequenceId,
    bool hasShot,
    std::int64_t shotId,
    bool hasAsset,
    std::int64_t assetId) const {
    return selectionMatches(hasSequence, sequenceId, selected_sequence_) &&
           selectionMatches(hasShot, shotId, selected_shot_) &&
           selectionMatches(hasAsset, assetId, selected_asset_);
}

void MikrosShotBrowserContext::applyProjectPage(
    std::uint64_t requestId, bool append, const utility::JsonStore &payload) {
    if (requestId != project_request_id_) {
        trace::drop("project", requestId);
        return;
    }
    applyCatalogPage(projects_, project_, rowsFromJson<poco::Project>(payload), append);
}

void MikrosShotBrowserContext::applyShotTree(
    std::uint64_t requestId, const utility::JsonStore &payload) {
    if (requestId != sequence_request_id_) {
        trace::drop("shot_tree", requestId);
        return;
    }
    shot_tree_->set_value(nlohmann::json(payload));
}

void MikrosShotBrowserContext::applyAssetPage(
    std::uint64_t requestId, bool append, const utility::JsonStore &payload) {
    if (requestId != asset_request_id_) {
        trace::drop("asset", requestId);
        return;
    }
    applyCatalogPage(assets_, asset_, rowsFromJson<poco::Asset>(payload), append);
}

void MikrosShotBrowserContext::applyPlaylistTree(
    std::uint64_t requestId,
    bool hasSequence,
    std::int64_t sequenceId,
    bool hasShot,
    std::int64_t shotId,
    bool hasAsset,
    std::int64_t assetId,
    const utility::JsonStore &payload) {
    if (requestId != playlist_request_id_) {
        trace::drop("playlist_tree", requestId);
        return;
    }
    if (!versionParentsStillSelected(
            hasSequence, sequenceId, hasShot, shotId, hasAsset, assetId)) {
        trace::drop("playlist_tree", requestId);
        return;
    }
    playlist_tree_->set_value(nlohmann::json(payload));
}

void MikrosShotBrowserContext::applyVersionPage(
    std::uint64_t requestId,
    bool hasSequence,
    std::int64_t sequenceId,
    bool hasShot,
    std::int64_t shotId,
    bool hasAsset,
    std::int64_t assetId,
    bool append,
    const utility::JsonStore &payload) {
    if (requestId != version_request_id_) {
        trace::drop("version", requestId);
        return;
    }
    if (!versionParentsStillSelected(
            hasSequence, sequenceId, hasShot, shotId, hasAsset, assetId)) {
        trace::drop("version", requestId);
        return;
    }
    applyCatalogPage(versions_, version_, rowsFromJson<poco::Version>(payload), append);
}

caf::message_handler MikrosShotBrowserContext::message_handler_extensions() {
    return caf::message_handler{
        [=](data_source::put_data_atom,
            const std::uint64_t requestId,
            const std::string &catalog,
            const std::int64_t projectId,
            const bool hasSequence,
            const std::int64_t sequenceId,
            const bool hasShot,
            const std::int64_t shotId,
            const bool hasAsset,
            const std::int64_t assetId,
            const bool append,
            const utility::JsonStore &payload) {
            if (catalog == "project") {
                applyProjectPage(requestId, append, payload);
                return;
            }
            if (!projectStillSelected(projectId)) {
                trace::drop(catalog, requestId); ///< The user picked another project.
                return;
            }
            if (catalog == "shot_tree") {
                applyShotTree(requestId, payload);
            } else if (catalog == "asset") {
                applyAssetPage(requestId, append, payload);
            } else if (catalog == "playlist_tree") {
                applyPlaylistTree(
                    requestId,
                    hasSequence,
                    sequenceId,
                    hasShot,
                    shotId,
                    hasAsset,
                    assetId,
                    payload);
            } else if (catalog == "version") {
                applyVersionPage(
                    requestId,
                    hasSequence,
                    sequenceId,
                    hasShot,
                    shotId,
                    hasAsset,
                    assetId,
                    append,
                    payload);
            }
        }};
}
