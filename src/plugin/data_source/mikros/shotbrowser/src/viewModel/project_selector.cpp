// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/project_selector.cpp
 * @brief Project combo: page load, clear, label lookup, and the reload that follows a choice.
 */

#include "viewModel/project_selector.hpp"
#include "viewModel/asset_tab.hpp"
#include "viewModel/playlist_tree.hpp"
#include "viewModel/shot_tree.hpp"
#include "viewModel/version_list.hpp"

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

std::optional<poco::Project>
findProjectByLabel(const PanelState &state, 
    const std::string &label) {
    return findRowByLabel(state.projects_, label);
}
void resetProjectCatalog(PanelState &state) {
    ++state.project_request_id_; ///< Drop pages from the previous project load.
    state.projects_.clear();
    state.selected_project_ = std::nullopt;
    state.project_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    state.project_->set_value(""); ///< Notifies, which clears the child catalogs.
}

void loadProjectsAsync(PanelState &state) {
    resetProjectCatalog(state);
    startProjectLoad(state, state.project_request_id_);
}

void startProjectLoad(PanelState &state, std::uint64_t requestId) {
    launchCatalogLoad(state.panel->system(), makeCatalogRequest(state.panel, CatalogKind::Project, requestId, 0));
}
void onProjectChosen(PanelState &state) {
    state.selected_project_ = findProjectByLabel(state, state.project_->value());
    logSelection("project", state.selected_project_);
    state.sequences_ready_ = false;
    state.assets_ready_ = false;
    state.playlists_ready_ = false;
    state.expanded_sequence_->set_value("", false); ///< Do not fetch shots for the previous project.
    state.expanded_playlist_->set_value("", false);
    if (state.browser_mode_->value() == "asset") {
        clearShotTree(state);
        refreshAssets(state); ///< The asset tab is what the user is looking at.
    } else {
        clearAssetCatalog(state);
        refreshShotTree(state); ///< Sequences only.
        clearShotSelection(state);
    }
    refreshVersions(state);
    if (state.result_view_->value() == "playlist") {
        refreshPlaylistTree(state); ///< Names only, because that view is open.
    } else {
        state.playlist_tree_->set_value(nlohmann::json::array());
    }
}
void applyProjectPage(PanelState &state, 
    std::uint64_t requestId, bool append, const utility::JsonStore &payload) {
    if (requestId != state.project_request_id_) {
        trace::drop("project", requestId);
        return;
    }
    applyCatalogPage(state.projects_, state.project_, rowsFromJson<poco::Project>(payload), append);
}

} // namespace xstudio::mikros_shotbrowser
