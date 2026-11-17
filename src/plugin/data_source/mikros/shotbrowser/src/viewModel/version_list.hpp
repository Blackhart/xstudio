// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_list.hpp
 * @brief Which versions to load from the current selection, and when that load is finished.
 */
#pragma once

#include "viewModel/context_shared.hpp"

namespace xstudio::mikros_shotbrowser {

/** @brief Empties the version list and marks it as still loading. */
void resetVersionCatalog(PanelState &state);

/** @brief Drops the version list and loads it again for the current selection. */
void refreshVersions(PanelState &state);

/**
 * @brief Loads version pages for the current sequence, shot, or asset.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the version list. A page with an older id is ignored.
 * @param projectId Project whose versions are loaded.
 */
void startVersionLoad(PanelState &state, std::uint64_t requestId, std::int64_t projectId);

/**
 * @brief True when the panel is still on the sequence, shot, and asset of a reply.
 *
 * A version page is dropped when any of those three has changed.
 * @param state Panel catalogs and attributes.
 */
bool versionParentsStillSelected(
    const PanelState &state,
    bool hasSequence,
    std::int64_t sequenceId,
    bool hasShot,
    std::int64_t shotId,
    bool hasAsset,
    std::int64_t assetId);

/**
 * @brief Appends or replaces the version list when the filter is still the same.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the load. An older page is ignored.
 * @param append False for the first page, which replaces the list.
 * @param payload JSON rows from the worker.
 */
void applyVersionPage(
    PanelState &state,
    std::uint64_t requestId,
    bool hasSequence,
    std::int64_t sequenceId,
    bool hasShot,
    std::int64_t shotId,
    bool hasAsset,
    std::int64_t assetId,
    bool append,
    const utility::JsonStore &payload);

} // namespace xstudio::mikros_shotbrowser
