// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_card.hpp
 * @brief One playlist header row: list metadata for QML (no QObject).
 */
#pragma once

#include "model/poco/playlist.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstddef>
#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Display state of one playlist header.
 *
 * The constructor copies list fields from the catalog row. The tree owner
 * publishes `toJson()` into the playlist tree attribute for QML.
 */
class PlaylistCard {
  public:
    /**
     * @brief Stores display fields from one playlist row.
     * @param row Playlist row from the catalog.
     */
    explicit PlaylistCard(const poco::Playlist &row);

    /**
     * @brief JSON object with the card keys read by `PlaylistCard.qml`.
     * @return One header object for the playlist tree.
     */
    [[nodiscard]] nlohmann::json toJson() const;

    [[nodiscard]] std::int64_t id() const { return id_; }

  private:
    std::int64_t id_ = 0;
    std::string name_;
    std::size_t version_count_ = 0;
    std::int64_t open_notes_count_ = 0;
    std::string departments_;
    std::string created_at_;
    std::string category_;
    std::string author_;
};

} // namespace xstudio::mikros_shotbrowser
