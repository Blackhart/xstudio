// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_card.hpp
 * @brief Fields one version card shows: name, author, statuses, paths, and the cached image.
 */
#pragma once

#include "viewModel/context_shared.hpp"

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Builds the JSON one version card reads.
 * @param row Version whose display fields are copied.
 * @param label Unique name shown in the title.
 * @param image Thumbnail path already resolved, or empty until the thumb arrives.
 * @return Object with the card keys. Empty strings stay empty so the card shows N/A.
 */
nlohmann::json versionFacts(
    const poco::Version &row, const std::string &label, const std::string &image);

/** @brief Publishes the name and the local image of each version in the current list. */
void publishVersionCards(PanelState &state);

} // namespace xstudio::mikros_shotbrowser
