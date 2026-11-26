// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/notes_load.cpp
 * @brief Detached load of the notes for one version.
 */

#include "utility/notes_load.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "model/poco/note_detail.hpp"

#include "xstudio/utility/helpers.hpp"

#include <caf/actor.hpp>
#include <caf/actor_cast.hpp>

namespace xstudio::mikros_media_details {
namespace {

/**
 * @brief Mails one notes page to the panel when it is still alive.
 * @param weak Panel that asked for the load.
 * @param requestId Id the panel checks before applying.
 * @param catalog `version_notes` or `version_notes_error`.
 * @param payload Notes JSON, or `{message}`.
 */
void sendNotes(
    const caf::weak_actor_ptr &weak,
    std::uint64_t requestId,
    const std::string &catalog,
    utility::JsonStore payload) {
    if (auto handle = weak.lock()) {
        anon_mail(data_source::put_data_atom_v, requestId, catalog, std::move(payload))
            .send(caf::actor_cast<caf::actor>(handle));
    }
}

/**
 * @brief Turns one note into the object the Notes list reads.
 * @param note Row from the DAL.
 * @return `{id, name, content, author, createdAt, status, to, department, attachments}`.
 */
nlohmann::json noteJson(const poco::NoteDetail &note) {
    nlohmann::json attachments = nlohmann::json::array();
    for (const poco::NoteAttachment &item : note.attachments) {
        attachments.push_back(
            {{"id", item.id}, {"name", item.name}, {"image", item.image}});
    }
    return {
        {"id", note.id},
        {"name", note.name},
        {"content", note.content},
        {"author", note.author},
        {"createdAt", note.created_at},
        {"status", note.status},
        {"to", note.to},
        {"department", note.department},
        {"attachments", std::move(attachments)}};
}

} // namespace

void executeNotesLoad(const NotesLoadRequest &request) {
    nlohmann::json payload = nlohmann::json::object();
    std::string catalog = "version_notes";
    try {
        nlohmann::json notes = nlohmann::json::array();
        for (const poco::NoteDetail &note : dal::findNotesByVersionId(request.version_id)) {
            notes.push_back(noteJson(note));
        }
        payload = {{"notes", std::move(notes)}};
    } catch (const std::exception &error) {
        catalog = "version_notes_error";
        payload = {{"message", error.what()}, {"id", request.version_id}};
    }
    sendNotes(request.panel, request.request_id, catalog, utility::JsonStore(std::move(payload)));
}

void registerNotesLoadType() {
    caf::init_global_meta_objects<caf::id_block::mikros_version_notes_load>();
}

NotesLoadActor::NotesLoadActor(caf::actor_config &cfg) : caf::event_based_actor(cfg) {}

const char *NotesLoadActor::name() const { return "NotesLoadActor"; }

caf::behavior NotesLoadActor::make_behavior() {
    return {
        [this](NotesLoadRequest request) {
            try {
                executeNotesLoad(request); ///< Blocking find stays on this detached thread.
            } catch (const std::exception &caught) {
                log::failure(caught); ///< A failed load must not take the panel down.
            }
            quit();
        }};
}

} // namespace xstudio::mikros_media_details
