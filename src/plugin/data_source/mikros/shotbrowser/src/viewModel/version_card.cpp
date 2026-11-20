// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_card.cpp
 * @brief Version card fields for one row. Thumbs and signed URLs are applied by the list owner.
 */

#include "viewModel/version_card.hpp"

namespace xstudio::mikros_shotbrowser {

VersionCard::VersionCard(const poco::VersionListElement &row, const std::string &label)
    : id_(row.id),
      name_(label),
      author_(row.author),
      created_at_(row.created_at),
      frame_range_(row.frame_range),
      description_(row.description),
      step_(row.step),
      status_(row.status),
      artistic_status_(row.artistic_status),
      sites_(row.sites),
      open_notes_count_(row.open_notes_count),
      synced_to_client_(row.synced_to_client),
      frames_(row.frames),
      movie_(row.movie) {}

void VersionCard::setImage(const std::string &path) { image_ = path; }

void VersionCard::setTranscoded(const poco::VersionTranscodedMedia &media) {
    uploaded_movie_ = media.uploaded_movie;
}

nlohmann::json VersionCard::toJson() const {
    return {
        {"name", name_},
        {"id", id_},
        {"image", image_},
        {"author", author_},
        {"createdAt", created_at_},
        {"frameRange", frame_range_},
        {"description", description_},
        {"step", step_},
        {"status", status_},
        {"artisticStatus", artistic_status_},
        {"sites", sites_},
        {"openNotesCount", open_notes_count_},
        {"syncedToClient", synced_to_client_},
        {"frames", frames_},
        {"movie", movie_},
        {"uploadedMovie", uploaded_movie_}};
}

} // namespace xstudio::mikros_shotbrowser
