// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_card.cpp
 * @brief Playlist header card fields for one row.
 */

#include "viewModel/playlist_card.hpp"

namespace xstudio::mikros_shotbrowser {

PlaylistCard::PlaylistCard(const poco::Playlist &row)
    : id_(row.id),
      name_(row.name),
      version_count_(row.version_count),
      open_notes_count_(row.open_notes_count),
      departments_(row.departments),
      created_at_(row.created_at),
      category_(row.category),
      author_(row.author) {}

nlohmann::json PlaylistCard::toJson() const {
    return {
        {"name", name_},
        {"id", id_},
        {"versionCount", version_count_},
        {"openNotesCount", open_notes_count_},
        {"departments", departments_},
        {"createdAt", created_at_},
        {"category", category_},
        {"author", author_}};
}

} // namespace xstudio::mikros_shotbrowser
