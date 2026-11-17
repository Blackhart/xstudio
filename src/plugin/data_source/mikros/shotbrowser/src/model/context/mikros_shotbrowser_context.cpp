// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_shotbrowser_context.cpp
 * @brief Async DAL loads and combo updates for the Mikros Shot Browser panel.
 */

#include "model/context/mikros_shotbrowser_context.hpp"

#include "model/constants.hpp"
#include "model/log.hpp"
#include "model/thumb_fetch.hpp"
#include "model/thumb_store.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/utility/helpers.hpp"

#include <type_traits>
#include <utility>

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
 * @brief Writes a version page, including the image source and its provider.
 * @param rows Versions to send to the actor.
 * @return JSON store the worker message can carry.
 */
template <>
utility::JsonStore rowsToJson<poco::Version>(const std::vector<poco::Version> &rows) {
    nlohmann::json out = nlohmann::json::array();
    for (const poco::Version &row : rows) {
        out.push_back(
            {{"id", row.id},
             {"name", row.name},
             {"image", row.image},
             {"image_provider", row.image_provider}});
    }
    return utility::JsonStore(out);
}

/**
 * @brief Reads a version page, including the image source and its provider.
 * @param json Payload from `rowsToJson`.
 * @return Versions, or empty when `json` is not an array.
 */
template <>
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
 * @brief Stores one page of images without blocking the actor.
 * @param weak Actor that receives the local file URLs.
 * @param projectId Project the page belongs to.
 * @param rows Versions of this page, with their image source.
 */
void scheduleThumbPage(
    const caf::weak_actor_ptr &weak,
    std::int64_t projectId,
    const std::vector<poco::Version> &rows) {
    if (rows.empty()) {
        return;
    }
    std::thread([weak, projectId, rows]() {
        const std::filesystem::path root = thumb::thumbRoot();
        nlohmann::json found = nlohmann::json::array();
        for (const poco::Version &row : rows) {
            const thumb::ThumbPlan plan =
                thumb::planThumb(root, row.image, row.image_provider, projectId, row.id);
            std::filesystem::path ready;
            if (plan.kind == thumb::ThumbKind::Ready) {
                ready = plan.path; ///< Cache hit or a file the card can open directly.
            } else if (plan.kind == thumb::ThumbKind::Fetch) {
                if (thumb::writeBytesIfMissing(plan.path, thumb::fetchJpeg(row.image))) {
                    ready = plan.path;
                }
            }
            if (ready.empty()) {
                continue;
            }
            found.push_back({{"versionId", row.id}, {"image", thumb::toFileUrl(ready)}});
        }
        if (found.empty()) {
            return;
        }
        sendCatalog(
            weak,
            0,
            "version_thumbs",
            projectId,
            {},
            false,
            utility::JsonStore(std::move(found)));
    }).detach();
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
void sendTree(const TreeMail &mail, const nlohmann::json &tree) {
    sendCatalog(
        mail.weak,
        mail.requestId,
        mail.catalog,
        mail.projectId,
        mail.scope,
        false,
        utility::JsonStore(tree));
}

/**
 * @brief Writes shot labels and ids into one sequence node.
 * @param node Sequence object already stored in the tree.
 * @param shots Shots received for that sequence so far.
 */
void writeShots(nlohmann::json &node, const std::vector<poco::Shot> &shots) {
    const std::vector<std::string> labels = buildUniqueLabels(shots);
    nlohmann::json names = nlohmann::json::array();
    nlohmann::json ids = nlohmann::json::array();
    for (std::size_t index = 0; index < shots.size(); ++index) {
        names.push_back(labels[index]);
        ids.push_back(shots[index].id);
    }
    node["shots"] = std::move(names);
    node["shotIds"] = std::move(ids);
}

/**
 * @brief Writes version labels into one playlist node.
 * @param node Playlist object already stored in the tree.
 * @param versions Versions received for that playlist so far.
 */
void writeVersions(nlohmann::json &node, const std::vector<poco::Version> &versions) {
    const std::vector<std::string> labels = buildUniqueLabels(versions);
    nlohmann::json entries = nlohmann::json::array();
    for (std::size_t index = 0; index < versions.size(); ++index) {
        entries.push_back(
            {{"name", labels[index]}, {"id", versions[index].id}, {"image", std::string()}});
    }
    node["versions"] = std::move(entries);
}

/**
 * @brief Mails one sequence's shots. The actor writes them into that node.
 * @param mail Destination. `catalog` is `sequence_shots`.
 * @param sequenceId Sequence the user unfolded.
 * @param shots Shots received for that sequence so far.
 * @param complete True on the last page.
 */
void sendSequenceShots(
    const TreeMail &mail,
    std::int64_t sequenceId,
    const std::vector<poco::Shot> &shots,
    bool complete) {
    nlohmann::json patch = {{"sequenceId", sequenceId}, {"complete", complete}};
    writeShots(patch, shots);
    sendCatalog(
        mail.weak,
        mail.requestId,
        mail.catalog,
        mail.projectId,
        mail.scope,
        false,
        utility::JsonStore(std::move(patch)));
}

/**
 * @brief Loads one sequence's shots and mails each page.
 * @param mail Destination. `catalog` is `sequence_shots`.
 * @param sequenceId Sequence the user unfolded.
 */
void publishShotsOfSequence(const TreeMail &mail, std::int64_t sequenceId) {
    std::vector<poco::Shot> shots;
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<poco::Shot> page =
            dal::findShots(sequenceId, dal::readPageLimit(), cursor);
        if (!page.rows.empty()) {
            shots.insert(shots.end(), page.rows.begin(), page.rows.end());
        }
        const bool complete = !page.next_cursor;
        if (!shots.empty() || complete) {
            sendSequenceShots(mail, sequenceId, shots, complete);
        }
        if (complete) {
            break;
        }
        cursor = page.next_cursor;
    }
}

