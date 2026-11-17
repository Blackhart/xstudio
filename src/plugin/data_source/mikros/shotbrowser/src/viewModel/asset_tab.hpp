// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/asset_tab.hpp
 * @brief Asset combo: page load, clear, and the selection that follows a choice.
 */
#pragma once

#include "viewModel/context_shared.hpp"

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Asset row for a combo label.
 * @param state Panel catalogs and attributes.
 * @param label Text in the Asset combo.
 * @return The row, or empty when the label is not in the loaded pages.
 */
std::optional<poco::Asset> findAssetByLabel(const PanelState &state, const std::string &label);

/** @brief Clears the asset and logs the clear when one was selected. */
void clearAssetSelection(PanelState &state);

/** @brief Empties the asset combo. The empty value notifies. */
void resetAssetCatalog(PanelState &state);

/** @brief Drops the asset list and loads assets for the selected project. */
void refreshAssets(PanelState &state);

/**
 * @brief Loads asset pages for one project.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the asset list. A page with an older id is ignored.
 * @param projectId Project whose assets are loaded.
 */
void startAssetLoad(PanelState &state, std::uint64_t requestId, std::int64_t projectId);

/** @brief Drops asset rows without asking the backend. */
void clearAssetCatalog(PanelState &state);

/** @brief Keeps the chosen asset, clears the shot branch, and reloads versions. */
void onAssetChosen(PanelState &state);

/**
 * @brief Appends or replaces the asset combo with one page.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the load. An older page is ignored.
 * @param append False for the first page, which replaces the combo.
 * @param payload JSON rows from the worker.
 */
void applyAssetPage(
    PanelState &state, std::uint64_t requestId, bool append, const utility::JsonStore &payload);

} // namespace xstudio::mikros_shotbrowser
