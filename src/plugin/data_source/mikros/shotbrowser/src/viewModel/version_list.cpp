// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_list.cpp
 * @brief Which versions to load from the current selection, and when that load is finished.
 */

#include "viewModel/version_list.hpp"
#include "viewModel/playlist_tree.hpp"
#include "viewModel/version_card.hpp"

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

void
resetVersionCatalog(PanelState &state) {
    ++state.version_request_id_; ///< Drop pages from the previous version load.
    state.versions_.clear();
    state.versions_loaded_->set_value(false); ///< An empty combo is a load in progress, not a result.
    if (state.version_cards_) {
        state.version_cards_->set_value(nlohmann::json::array());
    }
    state.version_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    state.version_->set_value("");
}

void refreshVersions(PanelState &state) {
    resetVersionCatalog(state);
    if (!state.selected_project_) {
        return;
    }
    startVersionLoad(state, state.version_request_id_, state.selected_project_->id);
    reloadExpandedPlaylist(state); ///< One open playlist follows the new version filter.
}

void startVersionLoad(PanelState &state, std::uint64_t requestId, std::int64_t projectId) {
    const VersionParents parents =
        readVersionParents(state.selected_sequence_, state.selected_shot_, state.selected_asset_);
    CatalogLoadRequest request =
        makeCatalogRequest(state.panel, CatalogKind::Version, requestId, projectId);
    request.sequence = parents.sequenceId;
    request.shot = parents.shotId;
    request.asset = parents.assetId;
    launchCatalogLoad(state.panel->system(), std::move(request));
}
bool versionParentsStillSelected(const PanelState &state, 
    bool hasSequence,
    std::int64_t sequenceId,
    bool hasShot,
    std::int64_t shotId,
    bool hasAsset,
    std::int64_t assetId) {
    return selectionMatches(hasSequence, sequenceId, state.selected_sequence_) &&
           selectionMatches(hasShot, shotId, state.selected_shot_) &&
           selectionMatches(hasAsset, assetId, state.selected_asset_);
}
void applyVersionPage(PanelState &state, 
    std::uint64_t requestId,
    bool hasSequence,
    std::int64_t sequenceId,
    bool hasShot,
    std::int64_t shotId,
    bool hasAsset,
    std::int64_t assetId,
    bool append,
    const utility::JsonStore &payload) {
    if (requestId != state.version_request_id_) {
        trace::drop("version", requestId);
        return;
    }
    if (!versionParentsStillSelected(state, 
            hasSequence, sequenceId, hasShot, shotId, hasAsset, assetId)) {
        trace::drop("version", requestId);
        return;
    }
    applyCatalogPage(state.versions_, state.version_, rowsFromJson<poco::Version>(payload), append);
    state.versions_loaded_->set_value(true); ///< Zero rows is a finished load.
    publishVersionCards(state);
}

} // namespace xstudio::mikros_shotbrowser
