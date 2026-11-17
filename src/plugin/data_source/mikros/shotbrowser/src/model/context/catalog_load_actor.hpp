// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/context/catalog_load_actor.hpp
 * @brief One detached actor for a single catalog load.
 */
#pragma once

#include "xstudio/atoms.hpp"

#include <caf/actor_system.hpp>
#include <caf/allowed_unsafe_message_type.hpp>
#include <caf/event_based_actor.hpp>

#include <cstdint>
#include <optional>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Which catalog one detached actor loads.
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
    CatalogKind kind = CatalogKind::Project; ///< Catalog this actor loads.
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
 * @brief Runs the DAL load carried by `request` and mails each page to the panel.
 * @param request Catalog, request id, project, and parents.
 */
void executeCatalogLoad(const CatalogLoadRequest &request);

/**
 * @brief Loads one catalog on its own thread, then stops.
 *
 * Spawned with `caf::detached` so a blocking find does not occupy the shared
 * scheduler or the panel actor. It is not linked to the panel.
 */
class CatalogLoadActor : public caf::event_based_actor {
  public:
    /**
     * @brief Builds an actor that waits for one `CatalogLoadRequest`.
     * @param cfg CAF actor config.
     */
    explicit CatalogLoadActor(caf::actor_config &cfg);

    /** @brief Name shown in CAF logs. */
    const char *name() const override;

    /**
     * @brief Handles one load request, then quits.
     * @return The one-shot behavior.
     */
    caf::behavior make_behavior() override;
};

/**
 * @brief Registers `CatalogLoadRequest` with CAF. Call once before the first load.
 */
void registerCatalogLoadType();

} // namespace xstudio::mikros_shotbrowser

CAF_ALLOW_UNSAFE_MESSAGE_TYPE(xstudio::mikros_shotbrowser::CatalogLoadRequest)

// Local only: the request holds a weak actor pointer, so it is not serialized.
// 17000 stays clear of the xSTUDIO blocks and the demo plugin at 16000.
CAF_BEGIN_TYPE_ID_BLOCK(mikros_shotbrowser_catalog, FIRST_CUSTOM_ID + 17000)
CAF_ADD_TYPE_ID(mikros_shotbrowser_catalog, (xstudio::mikros_shotbrowser::CatalogLoadRequest))
CAF_END_TYPE_ID_BLOCK(mikros_shotbrowser_catalog)
