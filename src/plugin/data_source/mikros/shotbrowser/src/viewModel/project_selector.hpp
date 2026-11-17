// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/project_selector.hpp
 * @brief Project combo: page load, clear, label lookup, and the reload that follows a choice.
 */
#pragma once

#include "viewModel/context_shared.hpp"

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Project row for a combo label.
 * @param state Panel catalogs and attributes.
 * @param label Text in the Project combo. Empty when nothing is selected.
 * @return The row, or empty when the label is not in the loaded pages.
 */
std::optional<poco::Project> findProjectByLabel(const PanelState &state, const std::string &label);

/**
 * @brief Empties the project combo.
 *
 * The empty value notifies, which clears the child lists.
 * @param state Panel catalogs and attributes.
 */
void resetProjectCatalog(PanelState &state);

/** @brief Drops any project pages still arriving, then loads the project list. */
void loadProjectsAsync(PanelState &state);

/**
 * @brief Loads project pages for one generation of the project list.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of this load. A page with an older id is ignored.
 */
void startProjectLoad(PanelState &state, std::uint64_t requestId);

/** @brief Loads the open tab and the version list for the project the user picked. */
void onProjectChosen(PanelState &state);

/**
 * @brief Appends or replaces the project combo with one page.
 * @param state Panel catalogs and attributes.
 * @param requestId Generation of the load. An older page is ignored.
 * @param append False for the first page, which replaces the combo.
 * @param payload JSON rows from the worker.
 */
void applyProjectPage(
    PanelState &state, std::uint64_t requestId, bool append, const utility::JsonStore &payload);

} // namespace xstudio::mikros_shotbrowser
