// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/context_shared.cpp
 * @brief Detached catalog actor, page mail, and the home directory the caches share.
 */

#include "viewModel/context_shared.hpp"
#include "viewModel/playlist_tree.hpp"
#include "viewModel/shot_tree.hpp"

#include <caf/actor_system.hpp>
#include <caf/allowed_unsafe_message_type.hpp>
#include <caf/event_based_actor.hpp>

#include <cstdlib>
#include <filesystem>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

CAF_ALLOW_UNSAFE_MESSAGE_TYPE(xstudio::mikros_shotbrowser::CatalogLoadRequest)

// Local only: the request holds a weak actor pointer, so it is not serialized.
// 17000 stays clear of the xSTUDIO blocks and the demo plugin at 16000.
CAF_BEGIN_TYPE_ID_BLOCK(mikros_shotbrowser_catalog, FIRST_CUSTOM_ID + 17000)
CAF_ADD_TYPE_ID(mikros_shotbrowser_catalog, (xstudio::mikros_shotbrowser::CatalogLoadRequest))
CAF_END_TYPE_ID_BLOCK(mikros_shotbrowser_catalog)

namespace xstudio::mikros_shotbrowser {

namespace thumb {

std::filesystem::path homeDirectory() {
    const char *home = std::getenv("HOME");
#ifdef _WIN32
    if (!home || home[0] == '\0') {
        home = std::getenv("USERPROFILE");
    }
#endif
    if (!home || home[0] == '\0') {
        return {};
    }
    return std::filesystem::path(home);
}

} // namespace thumb

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
 * @brief Records which sequence, shot, and asset a version page was loaded for.
 *
 * The panel drops the page when the user has changed any of those three.
 * @param parents Sequence, shot, and asset ids captured when the load started.
 * @return The filter carried with every page of that load.
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

void launchCatalogLoad(caf::actor_system &system, CatalogLoadRequest request) {
    auto worker = system.spawn<CatalogLoadActor, caf::detached>();
    anon_mail(std::move(request)).send(worker);
}

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
    std::int64_t projectId) {
    CatalogLoadRequest request;
    request.kind = kind;
    request.panel = caf::actor_cast<caf::weak_actor_ptr>(panel);
    request.request_id = requestId;
    request.project_id = projectId;
    return request;
}

/**
 * @brief Reads the sequence, shot, and asset the version load was started with.
 * @param request Load whose filter may name those three.
 * @return The filter a later page must still match.
 */
VersionParents versionParentsOf(const CatalogLoadRequest &request) {
    return VersionParents{request.sequence, request.shot, request.asset};
}

/**
 * @brief Address of one tree mail built from a load request.
 * @param request Panel, request id, and project.
 * @param catalog `shot_tree`, `sequence_shots`, `playlist_tree`, or `playlist_versions`.
 * @param scope Parents that must still match when the page is applied.
 * @return Mail the publish functions send.
 */
TreeMail treeMail(const CatalogLoadRequest &request, const std::string &catalog, PageScope scope) {
    TreeMail mail;
    mail.weak = request.panel;
    mail.requestId = request.request_id;
    mail.catalog = catalog;
    mail.projectId = request.project_id;
    mail.scope = std::move(scope);
    return mail;
}

void executeCatalogLoad(const CatalogLoadRequest &request) {
    switch (request.kind) {
    case CatalogKind::Project:
        publishPages<poco::Project>(
            request.panel,
            request.request_id,
            "project",
            0,
            [](const std::optional<std::string> &cursor) {
                return dal::findProjects(dal::readPageLimit(), cursor);
            });
        return;
    case CatalogKind::ShotTree:
        publishSequences(treeMail(request, "shot_tree", {})); ///< Shots wait for an unfold.
        return;
    case CatalogKind::SequenceShots:
        publishShotsOfSequence(treeMail(request, "sequence_shots", {}), request.sequence_id);
        return;
    case CatalogKind::Asset:
        publishPages<poco::Asset>(
            request.panel,
            request.request_id,
            "asset",
            request.project_id,
            [projectId = request.project_id](const std::optional<std::string> &cursor) {
                return dal::findAssets(projectId, dal::readPageLimit(), cursor);
            });
        return;
    case CatalogKind::Version: {
        const VersionParents parents = versionParentsOf(request);
        publishPages<poco::Version>(
            request.panel,
            request.request_id,
            "version",
            request.project_id,
            [projectId = request.project_id, parents](const std::optional<std::string> &cursor) {
                return dal::findVersions(
                    projectId,
                    dal::readPageLimit(),
                    cursor,
                    parents.sequenceId,
                    parents.shotId,
                    parents.assetId);
            },
            pageScope(parents));
        return;
    }
    case CatalogKind::PlaylistTree:
        publishPlaylistNames(treeMail(request, "playlist_tree", {})); ///< Versions wait for an unfold.
        return;
    case CatalogKind::PlaylistVersions: {
        const VersionParents parents = versionParentsOf(request);
        publishVersionsOfPlaylist(
            treeMail(request, "playlist_versions", pageScope(parents)),
            parents,
            request.playlist_id);
        return;
    }
    }
}

/**
 * @brief Registers the load request so a detached actor can receive it.
 */
void registerCatalogLoadType() {
    caf::init_global_meta_objects<caf::id_block::mikros_shotbrowser_catalog>();
}

CatalogLoadActor::CatalogLoadActor(caf::actor_config &cfg) : caf::event_based_actor(cfg) {}

const char *CatalogLoadActor::name() const { return "CatalogLoadActor"; }

caf::behavior CatalogLoadActor::make_behavior() {
    return {
        [this](CatalogLoadRequest request) {
            try {
                executeCatalogLoad(request); ///< Blocking find stays on this detached thread.
            } catch (const std::exception &caught) {
                log::failure(caught); ///< A failed load must not take the panel down.
            }
            quit();
        },
    };
}

} // namespace xstudio::mikros_shotbrowser
