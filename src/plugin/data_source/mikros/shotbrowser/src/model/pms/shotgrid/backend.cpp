// SPDX-License-Identifier: Apache-2.0
/**
 * @file shotgrid/backend.cpp
 * @brief ShotGrid REST find backend.
 */

#include "pms/shotgrid/backend.hpp"

#include "pms/error.hpp"
#include "pms/shotgrid/translate.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <utility>

namespace pms {
namespace {

constexpr const char *kContentTypeHash = "application/vnd+shotgun.api3_hash+json";

httplib::Params authParams(const ShotGridAuth &auth) {
    httplib::Params params;
    if (const auto *script = std::get_if<ShotGridScriptAuth>(&auth)) {
        params.emplace("grant_type", "client_credentials");
        params.emplace("client_id", script->script_name);
        params.emplace("client_secret", script->script_key);
        return params;
    }
    const auto &user = std::get<ShotGridUserAuth>(auth);
    params.emplace("grant_type", "password");
    params.emplace("username", user.username);
    params.emplace("password", user.password);
    params.emplace("client_id", user.client_id);
    params.emplace("client_secret", user.client_secret);
    return params;
}

std::unique_ptr<httplib::Client> makeClient(const std::string &base_url) {
    auto client = std::make_unique<httplib::Client>(base_url);
    client->set_connection_timeout(30, 0);
    client->set_read_timeout(120, 0);
    return client;
}

} // namespace

ShotGridBackend::ShotGridBackend(ShotGridSite site, const std::string &mapping_path)
    : site_(std::move(site)), mapping_(ShotGridMapping::load(mapping_path)) {}

ShotGridBackend::ShotGridBackend(ShotGridSite site, ShotGridMapping mapping)
    : site_(std::move(site)), mapping_(std::move(mapping)) {}

void ShotGridBackend::ensureToken() {
    const auto now = std::chrono::steady_clock::now();
    if (access_token_ && now + std::chrono::seconds(30) < token_expires_) {
        return;
    }

    auto client = makeClient(site_.base_url);
    httplib::Params params;
    if (refresh_token_) {
        params.emplace("grant_type", "refresh_token");
        params.emplace("refresh_token", *refresh_token_);
        if (const auto *script = std::get_if<ShotGridScriptAuth>(&site_.auth)) {
            params.emplace("client_id", script->script_name);
            params.emplace("client_secret", script->script_key);
        } else {
            const auto &user = std::get<ShotGridUserAuth>(site_.auth);
            params.emplace("client_id", user.client_id);
            params.emplace("client_secret", user.client_secret);
        }
    } else {
        params = authParams(site_.auth);
    }

    const auto response = client->Post("/api/v1/auth/access_token", params);
    if (!response) {
        throw Error("shotgrid: auth request failed");
    }
    if (response->status < 200 || response->status >= 300) {
        throw Error(
            "shotgrid: auth HTTP " + std::to_string(response->status) + ": " + response->body);
    }
    nlohmann::json body = nlohmann::json::parse(response->body);
    if (!body.contains("access_token") || !body.contains("expires_in")) {
        throw Error("shotgrid: auth response missing token");
    }
    access_token_ = body.at("access_token").get<std::string>();
    if (body.contains("refresh_token") && body["refresh_token"].is_string()) {
        refresh_token_ = body.at("refresh_token").get<std::string>();
    }
    const int expiresIn = body.at("expires_in").get<int>();
    token_expires_ = now + std::chrono::seconds(expiresIn);
}

Page ShotGridBackend::find(const Ir &ir) {
    mapping_.requireComplete();
    if (site_.base_url.empty()) {
        throw Error("shotgrid: base_url is empty");
    }
    const ShotGridEntityMap *entity = mapping_.entity(ir.entity);
    if (!entity) {
        throw Error("shotgrid: unknown entity \"" + ir.entity + "\"");
    }

    ensureToken();

    nlohmann::json body;
    body["filters"] = shotgridFiltersFromIr(ir, mapping_);
    body["fields"] = shotgridFieldNames(*entity);
    const ShotGridFieldMap *idField = entity->field("id");
    body["sort"] = nlohmann::json::array({idField ? idField->shotgrid : "id"});
    body["page"] = nlohmann::json::object();
    body["page"]["size"] = ir.limit ? *ir.limit : 5000;
    body["page"]["number"] = 1;

    auto client = makeClient(site_.base_url);
    httplib::Headers headers = {
        {"Authorization", "Bearer " + *access_token_},
        {"Accept", kContentTypeHash},
        {"Content-Type", kContentTypeHash},
    };
    const std::string path = "/api/v1/entity/" + entity->shotgrid + "/_search";
    auto response = client->Post(path.c_str(), headers, body.dump(), kContentTypeHash);
    if (!response) {
        throw Error("shotgrid: search request failed");
    }
    if (response->status == 401) {
        access_token_.reset();
        refresh_token_.reset();
        ensureToken();
        headers = {
            {"Authorization", "Bearer " + *access_token_},
            {"Accept", kContentTypeHash},
            {"Content-Type", kContentTypeHash},
        };
        response = client->Post(path.c_str(), headers, body.dump(), kContentTypeHash);
        if (!response) {
            throw Error("shotgrid: search request failed after re-auth");
        }
    }
    if (response->status < 200 || response->status >= 300) {
        throw Error(
            "shotgrid: search HTTP " + std::to_string(response->status) + ": " + response->body);
    }

    const nlohmann::json payload = nlohmann::json::parse(response->body);
    if (!payload.contains("data") || !payload["data"].is_array()) {
        throw Error("shotgrid: search response missing data");
    }

    Page page;
    for (const nlohmann::json &row : payload["data"]) {
        page.rows.push_back(shotgridRecordFromJson(row, *entity));
    }
    if (ir.limit && !page.rows.empty() && page.rows.size() == *ir.limit) {
        page.next_cursor = std::to_string(std::get<std::int64_t>(page.rows.back().at("id")));
    }
    return page;
}

} // namespace pms
