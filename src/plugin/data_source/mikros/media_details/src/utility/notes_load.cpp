// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/notes_load.cpp
 * @brief Detached load of the notes for one version.
 */

#include "utility/notes_load.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "model/poco/note_detail.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/utility/helpers.hpp"

#include <caf/actor.hpp>
#include <caf/actor_cast.hpp>
#include <caf/actor_system.hpp>
#include <caf/event_based_actor.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

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

std::mutex parkedMutex; ///< Serializes park and take. The actor runs on another thread.
std::uint64_t nextToken = 0; ///< Zero is never issued. A missing take stays distinguishable.
std::unordered_map<std::uint64_t, std::shared_ptr<NotesLoadRequest>> parked;

/**
 * @brief Stores one notes request and returns the token mailed to its actor.
 * @param request Version id and request id.
 * @return Token the actor passes to `take`.
 */
std::uint64_t park(NotesLoadRequest request) {
    std::lock_guard<std::mutex> lock(parkedMutex); ///< The actor may take while another load parks.
    const std::uint64_t token = ++nextToken; ///< uint64_t is registered before the actor system starts.
    parked.emplace(token, std::make_shared<NotesLoadRequest>(std::move(request)));
    return token;
}

/**
 * @brief Removes one parked notes request.
 * @param token Value mailed to the actor.
 * @return The request, or empty when the token was never parked.
 */
std::shared_ptr<NotesLoadRequest> take(std::uint64_t token) {
    std::lock_guard<std::mutex> lock(parkedMutex); ///< park may run on the panel thread.
    const auto found = parked.find(token);
    if (found == parked.end()) {
        return {}; ///< The actor quits when the token is unknown.
    }
    auto request = std::move(found->second);
    parked.erase(found);
    return request;
}

} // namespace

void launchNotesLoad(caf::actor_system &system, NotesLoadRequest request) {
    const std::uint64_t token = park(std::move(request)); ///< The struct never enters a CAF message.
    auto worker = system.spawn<NotesLoadActor, caf::detached>();
    anon_mail(token).send(worker);
}

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

NotesLoadActor::NotesLoadActor(caf::actor_config &cfg) : caf::event_based_actor(cfg) {}

const char *NotesLoadActor::name() const { return "NotesLoadActor"; }

caf::behavior NotesLoadActor::make_behavior() {
    return {
        [this](std::uint64_t token) {
            const std::shared_ptr<NotesLoadRequest> request = take(token);
            if (!request) {
                quit(); ///< A missing token has nothing to load.
                return;
            }
            try {
                executeNotesLoad(*request); ///< Blocking find stays on this detached thread.
            } catch (const std::exception &caught) {
                log::failure(caught); ///< A failed load must not take the panel down.
            }
            quit();
        },
    };
}

} // namespace xstudio::mikros_media_details