/**
 * @brief Loads sequences and mails the tree after each page. Shots stay empty.
 * @param mail Destination and request. `catalog` is `shot_tree`.
 */
void publishSequences(const TreeMail &mail) {
    nlohmann::json tree = nlohmann::json::array();
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<poco::Sequence> page =
            dal::findSequences(mail.projectId, dal::readPageLimit(), cursor);
        for (const poco::Sequence &sequence : page.rows) {
            tree.push_back(
                {{"name", sequence.name},
                 {"id", sequence.id},
                 {"shots", nlohmann::json::array()},
                 {"shotIds", nlohmann::json::array()}});
        }
        if (!page.rows.empty()) {
            sendTree(mail, tree); ///< Show this page of sequences. Shots wait for an unfold.
        }
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
    }
}

/**
 * @brief Mails one playlist's versions. The actor writes them into that node.
 * @param mail Destination. `catalog` is `playlist_versions`.
 * @param playlistId Playlist the user unfolded.
 * @param versions Versions received for that playlist so far.
 * @param complete True on the last page.
 */
void sendPlaylistVersions(
    const TreeMail &mail,
    std::int64_t playlistId,
    const std::vector<poco::Version> &versions,
    bool complete) {
    nlohmann::json patch = {{"playlistId", playlistId}, {"complete", complete}};
    writeVersions(patch, versions);
    sendCatalog(
        mail.weak,
        mail.requestId,
        mail.catalog,
        mail.projectId,
        mail.scope,
        false,
        utility::JsonStore(std::move(patch)));
}

/**
 * @brief Loads one playlist's versions and mails each page.
 * @param mail Destination. `catalog` is `playlist_versions`.
 * @param parents Sequence, shot, and asset filter shared with the version list.
 * @param playlistId Playlist the user unfolded.
 */
void publishVersionsOfPlaylist(
    const TreeMail &mail, const VersionParents &parents, std::int64_t playlistId) {
    std::vector<poco::Version> versions;
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<poco::Version> page = dal::findVersions(
            mail.projectId,
            dal::readPageLimit(),
            cursor,
            parents.sequenceId,
            parents.shotId,
            parents.assetId,
            playlistId);
        if (!page.rows.empty()) {
            versions.insert(versions.end(), page.rows.begin(), page.rows.end());
            scheduleThumbPage(mail.weak, mail.projectId, page.rows);
        }
        const bool complete = !page.next_cursor;
        if (!versions.empty() || complete) {
            sendPlaylistVersions(mail, playlistId, versions, complete);
        }
        if (complete) {
            break;
        }
        cursor = page.next_cursor;
    }
}

/**
 * @brief Loads playlist names and mails the tree after each page.
 * @param mail Destination and request. `catalog` is `playlist_tree`.
 */
