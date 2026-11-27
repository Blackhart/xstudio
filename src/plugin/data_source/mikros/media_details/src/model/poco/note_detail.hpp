// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/note_detail.hpp
 * @brief Note row for the Notes tab of Media Details.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xstudio::mikros_media_details::poco {

/**
 * @brief One attachment on a note.
 *
 * `image` is the thumbnail URL. The DAL copies it from the link.
 */
struct NoteAttachment {
    std::int64_t id = 0; ///< Attachment id.
    std::string name;    ///< File name.
    std::string image;   ///< Thumbnail URL or path. Empty when none.
};

/**
 * @brief One note as shown for a version.
 */
struct NoteDetail {
    std::int64_t id = 0;    ///< Domain note id.
    std::string name;       ///< Subject.
    std::string content;    ///< Body.
    std::string author;     ///< Person who wrote the note.
    std::string created_at; ///< Creation time as an ISO string.
    std::string status;     ///< Status code, such as `opn`.
    std::string to;         ///< Recipient names, joined by commas.
    std::string department; ///< Department label.
    std::vector<NoteAttachment> attachments; ///< Thumbnails on the note.
};

} // namespace xstudio::mikros_media_details::poco
