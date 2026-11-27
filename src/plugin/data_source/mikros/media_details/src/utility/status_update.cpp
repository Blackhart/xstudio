// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/status_update.cpp
 * @brief Detached write of one editable status field.
 */

#include "utility/status_update.hpp"

#include "model/dal/dal.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/utility/helpers.hpp"

#include <caf/actor.hpp>
#include <caf/actor_cast.hpp>
#include <caf/actor_system.hpp>
#include <caf/event_based_actor.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace xstudio::mikros_media_details {
namespace {

/**
 * @brief Mails one status result to the panel when it is still alive.
 * @param weak Panel that asked for the write.
 * @param requestId Id the panel checks before applying.
 * @param catalog `status_updated`, `status_error`, `note_status_updated`, or `note_status_error`.
 * @param payload `{entity,id,field,code}` or `{message}`.
 */
void sendStatus(
    const caf::weak_actor_ptr &weak,
    std::uint64_t requestId,
    const std::string &catalog,
    utility::JsonStore payload) {
    if (auto handle = weak.lock()) {
        anon_mail(data_source::put_data_atom_v, requestId, catalog, std::move(payload))
            .send(caf::actor_cast<caf::actor>(handle));
    }
}

std::mutex parkedMutex; ///< Serializes park and take. The actor runs on another thread.
std::uint64_t nextToken = 0; ///< Zero is never issued. A missing take stays distinguishable.
std::unordered_map<std::uint64_t, std::shared_ptr<StatusUpdateRequest>> parked;

/**
 * @brief Stores one update request and returns the token mailed to its actor.
 * @param request Entity, id, field, and code.
 * @return Token the actor passes to `take`.
 */
std::uint64_t park(StatusUpdateRequest request) {
    std::lock_guard<std::mutex> lock(parkedMutex); ///< The actor may take while another write parks.
    const std::uint64_t token = ++nextToken; ///< uint64_t is registered before the actor system starts.
    parked.emplace(token, std::make_shared<StatusUpdateRequest>(std::move(request)));
    return token;
}

/**
 * @brief Removes one parked update request.
 * @param token Value mailed to the actor.
 * @return The request, or empty when the token was never parked.
 */
std::shared_ptr<StatusUpdateRequest> take(std::uint64_t token) {
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

void launchStatusUpdate(caf::actor_system &system, StatusUpdateRequest request) {
    const std::uint64_t token = park(std::move(request)); ///< The struct never enters a CAF message.
    auto worker = system.spawn<StatusUpdateActor, caf::detached>();
    anon_mail(token).send(worker);
}

void executeStatusUpdate(const StatusUpdateRequest &request) {
    try {
        dal::updateField(request.entity, request.id, request.field, request.code);
        sendStatus(
            request.panel,
            request.request_id,
            request.catalog_ok,
            utility::JsonStore(nlohmann::json{
                {"entity", request.entity},
                {"id", request.id},
                {"field", request.field},
                {"code", request.code}}));
    } catch (const std::exception &error) {
        sendStatus(
            request.panel,
            request.request_id,
            request.catalog_error,
            utility::JsonStore(nlohmann::json{{"message", error.what()}}));
    }
}

StatusUpdateActor::StatusUpdateActor(caf::actor_config &cfg) : caf::event_based_actor(cfg) {}

const char *StatusUpdateActor::name() const { return "StatusUpdateActor"; }

caf::behavior StatusUpdateActor::make_behavior() {
    return {
        [this](std::uint64_t token) {
            const std::shared_ptr<StatusUpdateRequest> request = take(token);
            if (!request) {
                quit(); ///< A missing token has nothing to write.
                return;
            }
            executeStatusUpdate(*request); ///< A failed write is mailed, not thrown to the panel.
            quit();
        },
    };
}

} // namespace xstudio::mikros_media_details