void publishPlaylistNames(const TreeMail &mail) {
    nlohmann::json tree = nlohmann::json::array();
    std::optional<std::string> cursor;
    for (;;) {
        const dal::RowPage<poco::Playlist> page =
            dal::findPlaylists(mail.projectId, dal::readPageLimit(), cursor);
        for (const poco::Playlist &playlist : page.rows) {
            tree.push_back(
                {{"name", playlist.name},
                 {"id", playlist.id},
                 {"versions", nlohmann::json::array()}});
        }
        if (!page.rows.empty()) {
            sendTree(mail, tree); ///< Names only. Versions wait for an unfold.
        }
        if (!page.next_cursor) {
            break;
        }
        cursor = page.next_cursor;
    }
}

/**
 * @brief Writes `set` or `default` after a resolved setting.
 * @param set True when the environment variable is non-empty.
 * @return The origin word.
 */
const char *settingOrigin(bool set) { return set ? "set" : "default"; }

/**
 * @brief Formats one text setting for the launch log.
 * @param name Environment variable name.
 * @param setting Resolved value.
 * @return `name=value (set)`, `name=value (default)`, or `name=unset`.
 */
std::string textSettingField(const char *name, const constants::TextSetting &setting) {
    if (!setting.set && setting.value.empty()) {
        return std::string(name) + "=unset";
    }
    return std::string(name) + "=" + setting.value + " (" + settingOrigin(setting.set) + ")";
}

/**
 * @brief Formats one numeric setting for the launch log.
 * @param name Environment variable name.
 * @param setting Resolved value.
 * @return `name=value (set)` or `name=value (default)`.
 */
std::string numberSettingField(const char *name, const constants::NumberSetting &setting) {
    return std::string(name) + "=" + std::to_string(setting.value) + " (" +
           settingOrigin(setting.set) + ")";
}

/**
 * @brief Formats the ShotGrid script key without printing the secret.
 * @return `SHOTGRID_CLIENT_SECRET=unset`, or the redacted key marked `set`.
 */
std::string secretSettingField() {
    const constants::TextSetting secret = constants::shotGridClientSecret();
    if (!secret.set) {
        return std::string(constants::kShotGridClientSecret) + "=unset";
    }
    return std::string(constants::kShotGridClientSecret) + "=" + constants::keyHint(secret.value) +
           " (set)";
}

/**
 * @brief Logs every Shot Browser setting and whether it came from the environment.
 */
void logSettings() {
    trace::info(
        std::string("env ") + textSettingField(constants::kBackend, constants::backend()) + " " +
        numberSettingField(constants::kCacheTtlSeconds, constants::cacheTtlSeconds()) + " " +
        numberSettingField(constants::kCacheBytes, constants::cacheMaxBytes()) + " " +
        numberSettingField(constants::kPageSize, constants::pageSize()) + " " +
        textSettingField(constants::kSchemaPath, constants::schemaPath()) + " " +
        textSettingField(constants::kShotGridUrl, constants::shotGridUrl()) + " " +
        textSettingField(constants::kShotGridClientId, constants::shotGridClientId()) + " " +
        textSettingField(constants::kShotGridMapping, constants::shotGridMapping()) + " " +
        secretSettingField());
}

/**
 * @brief Logs every path the Shot Browser writes.
 */
void logWriteLocations() {
    const std::string preferences = utility::preference_path_context("PLUGIN");
    const std::filesystem::path root = thumb::thumbRoot();
    if (root.empty()) {
        trace::info(
            "writes preferences " + preferences +
            " (browser_mode, result_view, left_pane_width); thumbnail directory unavailable "
            "because HOME is not set");
        return;
    }
    trace::info(
        "writes preferences " + preferences +
        " (browser_mode, result_view, left_pane_width); thumbnails " + root.string());
}

} // namespace

MikrosShotBrowserContext::MikrosShotBrowserContext(
    caf::actor_config &cfg, const utility::JsonStore &init_settings)
    : plugin::StandardPlugin(cfg, "MikrosShotBrowserPlugin", init_settings) {

    addPanelAttributes(); ///< Combos and trees the panel binds to.
    logSettings();       ///< Each environment variable, and whether it is set or default.
    logWriteLocations(); ///< Preferences file and thumbnail directory, resolved now.
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

    versions_loaded_ = add_boolean_attribute("VersionsLoaded", "VersionsLoaded", false);
    versions_loaded_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    version_cards_ = add_json_attribute("VersionCards", "VersionCards", nlohmann::json::array());
    version_cards_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    playlist_tree_ = add_json_attribute("PlaylistTree", "PlaylistTree", nlohmann::json::array());
    playlist_tree_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    browser_mode_ = add_string_attribute("BrowserMode", "BrowserMode", "shot");
    browser_mode_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    result_view_ = add_string_attribute("ResultView", "ResultView", "versions");
    result_view_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    expanded_sequence_ = add_string_attribute("ExpandedSequence", "ExpandedSequence", "");
    expanded_sequence_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");

    expanded_playlist_ = add_string_attribute("ExpandedPlaylist", "ExpandedPlaylist", "");
    expanded_playlist_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
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
    sequences_ready_ = selected_project_.has_value();
    clearSequenceSelection();
    shot_tree_->set_value(nlohmann::json::array()); ///< Hide the previous project's tree.
    if (!selected_project_) {
        return;
    }
    startShotTreeLoad(sequence_request_id_, selected_project_->id);
}

