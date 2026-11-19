// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_card.hpp
 * @brief One version row: list metadata for QML (no QObject).
 */
#pragma once

#include "model/poco/version_list_element.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Display state of one version. Reusable by the version list and playlists.
 *
 * The constructor copies list fields. The list owner resolves thumbnails in
 * batch and calls `setImage` when a local URL is ready.
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
     * @brief JSON object with the card keys read by `VersionCard.qml`.
     * @return One row for the `VersionCards` attribute.
     */
    [[nodiscard]] nlohmann::json toJson() const;

    [[nodiscard]] std::int64_t id() const { return id_; }

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
    bool has_notes_ = false;
    bool synced_to_client_ = false;
};

} // namespace xstudio::mikros_shotbrowser
