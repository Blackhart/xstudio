// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_card.hpp
 * @brief One version row: list metadata for QML (no QObject).
 */
#pragma once

#include "model/poco/version_list_element.hpp"
#include "model/poco/version_transcoded_media.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Display state of one version. Reusable by the version list and playlists.
 *
 * The constructor copies list fields (including local frames/movie paths). The
 * list owner resolves thumbnails and signed movie URLs in batch, then calls
 * `setImage` / `setTranscoded` when ready.
 */
class VersionCard {
  public:
    /**
     * @brief Stores display fields from one list row.
     * @param row List row from the catalog.
     * @param label Name shown in the title.
     */
    VersionCard(const poco::VersionListElement &row, const std::string &label);

    /**
     * @brief Writes the local image URL after the thumb resolve finishes.
     * @param path `file://` URL, or empty.
     */
    void setImage(const std::string &path);

    /**
     * @brief Stores the signed uploaded-movie URL after the batch resolve.
     * @param media Transcoded row for this version id.
     */
    void setTranscoded(const poco::VersionTranscodedMedia &media);

    /**
     * @brief JSON object with the card keys read by `VersionCard.qml`.
     * @return One row for the `VersionCards` attribute.
     */
    [[nodiscard]] nlohmann::json toJson() const;

    [[nodiscard]] std::int64_t id() const { return id_; }
    [[nodiscard]] const std::string &frameRange() const { return frame_range_; }
    [[nodiscard]] const std::string &frames() const { return frames_; }
    [[nodiscard]] const std::string &movie() const { return movie_; }
    [[nodiscard]] const std::string &uploadedMovie() const { return uploaded_movie_; }

  private:
    std::int64_t id_ = 0;
    std::string name_;
    std::string image_;
    std::string author_;
    std::string created_at_;
    std::string frame_range_;
    std::string description_;
    std::string step_;
    std::string status_;
    std::string artistic_status_;
    std::string sites_;
    std::int64_t open_notes_count_ = 0;
    bool synced_to_client_ = false;
    std::string frames_;
    std::string movie_;
    std::string uploaded_movie_;
};

} // namespace xstudio::mikros_shotbrowser