void MikrosShotBrowserContext::startSequenceShotLoad(
    std::uint64_t requestId, std::int64_t projectId, std::int64_t sequenceId) {
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId, projectId, sequenceId]() {
        TreeMail mail;
        mail.weak = weak;
        mail.requestId = requestId;
        mail.catalog = "sequence_shots";
        mail.projectId = projectId;
        publishShotsOfSequence(mail, sequenceId);
    }).detach();
}

bool MikrosShotBrowserContext::sequenceShotsReady(std::int64_t sequenceId) const {
    const nlohmann::json tree = shot_tree_ ? shot_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return false;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != sequenceId) {
            continue;
        }
        return node.value("shotsLoaded", false);
    }
    return false;
}

void MikrosShotBrowserContext::startShotTreeLoad(std::uint64_t requestId, std::int64_t projectId) {
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId, projectId]() {
        TreeMail mail;
        mail.weak = weak;
        mail.requestId = requestId;
        mail.catalog = "shot_tree";
        mail.projectId = projectId;
        publishSequences(mail); ///< Sequence names only. Shots wait for an unfold.
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
    assets_ready_ = selected_project_.has_value();
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
    versions_loaded_->set_value(false); ///< An empty combo is a load in progress, not a result.
    if (version_cards_) {
        version_cards_->set_value(nlohmann::json::array());
    }
    version_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    version_->set_value("");
}

void MikrosShotBrowserContext::refreshVersions() {
    resetVersionCatalog();
    if (!selected_project_) {
        return;
    }
    startVersionLoad(version_request_id_, selected_project_->id);
    reloadExpandedPlaylist(); ///< One open playlist follows the new version filter.
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
    playlists_ready_ = selected_project_.has_value();
    playlist_tree_->set_value(nlohmann::json::array()); ///< Hide playlists from the previous selection.
    if (!selected_project_) {
        return;
    }
    startPlaylistLoad(playlist_request_id_, selected_project_->id);
}

void MikrosShotBrowserContext::startPlaylistVersionLoad(
    std::uint64_t requestId, std::int64_t projectId, std::int64_t playlistId) {
    const VersionParents parents =
        readVersionParents(selected_sequence_, selected_shot_, selected_asset_);
    const PageScope scope = pageScope(parents);
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId, projectId, playlistId, parents, scope]() {
        TreeMail mail;
        mail.weak = weak;
        mail.requestId = requestId;
        mail.catalog = "playlist_versions";
        mail.projectId = projectId;
        mail.scope = scope;
        publishVersionsOfPlaylist(mail, parents, playlistId);
    }).detach();
}

bool MikrosShotBrowserContext::playlistVersionsReady(std::int64_t playlistId) const {
    const nlohmann::json tree = playlist_tree_ ? playlist_tree_->value() : nlohmann::json::array();
    if (!tree.is_array()) {
        return false;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != playlistId) {
            continue;
        }
        return node.value("versionsLoaded", false);
    }
    return false;
}

void MikrosShotBrowserContext::startPlaylistLoad(
    std::uint64_t requestId, std::int64_t projectId) {
    const caf::weak_actor_ptr weak = caf::actor_cast<caf::weak_actor_ptr>(this);
    std::thread([weak, requestId, projectId]() {
        TreeMail mail;
        mail.weak = weak;
        mail.requestId = requestId;
        mail.catalog = "playlist_tree";
        mail.projectId = projectId;
        publishPlaylistNames(mail); ///< Playlist names only. Versions wait for an unfold.
    }).detach();
}

void MikrosShotBrowserContext::clearAssetCatalog() {
    ++asset_request_id_; ///< Drop asset pages still loading for the previous project.
    assets_.clear();
    assets_ready_ = false;
    asset_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    clearAssetSelection();
}

