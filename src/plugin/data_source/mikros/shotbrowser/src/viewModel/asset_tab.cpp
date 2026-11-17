// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/asset_tab.cpp
 * @brief Asset combo: page load, clear, and the selection that follows a choice.
 */

#include "viewModel/asset_tab.hpp"
#include "viewModel/shot_tree.hpp"
#include "viewModel/version_list.hpp"

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

std::optional<poco::Asset>
findAssetByLabel(const PanelState &state, 
    const std::string &label) {
    return findRowByLabel(state.assets_, label);
}
void clearAssetSelection(PanelState &state) {
    const bool hadSelection = state.selected_asset_.has_value() || !state.asset_->value().empty();
    state.selected_asset_ = std::nullopt;
    if (!state.asset_->value().empty()) {
        state.asset_->set_value("", false); ///< Do not re-enter the asset handler.
    }
    if (hadSelection) {
        trace::select("asset", false, 0, {});
    }
}
void resetAssetCatalog(PanelState &state) {
    ++state.asset_request_id_; ///< Drop pages from the previous asset load.
    state.assets_.clear();
    state.selected_asset_ = std::nullopt;
    state.asset_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    state.asset_->set_value(""); ///< Notifies, which may log `select asset none`.
}

void refreshAssets(PanelState &state) {
    resetAssetCatalog(state);
    state.assets_ready_ = state.selected_project_.has_value();
    if (!state.selected_project_) {
        return;
    }
    startAssetLoad(state, state.asset_request_id_, state.selected_project_->id);
}

void startAssetLoad(PanelState &state, std::uint64_t requestId, std::int64_t projectId) {
    launchCatalogLoad(
        state.panel->system(), makeCatalogRequest(state.panel, CatalogKind::Asset, requestId, projectId));
}
void clearAssetCatalog(PanelState &state) {
    ++state.asset_request_id_; ///< Drop asset pages still loading for the previous project.
    state.assets_.clear();
    state.assets_ready_ = false;
    state.asset_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    clearAssetSelection(state);
}
void onAssetChosen(PanelState &state) {
    state.selected_asset_ = findAssetByLabel(state, state.asset_->value());
    logSelection("asset", state.selected_asset_);
    if (state.selected_asset_) {
        clearSequenceBranch(state); ///< Asset and the shot tree are mutually exclusive.
    } else {
        clearShotSelection(state);
    }
    refreshVersions(state);
}
void applyAssetPage(PanelState &state, 
    std::uint64_t requestId, bool append, const utility::JsonStore &payload) {
    if (requestId != state.asset_request_id_) {
        trace::drop("asset", requestId);
        return;
    }
    applyCatalogPage(state.assets_, state.asset_, rowsFromJson<poco::Asset>(payload), append);
}

} // namespace xstudio::mikros_shotbrowser
