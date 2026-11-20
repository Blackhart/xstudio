// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/catalog.cpp
 * @brief Detached catalog actor and the page mail each screen receives.
 */

#include "utility/catalog.hpp"

#include <caf/actor_system.hpp>
#include <caf/allowed_unsafe_message_type.hpp>
#include <caf/event_based_actor.hpp>

#include <optional>
#include <utility>

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

VersionParents readVersionParents() {
    VersionParents parents;
    if (dal::selectedSequence() != -1) {
        parents.sequenceId = dal::selectedSequence(); ///< The shot tree stored this id.
    }
    if (dal::selectedShot() != -1) {
        parents.shotId = dal::selectedShot();
    }
    if (dal::selectedAsset() != -1) {
        parents.assetId = dal::selectedAsset(); ///< The asset combo stored this id.
    }
    return parents;
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
            scope.has_playlist,
            scope.playlist_id,
            append,
            std::move(payload))
            .send(caf::actor_cast<caf::actor>(handle));
    }
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
            },
            {},
            request.generation);
        return;
    case CatalogKind::ShotTree: {
        std::optional<std::string> cursor;
        bool append = false;
        for (;;) {
            if (request.generation && request.generation->load() != request.request_id) {
                break; ///< A newer load started. Do not call the DAL again.
            }
            const dal::RowPage<poco::SequenceShots> page =
                dal::findSequenceShots(request.project_id, dal::readPageLimit(), cursor);
            sendCatalog(
                request.panel,
                request.request_id,
                "shot_tree",
                request.project_id,
                {},
                append,
                rowsToJson(page.rows));
            if (!page.next_cursor) {
                break;
            }
            cursor = page.next_cursor;
            append = true;
        }
        return;
    }
    case CatalogKind::ShotTreeCount: {
        if (request.generation && request.generation->load() != request.request_id) {
            return; ///< A newer load started. Do not call the DAL.
        }
        utility::JsonStore counts;
        counts["sequences"] = dal::getNumberOfSequences(request.project_id);
        counts["shots"] = dal::getNumberOfShots(request.project_id);
        sendCatalog(
            request.panel,
            request.request_id,
            "shot_tree_count",
            request.project_id,
            {},
            false,
            std::move(counts));
        return;
    }
    case CatalogKind::Asset:
        publishPages<poco::Asset>(
            request.panel,
            request.request_id,
            "asset",
            request.project_id,
            [projectId = request.project_id](const std::optional<std::string> &cursor) {
                return dal::findAssets(projectId, dal::readPageLimit(), cursor);
            },
            {},
            request.generation); ///< Stops before the next findAssets when the id moves.
        return;
    case CatalogKind::AssetCount: {
        if (request.generation && request.generation->load() != request.request_id) {
            return; ///< A newer load started. Do not call the DAL.
        }
        utility::JsonStore counts;
        counts["assets"] = dal::getNumberOfAssets(request.project_id);
        sendCatalog(
            request.panel,
            request.request_id,
            "asset_count",
            request.project_id,
            {},
            false,
            std::move(counts));
        return;
    }
    case CatalogKind::Version: {
        const VersionParents parents = versionParentsOf(request);
        if (request.generation && request.generation->load() != request.request_id) {
            return; ///< A newer load started before this find.
        }
        const dal::RowPage<poco::VersionListElement> page = dal::findVersionListElements(
            request.project_id,
            dal::readPageLimit(),
            request.cursor,
            parents.sequenceId,
            parents.shotId,
            parents.assetId,
            std::nullopt,
            request.name_or_id);
        nlohmann::json body;
        body["rows"] = static_cast<nlohmann::json>(rowsToJson(page.rows));
        if (page.next_cursor) {
            body["next_cursor"] = *page.next_cursor; ///< Opaque token for loadMoreVersionsAsync.
        } else {
            body["next_cursor"] = nullptr; ///< No further page.
        }
        sendCatalog(
            request.panel,
            request.request_id,
            "version",
            request.project_id,
            pageScope(parents),
            request.cursor.has_value(), ///< First page replaces; a cursor page appends.
            utility::JsonStore(std::move(body)));
        return;
    }
    case CatalogKind::VersionCount: {
        const VersionParents parents = versionParentsOf(request);
        if (request.generation && request.generation->load() != request.request_id) {
            return; ///< A newer load started. Do not call the DAL.
        }
        utility::JsonStore counts;
        counts["versions"] = dal::getNumberOfVersions(
            request.project_id,
            parents.sequenceId,
            parents.shotId,
            parents.assetId,
            std::nullopt,
            request.name_or_id);
        sendCatalog(
            request.panel,
            request.request_id,
            "version_count",
            request.project_id,
            pageScope(parents),
            false,
            std::move(counts));
        return;
    }
    case CatalogKind::PlaylistTree: {
        if (request.generation && request.generation->load() != request.request_id) {
            return; ///< A newer load started before this find.
        }
        const dal::RowPage<poco::Playlist> page = dal::findPlaylists(
            request.project_id, dal::readPageLimit(), request.cursor, request.name_or_id);
        nlohmann::json body;
        body["rows"] = static_cast<nlohmann::json>(rowsToJson(page.rows));
        if (page.next_cursor) {
            body["next_cursor"] = *page.next_cursor; ///< Opaque token for loadMorePlaylistsAsync.
        } else {
            body["next_cursor"] = nullptr; ///< No further page.
        }
        sendCatalog(
            request.panel,
            request.request_id,
            "playlist_tree",
            request.project_id,
            {},
            request.cursor.has_value(), ///< First page replaces; a cursor page appends.
            utility::JsonStore(std::move(body)));
        return;
    }
    case CatalogKind::PlaylistCount: {
        if (request.generation && request.generation->load() != request.request_id) {
            return; ///< A newer load started. Do not call the DAL.
        }
        utility::JsonStore counts;
        counts["playlists"] =
            dal::getNumberOfPlaylists(request.project_id, request.name_or_id);
        sendCatalog(
            request.panel,
            request.request_id,
            "playlist_count",
            request.project_id,
            {},
            false,
            std::move(counts));
        return;
    }
    case CatalogKind::PlaylistVersions: {
        const VersionParents parents = versionParentsOf(request);
        PageScope scope = pageScope(parents);
        scope.has_playlist = true;
        scope.playlist_id = request.playlist_id; ///< The panel writes these versions into that node.
        publishPages<poco::VersionListElement>(
            request.panel,
            request.request_id,
            "playlist_versions",
            request.project_id,
            [projectId = request.project_id, parents, playlistId = request.playlist_id](
                const std::optional<std::string> &cursor) {
                return dal::findVersionListElements(
                    projectId,
                    dal::readPageLimit(),
                    cursor,
                    parents.sequenceId,
                    parents.shotId,
                    parents.assetId,
                    playlistId);
            },
            scope);
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