void MikrosShotBrowserContext::clearShotTree() {
    ++sequence_request_id_; ///< Drop sequence and shot pages for the previous project.
    sequences_ready_ = false;
    clearSequenceSelection();
    clearShotSelection();
    shot_tree_->set_value(nlohmann::json::array());
}

void MikrosShotBrowserContext::onProjectChosen() {
    selected_project_ = findProjectByLabel(project_->value());
    logSelection("project", selected_project_);
    sequences_ready_ = false;
    assets_ready_ = false;
    playlists_ready_ = false;
    expanded_sequence_->set_value("", false); ///< Do not fetch shots for the previous project.
    expanded_playlist_->set_value("", false);
    if (browser_mode_->value() == "asset") {
        clearShotTree();
        refreshAssets(); ///< The asset tab is what the user is looking at.
    } else {
        clearAssetCatalog();
        refreshShotTree(); ///< Sequences only.
        clearShotSelection();
    }
    refreshVersions();
    if (result_view_->value() == "playlist") {
        refreshPlaylistTree(); ///< Names only, because that view is open.
    } else {
        playlist_tree_->set_value(nlohmann::json::array());
    }
}

void MikrosShotBrowserContext::onBrowserModeChosen() {
    if (!selected_project_) {
        return;
    }
    if (browser_mode_->value() == "asset") {
        if (!assets_ready_) {
            refreshAssets();
        }
        return;
    }
    if (!sequences_ready_) {
        refreshShotTree();
    }
}

void MikrosShotBrowserContext::onResultViewChosen() {
    if (result_view_->value() != "playlist" || !selected_project_ || playlists_ready_) {
        return;
    }
    refreshPlaylistTree();
}

void MikrosShotBrowserContext::onSequenceExpanded() {
    const std::string name = expanded_sequence_->value();
    if (name.empty() || !selected_project_) {
        return;
    }
    const std::optional<poco::Sequence> sequence = findSequenceByLabel(name);
    if (!sequence) {
        return;
    }
    if (!sequenceShotsReady(sequence->id)) {
        startSequenceShotLoad(sequence_request_id_, selected_project_->id, sequence->id);
    }
    if (!selected_sequence_ || selected_sequence_->id != sequence->id) {
        sequence_->set_value(sequence->name); ///< Versions of the sequence the user opened.
    }
}

void MikrosShotBrowserContext::onPlaylistExpanded() {
    const std::string name = expanded_playlist_->value();
    if (name.empty() || !selected_project_) {
        return;
    }
    const nlohmann::json tree = playlist_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    for (const auto &node : tree) {
        if (!node.is_object() || node.value("name", std::string()) != name || !node.contains("id")) {
            continue;
        }
        const std::int64_t playlistId = node["id"].get<std::int64_t>();
        if (!playlistVersionsReady(playlistId)) {
            startPlaylistVersionLoad(playlist_request_id_, selected_project_->id, playlistId);
        }
        return;
    }
}

void MikrosShotBrowserContext::reloadExpandedPlaylist() {
    if (!result_view_ || result_view_->value() != "playlist" || !selected_project_) {
        return;
    }
    const std::string name = expanded_playlist_->value();
    if (name.empty()) {
        return;
    }
    nlohmann::json tree = playlist_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    for (auto &node : tree) {
        if (!node.is_object() || node.value("name", std::string()) != name || !node.contains("id")) {
            continue;
        }
        node["versionsLoaded"] = false;
        const std::int64_t playlistId = node["id"].get<std::int64_t>();
        playlist_tree_->set_value(tree);
        startPlaylistVersionLoad(playlist_request_id_, selected_project_->id, playlistId);
        return;
    }
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
    } else if (attr_uuid == browser_mode_->uuid()) {
        onBrowserModeChosen();
    } else if (attr_uuid == result_view_->uuid()) {
        onResultViewChosen();
    } else if (attr_uuid == expanded_sequence_->uuid()) {
        onSequenceExpanded();
    } else if (attr_uuid == expanded_playlist_->uuid()) {
        onPlaylistExpanded();
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

void MikrosShotBrowserContext::applySequenceShots(
    std::uint64_t requestId, const utility::JsonStore &payload) {
    if (requestId != sequence_request_id_) {
        trace::drop("sequence_shots", requestId);
        return;
    }
    const nlohmann::json patch = nlohmann::json(payload);
    if (!patch.contains("sequenceId")) {
        return;
    }
    nlohmann::json tree = shot_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    const std::int64_t sequenceId = patch["sequenceId"].get<std::int64_t>();
    for (auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != sequenceId) {
            continue;
        }
        node["shots"] = patch["shots"];
        node["shotIds"] = patch["shotIds"];
        if (patch.value("complete", false)) {
            node["shotsLoaded"] = true;
        }
        shot_tree_->set_value(tree);
        return;
    }
}

