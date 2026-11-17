// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/context/catalog_load_actor.cpp
 * @brief Runs one catalog load, then stops the actor.
 */

#include "model/context/catalog_load_actor.hpp"

#include "model/log.hpp"

namespace xstudio::mikros_shotbrowser {

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
