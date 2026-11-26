// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/detail_load.cpp
 * @brief Detached media-detail actor: version find, parent walk, panel mail.
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

#include <optional>
#include <string>
#include <utility>

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
 * @brief Loads one version row by id.
 * @param versionId ShotGrid version id.
 * @return The row when found.
 */
std::optional<poco::VersionDetail> loadVersionDetailsFromId(std::int64_t versionId) {
    return dal::findVersionDetailById(versionId);
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
        outSequence = namedObject(row->id, row->name);
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
        parents.shot = namedObject(shotRow->id, shotRow->name);
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
        parents.asset = namedObject(assetRow->id, assetRow->name);
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
          {"createdAt", version.created_at},
          {"description", version.description},
          {"author", version.author},
          {"frameRange", version.frame_range}}},
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

} // namespace

void executeMediaDetailLoad(const MediaDetailLoadRequest &request) {
    nlohmann::json payload = nlohmann::json::object();
    std::string catalog = "version_detail";
    try {
        const std::optional<poco::VersionDetail> version =
            loadVersionDetailsFromId(request.version_id);
        if (!version) {
            catalog = "version_detail_error";
            payload = {{"message", "Version not found"}, {"id", request.version_id}};
        } else {
            const ParentDetails parents = loadParentDetailsFromVersion(*version);
            const nlohmann::json &mediaDetails = request.media_details;
            payload = assembleDetailJson(*version, parents, mediaDetails);
        }
    } catch (const std::exception &error) {
        catalog = "version_detail_error";
        payload = {{"message", error.what()}, {"id", request.version_id}};
    }
    sendDetail(
        request.panel,
        request.request_id,
        catalog,
        utility::JsonStore(std::move(payload)));
}

void registerMediaDetailLoadType() {
    caf::init_global_meta_objects<caf::id_block::mikros_media_details_load>();
}

MediaDetailLoadActor::MediaDetailLoadActor(caf::actor_config &cfg)
    : caf::event_based_actor(cfg) {}

const char *MediaDetailLoadActor::name() const { return "MediaDetailLoadActor"; }

caf::behavior MediaDetailLoadActor::make_behavior() {
    return {
        [this](MediaDetailLoadRequest request) {
            try {
                executeMediaDetailLoad(request); ///< Blocking find stays on this detached thread.
            } catch (const std::exception &caught) {
                log::failure(caught); ///< A failed load must not take the panel down.
            }
            quit();
        },
    };
}

} // namespace xstudio::mikros_media_details
