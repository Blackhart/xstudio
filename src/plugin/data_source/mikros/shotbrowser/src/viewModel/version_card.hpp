// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_card.hpp
 * @brief One version row: display fields and thumbnail load (no QObject).
 */
#pragma once

#include "model/poco/version.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Display state of one version. Reusable by the version list and playlists.
 *
 * The constructor copies ShotGrid fields and starts the thumbnail resolve. When the
 * local image is ready, `ImageReady` is called so the owner can republish JSON to QML.
 */
class VersionCard {
  public:
    /**
     * @brief Called on a worker thread when the local thumb URL is ready.
     * @param id Version id.
     * @param fileUrl `file://` path for the image.
     */
    using ImageReady = std::function<void(std::int64_t id, const std::string &fileUrl)>;

    /**
     * @brief Stores display fields and starts the thumbnail load.
     * @param row Version row from the catalog.
     * @param label Name shown in the title.
     * @param projectId Project used for the thumb cache path.
     * @param onImageReady Notified when the thumb file URL is ready. May run off the actor.
     */
    VersionCard(
        const poco::Version &row,
        const std::string &label,
        std::int64_t projectId,
        ImageReady onImageReady);

    /**
     * @brief Writes the local image URL after the thumb resolve finishes.
     * @param path `file://` URL, or empty.
     */
    void setImage(const std::string &path);

    /**
     * @brief JSON object with the card keys read by `VersionCard.qml`.
     * @return One row for the `VersionCards` attribute.
     */
    [[nodiscard]] nlohmann::json toJson() const;

    [[nodiscard]] std::int64_t id() const { return id_; }

  private:
    /**
     * @brief Resolves the thumbnail from cache or starts a download.
     * @param projectId Project for the cache path.
     * @param remoteImage ShotGrid image URL or path.
     * @param provider `http`, `file`, or empty.
     */
    void startThumbnailLoad(
        std::int64_t projectId, const std::string &remoteImage, const std::string &provider);

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
    bool has_notes_ = false;
    bool synced_to_client_ = false;
    std::string frames_;
    std::string movie_;
    std::string uploaded_movie_;
    ImageReady on_image_ready_; ///< Owner republishes JSON when the thumb arrives.
};

} // namespace xstudio::mikros_shotbrowser