void MikrosShotBrowserContext::applyPlaylistVersions(
    std::uint64_t requestId, const utility::JsonStore &payload) {
    if (requestId != playlist_request_id_) {
        trace::drop("playlist_versions", requestId);
        return;
    }
    const nlohmann::json patch = nlohmann::json(payload);
    if (!patch.contains("playlistId")) {
        return;
    }
    nlohmann::json tree = playlist_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    const std::int64_t playlistId = patch["playlistId"].get<std::int64_t>();
    for (auto &node : tree) {
        if (!node.is_object() || !node.contains("id") || node["id"].get<std::int64_t>() != playlistId) {
            continue;
        }
        node["versions"] = patch["versions"];
        if (patch.value("complete", false)) {
            node["versionsLoaded"] = true;
        }
        playlist_tree_->set_value(tree);
        stampPlaylistThumbs(); ///< A file stored earlier can show with this page.
        return;
    }
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
    bool /*hasSequence*/,
    std::int64_t /*sequenceId*/,
    bool /*hasShot*/,
    std::int64_t /*shotId*/,
    bool /*hasAsset*/,
    std::int64_t /*assetId*/,
    const utility::JsonStore &payload) {
    if (requestId != playlist_request_id_) {
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
    versions_loaded_->set_value(true); ///< Zero rows is a finished load.
    publishVersionCards();
}

std::string MikrosShotBrowserContext::thumbFor(std::int64_t projectId, std::int64_t versionId) const {
    const auto found = thumb_paths_.find({projectId, versionId});
    if (found == thumb_paths_.end()) {
        return {};
    }
    return found->second;
}

void MikrosShotBrowserContext::rememberThumbs(
    std::int64_t projectId, const utility::JsonStore &payload) {
    if (!payload.is_array()) {
        return;
    }
    for (const auto &entry : payload) {
        if (!entry.contains("versionId") || !entry.contains("image")) {
            continue;
        }
        const std::string image = entry["image"].get<std::string>();
        if (image.empty()) {
            continue;
        }
        thumb_paths_[{projectId, entry["versionId"].get<std::int64_t>()}] = image;
    }
}

void MikrosShotBrowserContext::publishVersionCards() {
    if (!version_cards_) {
        return;
    }
    nlohmann::json cards = nlohmann::json::array();
    if (selected_project_) {
        const std::vector<std::string> labels = buildUniqueLabels(versions_);
        for (std::size_t index = 0; index < versions_.size(); ++index) {
            cards.push_back(
                {{"name", labels[index]},
                 {"image", thumbFor(selected_project_->id, versions_[index].id)}});
        }
    }
    version_cards_->set_value(std::move(cards));
}

void MikrosShotBrowserContext::stampPlaylistThumbs() {
    if (!playlist_tree_ || !selected_project_) {
        return;
    }
    nlohmann::json tree = playlist_tree_->value();
    if (!tree.is_array()) {
        return;
    }
    bool changed = false;
    for (auto &node : tree) {
        if (!node.is_object() || !node.contains("versions") || !node["versions"].is_array()) {
            continue;
        }
        for (auto &entry : node["versions"]) {
            if (!entry.is_object() || !entry.contains("id")) {
                continue;
            }
            const std::string image =
                thumbFor(selected_project_->id, entry["id"].get<std::int64_t>());
            if (entry.value("image", std::string()) == image) {
                continue;
            }
            entry["image"] = image;
            changed = true;
        }
    }
    if (changed) {
        playlist_tree_->set_value(std::move(tree));
    }
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
            if (catalog == "version_thumbs") {
                rememberThumbs(projectId, payload); ///< Keep the file even if the filter moved on.
                if (projectStillSelected(projectId)) {
                    publishVersionCards();
                    stampPlaylistThumbs();
                }
                return;
            }
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
            } else if (catalog == "sequence_shots") {
                applySequenceShots(requestId, payload);
            } else if (catalog == "playlist_versions") {
                applyPlaylistVersions(requestId, payload);
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
