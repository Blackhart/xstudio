// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_card.cpp
 * @brief Fields one version card shows: name, author, statuses, paths, and the cached image.
 */

#include "viewModel/version_card.hpp"
#include "viewModel/thumbnails.hpp"

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

nlohmann::json versionFacts(
    const poco::Version &row, const std::string &label, const std::string &image) {
    return {
        {"name", label},
        {"id", row.id},
        {"image", image},
        {"author", row.author},
        {"createdAt", row.created_at},
        {"frameRange", row.frame_range},
        {"description", row.description},
        {"step", row.step},
        {"status", row.status},
        {"artisticStatus", row.artistic_status},
        {"sites", row.sites},
        {"hasNotes", row.has_notes},
        {"syncedToClient", row.synced_to_client},
        {"frames", row.frames},
        {"movie", row.movie},
        {"uploadedMovie", row.uploaded_movie}};
}

} // namespace xstudio::mikros_shotbrowser

namespace xstudio::mikros_shotbrowser {

void
publishVersionCards(PanelState &state) {
    if (!state.version_cards_) {
        return;
    }
    nlohmann::json cards = nlohmann::json::array();
    if (state.selected_project_) {
        const std::vector<std::string> labels = buildUniqueLabels(state.versions_);
        for (std::size_t index = 0; index < state.versions_.size(); ++index) {
            cards.push_back(versionFacts(
                state.versions_[index],
                labels[index],
                thumbFor(state, state.selected_project_->id, state.versions_[index].id)));
        }
    }
    state.version_cards_->set_value(std::move(cards));
}


} // namespace xstudio::mikros_shotbrowser
