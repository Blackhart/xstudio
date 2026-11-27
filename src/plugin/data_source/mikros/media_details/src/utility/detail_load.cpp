// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/detail_load.cpp
 * @brief Detached media-detail actor: light version find, then parents and slow fields.
 */

#include "utility/detail_load.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "model/poco/asset_detail.hpp"
#include "model/poco/project_detail.hpp"
#include "model/poco/sequence_detail.hpp"
#include "model/poco/shot_detail.hpp"
#include "model/poco/version_detail.hpp"

#include "xstudio/atoms.hpp"

#include <caf/event_based_actor.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trace = xstudio::mikros_media_details::log;

using namespace xstudio;
using namespace xstudio::mikros_media_details;

namespace xstudio::mikros_media_details {
namespace {

/**
 * @brief Parent sections filled by the parent walk.
 */
struct ParentDetails {
    nlohmann::json project = nlohmann::json::object(); ///< Project section.
    nlohmann::json sequence = nlohmann::json::object(); ///< Sequence section when present.
    nlohmann::json shot = nlohmann::json::object();     ///< Shot section when present.
    nlohmann::json asset = nlohmann::json::object();    ///< Asset section when present.
    std::int64_t project_id = 0; ///< Resolved project id for thumb cache.
    std::int64_t sequence_id = 0; ///< Sequence whose shot count is loaded later.
};

/**
 * @brief Builds a project JSON object when the id is set.
 * @param project Loaded project, or nullopt.
 * @param fallbackId Id from the version when the project find failed.
 * @param fallbackName Name from the version when the project find failed.
 * @return `{id,name,code,status,type}` or empty object.
 */
nlohmann::json projectJson(
    const std::optional<poco::ProjectDetail> &project,
    std::int64_t fallbackId,
    const std::string &fallbackName) {
    if (project) {
        return nlohmann::json{
            {"id", project->id},
            {"name", project->name},
            {"code", project->code},
            {"status", project->status},
            {"type", project->type}};
    }
    if (fallbackId <= 0) {
        return nlohmann::json::object();
    }
    return nlohmann::json{
        {"id", fallbackId},
        {"name", fallbackName},
        {"code", ""},
        {"status", ""},
        {"type", ""}};
}

/**
 * @brief Builds a named `{id,name}` object when the id is set.
 * @param id Entity id.
 * @param name Entity name.
 * @return Object or empty object.
 */
nlohmann::json namedObject(std::int64_t id, const std::string &name) {
    if (id <= 0) {
        return nlohmann::json::object();
    }
    return nlohmann::json{{"id", id}, {"name", name}};
}

/**
 * @brief Builds the sequence JSON object from a loaded row.
 * @param row Loaded sequence detail.
 * @return Sequence section for QML.
 */
nlohmann::json sequenceJson(const poco::SequenceDetail &row) {
    return nlohmann::json{
        {"id", row.id},
        {"name", row.name},
        {"description", row.description},
        {"status", row.status},
        {"cutDurationInSec", row.cut_duration_in_sec},
        {"estimatedFramesCount", row.estimated_frames_count},
        {"estimatedShotsCount", row.estimated_shots_count},
        {"shotCount", row.shot_count},
        {"sequenceOrder", row.sequence_order}};
}

/**
 * @brief Builds the shot JSON object from a loaded row.
 * @param row Loaded shot detail.
 * @return Shot section for QML.
 */
nlohmann::json shotJson(const poco::ShotDetail &row) {
    return nlohmann::json{
        {"id", row.id},
        {"name", row.name},
        {"description", row.description},
        {"status", row.status},
        {"cutDurationInSec", row.cut_duration_in_sec},
        {"cutIn", row.cut_in},
        {"cutOut", row.cut_out},
        {"cutOrder", row.cut_order}};
}

/**
 * @brief Builds the asset JSON object from a loaded row.
 * @param row Loaded asset detail.
 * @return Asset section for QML.
 */
nlohmann::json assetJson(const poco::AssetDetail &row) {
    return nlohmann::json{
        {"id", row.id},
        {"name", row.name},
        {"description", row.description},
        {"status", row.status}};
}

/**
 * @brief Builds the playlists JSON array from version links.
 * @param playlists Playlist id/name pairs.
 * @return Array of `{id,name,type}` objects.
 */
nlohmann::json playlistsJson(const std::vector<poco::PlaylistLink> &playlists) {
    nlohmann::json list = nlohmann::json::array();
    for (const poco::PlaylistLink &link : playlists) {
        if (link.id <= 0) {
            continue;
        }
        list.push_back({{"id", link.id}, {"name", link.name}, {"type", link.type}});
    }
    return list;
}

/**
 * @brief Mails one catalog payload to the panel, or does nothing when gone.
 * @param weak Panel actor.
 * @param requestId Generation of the load.
 * @param catalog `version_detail` or `version_detail_error`.
 * @param payload JSON for the panel.
 */
void sendDetail(
    const caf::weak_actor_ptr &weak,
    std::uint64_t requestId,
    const std::string &catalog,
    utility::JsonStore payload) {
    if (auto handle = weak.lock()) {
        anon_mail(
            data_source::put_data_atom_v,
            requestId,
            catalog,
            std::move(payload))
            .send(caf::actor_cast<caf::actor>(handle));
    }
}

/**
 * @brief Loads one version row by id, without the slow ShotGrid fields.
 * @param versionId ShotGrid version id.
 * @return The row when found.
 */
std::optional<poco::VersionDetail> loadVersionDetailsFromId(std::int64_t versionId) {
    return dal::findVersionDetailById(versionId);
}

/**
 * @brief Fills parent sections from the links already on the version.
 * @param version Loaded version row.
 * @return `{id,name}` sections. The full parent walk replaces them later.
 */
ParentDetails linkParentsFromVersion(const poco::VersionDetail &version) {
    ParentDetails parents;
    parents.project_id = version.project_id;

    const std::string &entityType = version.entity_type;
    if (entityType == "Shot") {
        parents.shot = namedObject(version.entity_id, version.entity_name);
    } else if (entityType == "Asset") {
        parents.asset = namedObject(version.entity_id, version.entity_name);
    } else if (entityType == "Sequence") {
        parents.sequence = namedObject(version.entity_id, version.entity_name);
    } else if (entityType == "Project" && version.entity_id > 0) {
        parents.project_id = version.entity_id; ///< The version entity is the project.
    }

    const std::string &projectName = (entityType == "Project" && version.project_name.empty())
                                          ? version.entity_name
                                          : version.project_name;
    parents.project = namedObject(parents.project_id, projectName);
    return parents;
}

/**
 * @brief Loads one project section by id.
 * @param projectId Domain project id.
 * @param fallbackName Name used when the find fails but the id is known.
 * @return Project JSON object.
 */
nlohmann::json loadProjectDetailsFromId(
    std::int64_t projectId, const std::string &fallbackName) {
    if (projectId <= 0) {
        return nlohmann::json::object();
    }
    return projectJson(dal::findProjectDetailById(projectId), projectId, fallbackName);
}

/**
 * @brief Loads sequence details and returns the linked project id.
 * @param sequenceId Domain sequence id.
 * @param fallbackName Name when the find fails.
 * @param outSequence Filled with `{id,name}` when known.
 * @return Linked project id, or 0.
 */
std::int64_t loadSequenceDetailsFromId(
    std::int64_t sequenceId,
    const std::string &fallbackName,
    nlohmann::json &outSequence) {
    if (sequenceId <= 0) {
        return 0;
    }
    if (const auto row = dal::findSequenceDetailById(sequenceId)) {
        outSequence = sequenceJson(*row);
        return row->project_id;
    }
    outSequence = namedObject(sequenceId, fallbackName);
    return 0;
}

/**
 * @brief Loads shot details and walks to sequence then project.
 * @param shotId Domain shot id.
 * @param fallbackName Name when the find fails.
 * @param parents Parent sections to fill.
 */
void loadShotParentDetailsFromId(
    std::int64_t shotId, const std::string &fallbackName, ParentDetails &parents) {
    if (shotId <= 0) {
        return;
    }
    if (const auto shotRow = dal::findShotDetailById(shotId)) {
        parents.shot = shotJson(*shotRow);
        parents.sequence_id = shotRow->sequence_id; ///< Shot count waits for the extra read.
        const std::int64_t projectId = loadSequenceDetailsFromId(
            shotRow->sequence_id, shotRow->sequence_name, parents.sequence);
        if (projectId > 0) {
            parents.project_id = projectId;
        }
        return;
    }
    parents.shot = namedObject(shotId, fallbackName);
}

/**
 * @brief Loads asset details and walks to project.
 * @param assetId Domain asset id.
 * @param fallbackName Name when the find fails.
 * @param parents Parent sections to fill.
 */
void loadAssetParentDetailsFromId(
    std::int64_t assetId, const std::string &fallbackName, ParentDetails &parents) {
    if (assetId <= 0) {
        return;
    }
    if (const auto assetRow = dal::findAssetDetailById(assetId)) {
        parents.asset = assetJson(*assetRow);
        if (assetRow->project_id > 0) {
            parents.project_id = assetRow->project_id;
        }
        return;
    }
    parents.asset = namedObject(assetId, fallbackName);
}

/**
 * @brief Loads parents from the version entity type.
 * @param version Loaded version row.
 * @return Parent sections and resolved project id.
 */
ParentDetails loadParentDetailsFromVersion(const poco::VersionDetail &version) {
    ParentDetails parents;
    parents.project_id = version.project_id;

    const std::string &entityType = version.entity_type;
    if (entityType == "Shot") {
        loadShotParentDetailsFromId(version.entity_id, version.entity_name, parents);
    } else if (entityType == "Asset") {
        loadAssetParentDetailsFromId(version.entity_id, version.entity_name, parents);
    } else if (entityType == "Sequence") {
        parents.sequence_id = version.entity_id; ///< Shot count waits for the extra read.
        const std::int64_t projectId = loadSequenceDetailsFromId(
            version.entity_id, version.entity_name, parents.sequence);
        if (projectId > 0) {
            parents.project_id = projectId;
        }
    } else if (entityType == "Project") {
        if (version.entity_id > 0) {
            parents.project_id = version.entity_id;
        }
    }

    parents.project = loadProjectDetailsFromId(parents.project_id, version.project_name);
    return parents;
}

/**
 * @brief Builds the version / status / team / files sections.
 * @param version Loaded version row.
 * @return Partial DetailJson without parents or media.
 */
nlohmann::json versionSectionsJson(const poco::VersionDetail &version) {
    return nlohmann::json{
        {"version",
         {{"id", version.id},
          {"name", version.name},
          {"step", version.step},
          {"task", version.task},
          {"createdAt", version.created_at},
          {"description", version.description},
          {"author", version.author},
          {"clientCode", version.client_code},
          {"clientReviewed", version.client_reviewed},
          {"clientApproved", version.client_approved},
          {"isLatest", version.is_latest},
          {"versionNumber", version.version_number},
          {"frameRange", version.frame_range},
          {"frameCount", version.frame_count},
          {"workSourceScene", version.work_source_scene},
          {"playlists", playlistsJson(version.playlists)}}},
        {"status",
         {{"technical", version.status}, {"artistic", version.artistic_status}}},
        {"team", {{"department", version.step}, {"artist", version.author}}},
        {"files",
         {{"frames", version.frames},
          {"movie", version.movie},
          {"uploadedMovie", version.uploaded_movie},
          {"image", ""}, ///< `file://` filled later by thumb resolve.
          {"sites", version.sites},
          {"syncedToClient", version.synced_to_client},
          {"openNotesCount", version.open_notes_count}}}};
}

/**
 * @brief Assembles the full DetailJson from version, parents, and media.
 * @param version Loaded version row.
 * @param parents Parent sections.
 * @param mediaDetails Media section from the playhead.
 * @return Full DetailJson object for QML, including `_thumb` helper.
 */
nlohmann::json assembleDetailJson(
    const poco::VersionDetail &version,
    const ParentDetails &parents,
    const nlohmann::json &mediaDetails) {
    nlohmann::json payload = versionSectionsJson(version);
    payload["project"] = parents.project;
    payload["sequence"] = parents.sequence;
    payload["shot"] = parents.shot;
    payload["asset"] = parents.asset;
    payload["media"] = mediaDetails.is_object() ? mediaDetails : nlohmann::json::object();
    payload["_thumb"] = {
        {"projectId", parents.project_id},
        {"versionId", version.id},
        {"image", version.image},
        {"imageProvider", version.image_provider}};
    return payload;
}

/**
 * @brief Builds the parent sections mailed after the panel is showing.
 * @param parents Full parent walk.
 * @return `{project, sequence, shot, asset}`.
 */
nlohmann::json parentSectionsJson(const ParentDetails &parents) {
    return nlohmann::json{
        {"project", parents.project},
        {"sequence", parents.sequence},
        {"shot", parents.shot},
        {"asset", parents.asset}};
}

/**
 * @brief Builds the slow fields mailed after the parent sections.
 * @param extra Summary fields, sites, and playlists.
 * @param shotCount Sequence shot count when a sequence was resolved.
 * @return Partial sections. Absent shot count omits `sequence`.
 */
nlohmann::json extraSectionsJson(
    const poco::VersionDetailExtra &extra, const std::optional<std::int64_t> &shotCount) {
    nlohmann::json payload = nlohmann::json{
        {"version",
         {{"versionNumber", extra.version_number},
          {"playlists", playlistsJson(extra.playlists)}}},
        {"files", {{"sites", extra.sites}, {"openNotesCount", extra.open_notes_count}}}};
    if (shotCount) {
        payload["sequence"] = {{"shotCount", *shotCount}}; ///< Overlay keeps the other sequence fields.
    }
    return payload;
}

/**
 * @brief Loads full parents and mails them. A failure leaves the link names on screen.
 * @param request Panel and request id of the load already showing.
 * @param version Loaded version row.
 * @return Sequence id for the shot-count read, or 0 when none was resolved.
 */
std::int64_t publishParentDetails(
    const MediaDetailLoadRequest &request, const poco::VersionDetail &version) {
    try {
        const ParentDetails parents = loadParentDetailsFromVersion(version);
        sendDetail(
            request.panel,
            request.request_id,
            "version_detail_parents",
            utility::JsonStore(parentSectionsJson(parents)));
        return parents.sequence_id;
    } catch (const std::exception &error) {
        log::failure(error); ///< The panel stays ready with the link names.
        return 0;
    }
}

/**
 * @brief Loads slow version fields and the sequence shot count, then mails them.
 * @param request Panel and request id of the load already showing.
 * @param versionId Version already on screen.
 * @param sequenceId Sequence from the parent walk. 0 skips the shot count.
 */
void publishExtraDetails(
    const MediaDetailLoadRequest &request, std::int64_t versionId, std::int64_t sequenceId) {
    try {
        nlohmann::json payload = nlohmann::json::object();
        if (const auto extra = dal::findVersionDetailExtraById(versionId)) {
            std::optional<std::int64_t> shotCount;
            if (sequenceId > 0) {
                shotCount = dal::findSequenceShotCountById(sequenceId); ///< Summary, after the section.
            }
            payload = extraSectionsJson(*extra, shotCount);
        } else if (sequenceId > 0) {
            if (const auto shotCount = dal::findSequenceShotCountById(sequenceId)) {
                payload = {{"sequence", {{"shotCount", *shotCount}}}};
            }
        }
        if (payload.empty()) {
            return; ///< Nothing new to merge. The panel keeps the light payload.
        }
        sendDetail(
            request.panel,
            request.request_id,
            "version_detail_extra",
            utility::JsonStore(std::move(payload)));
    } catch (const std::exception &error) {
        log::failure(error); ///< The panel stays ready without the slow fields.
    }
}

std::mutex parkedMutex; ///< Serializes park and take. The actor runs on another thread.
std::uint64_t nextToken = 0; ///< Zero is never issued. A missing take stays distinguishable.
std::unordered_map<std::uint64_t, std::shared_ptr<MediaDetailLoadRequest>> parked;

/**
 * @brief Stores one detail request and returns the token mailed to its actor.
 * @param request Version id, request id, and media JSON.
 * @return Token the actor passes to `take`.
 */
std::uint64_t park(MediaDetailLoadRequest request) {
    std::lock_guard<std::mutex> lock(parkedMutex); ///< The actor may take while another load parks.
    const std::uint64_t token = ++nextToken; ///< uint64_t is registered before the actor system starts.
    parked.emplace(token, std::make_shared<MediaDetailLoadRequest>(std::move(request)));
    return token;
}

/**
 * @brief Removes one parked detail request.
 * @param token Value mailed to the actor.
 * @return The request, or empty when the token was never parked.
 */
std::shared_ptr<MediaDetailLoadRequest> take(std::uint64_t token) {
    std::lock_guard<std::mutex> lock(parkedMutex); ///< park may run on the panel thread.
    const auto found = parked.find(token);
    if (found == parked.end()) {
        return {}; ///< The actor quits when the token is unknown.
    }
    auto request = std::move(found->second);
    parked.erase(found);
    return request;
}

} // namespace

void launchMediaDetailLoad(caf::actor_system &system, MediaDetailLoadRequest request) {
    const std::uint64_t token = park(std::move(request)); ///< The struct never enters a CAF message.
    auto worker = system.spawn<MediaDetailLoadActor, caf::detached>();
    anon_mail(token).send(worker);
}

void executeMediaDetailLoad(const MediaDetailLoadRequest &request) {
    nlohmann::json payload = nlohmann::json::object();
    std::string catalog = "version_detail";
    std::optional<poco::VersionDetail> version;
    try {
        version = loadVersionDetailsFromId(request.version_id); ///< Scalars and links only.
        if (!version) {
            catalog = "version_detail_error";
            payload = {{"message", "Version not found"}, {"id", request.version_id}};
        } else {
            const ParentDetails links = linkParentsFromVersion(*version); ///< Names before the walk.
            const nlohmann::json &mediaDetails = request.media_details;
            payload = assembleDetailJson(*version, links, mediaDetails);
        }
    } catch (const std::exception &error) {
        catalog = "version_detail_error";
        payload = {{"message", error.what()}, {"id", request.version_id}};
        version.reset(); ///< A failed first read has nothing to enrich.
    }
    sendDetail(
        request.panel,
        request.request_id,
        catalog,
        utility::JsonStore(std::move(payload)));
    if (!version) {
        return; ///< Parents and slow fields wait until the panel is showing this version.
    }
    const std::int64_t sequenceId = publishParentDetails(request, *version);
    publishExtraDetails(request, version->id, sequenceId);
}

MediaDetailLoadActor::MediaDetailLoadActor(caf::actor_config &cfg)
    : caf::event_based_actor(cfg) {}

const char *MediaDetailLoadActor::name() const { return "MediaDetailLoadActor"; }

caf::behavior MediaDetailLoadActor::make_behavior() {
    return {
        [this](std::uint64_t token) {
            const std::shared_ptr<MediaDetailLoadRequest> request = take(token);
            if (!request) {
                quit(); ///< A missing token has nothing to load.
                return;
            }
            try {
                executeMediaDetailLoad(*request); ///< Blocking finds stay on this detached thread.
            } catch (const std::exception &caught) {
                log::failure(caught); ///< A failed load must not take the panel down.
            }
            quit();
        },
    };
}

} // namespace xstudio::mikros_media_details
