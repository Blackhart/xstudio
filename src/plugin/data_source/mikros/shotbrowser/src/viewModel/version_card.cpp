// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_card.cpp
 * @brief Version card fields and thumbnail resolve for one row.
 */

#include "viewModel/version_card.hpp"
#include "utility/thumbnails.hpp"

#include <utility>

namespace xstudio::mikros_shotbrowser {

VersionCard::VersionCard(
    const poco::Version &row,
    const std::string &label,
    std::int64_t projectId,
    ImageReady onImageReady)
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
      has_notes_(row.has_notes),
      synced_to_client_(row.synced_to_client),
      frames_(row.frames),
      movie_(row.movie),
      uploaded_movie_(row.uploaded_movie),
      on_image_ready_(std::move(onImageReady)) {
    startThumbnailLoad(projectId, row.image, row.image_provider);
}

void VersionCard::setImage(const std::string &path) { image_ = path; }

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
        {"hasNotes", has_notes_},
        {"syncedToClient", synced_to_client_},
        {"frames", frames_},
        {"movie", movie_},
        {"uploadedMovie", uploaded_movie_}};
}

void VersionCard::startThumbnailLoad(
    std::int64_t projectId, const std::string &remoteImage, const std::string &provider) {
    scheduleOneThumb(
        projectId,
        id_,
        remoteImage,
        provider,
        [id = id_, onReady = on_image_ready_](std::string fileUrl) {
            if (onReady) {
                onReady(id, fileUrl); ///< Owner applies the URL on the actor thread.
            }
        });
}

} // namespace xstudio::mikros_shotbrowser
