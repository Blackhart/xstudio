// SPDX-License-Identifier: Apache-2.0
/**
 * @file shotgrid/backend.cpp
 * @brief ShotGrid REST find backend.
 */

#include "pms/shotgrid/backend.hpp"

#include "pms/error.hpp"
#include "pms/shotgrid/field_schema.hpp"
#include "pms/shotgrid/translate.hpp"

#include <cpp-httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace pms {
namespace {

constexpr const char *kApiRoot = "/api/v1.1";
constexpr const char *kAcceptJson = "application/json";
constexpr const char *kContentTypeHash = "application/vnd+shotgun.api3_hash+json";
constexpr const char *kContentTypeArray = "application/vnd+shotgun.api3_array+json";
constexpr int kMaxPageSize = 500;

/**
 * @brief REST collection for a mapped ShotGrid type.
 * @param typeName Mapping type, e.g. `Project`.
 * @return Plural name used in `/entity/{collection}`. Anything else is used as written.
 */
std::string findCollectionName(const std::string &typeName) {
    if (typeName == "Project") {
        return "projects";
    } else if (typeName == "Sequence") {
        return "sequences";
    } else if (typeName == "Shot") {
        return "shots";
    } else if (typeName == "Asset") {
        return "assets";
    } else if (typeName == "Playlist") {
        return "playlists";
    } else if (typeName == "Version") {
        return "versions";
    }
    return typeName;
}

httplib::Params buildCredentialParams(const ShotGridAuth &auth) {
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
    return params;
}

/**
 * @brief Wraps a basic condition so the hash content type always gets a complex filter.
 * @param filters Leaf array or complex filter object.
 * @return Object with `logical_operator` and `conditions`.
 */
nlohmann::json wrapAsHashFilter(nlohmann::json filters) {
    if (!filters.is_array()) {
        return filters;
    }
    nlohmann::json wrapped;
    wrapped["logical_operator"] = "and";
    wrapped["conditions"] = nlohmann::json::array({std::move(filters)});
    return wrapped;
}

std::unique_ptr<httplib::Client> openHttpClient(const std::string &baseUrl) {
    auto client = std::make_unique<httplib::Client>(baseUrl);
    client->set_connection_timeout(30, 0);
    client->set_read_timeout(120, 0);
    return client;
}

[[noreturn]] void throwIfNoHttpResponse(const char *what, const httplib::Result &response) {
    throw Error(
        std::string("shotgrid: ") + what + " (" + httplib::to_string(response.error()) + ")");
}

} // namespace

ShotGridBackend::ShotGridBackend(ShotGridSite site, const std::string &mapping_path)
    : site_(std::move(site)), mapping_(ShotGridMapping::load(mapping_path)) {}

ShotGridBackend::ShotGridBackend(ShotGridSite site, ShotGridMapping mapping)
    : site_(std::move(site)), mapping_(std::move(mapping)) {}

void ShotGridBackend::setSearchLog(std::function<void(const std::string &)> searchLog) {
    searchLog_ = std::move(searchLog);
}

void ShotGridBackend::setAuthLog(std::function<void(const std::string &)> authLog) {
    authLog_ = std::move(authLog);
}

const ShotGridSchema &ShotGridBackend::extractSchemaFromIR(const Ir &ir) const {
    mapping_.throwIfNamesMissing();
    throwIfBaseUrlEmpty();
    const ShotGridSchema *schema = mapping_.findSchema(ir.entity);
    if (!schema) {
        throw Error("shotgrid: unknown entity \"" + ir.entity + "\"");
    }
    return *schema;
}

void ShotGridBackend::throwIfBaseUrlEmpty() const {
    if (site_.base_url.empty()) {
        throw Error("shotgrid: base_url is empty");
    }
}

bool ShotGridBackend::hasUsableToken() const {
    const auto now = std::chrono::steady_clock::now();
    return access_token_ && now + std::chrono::seconds(30) < token_expires_;
}

void ShotGridBackend::connectIfNeeded() {
    if (hasUsableToken()) {
        return;
    }
    storeAccessToken(requestAccessToken());
}

httplib::Params ShotGridBackend::buildAuthParams() const {
    if (!refresh_token_) {
        return buildCredentialParams(site_.auth);
    }
    httplib::Params params;
    params.emplace("grant_type", "refresh_token");
    params.emplace("refresh_token", *refresh_token_);
    return params;
}

httplib::Result ShotGridBackend::requestAccessToken() {
    auto client = openHttpClient(site_.base_url);
    const httplib::Headers headers = {{"Accept", kAcceptJson}};
    httplib::Result response =
        client->Post(std::string(kApiRoot) + "/auth/access_token", headers, buildAuthParams());
    if (!response) {
        throwIfNoHttpResponse("auth request failed", response);
    }
    return response;
}

void ShotGridBackend::storeAccessToken(const httplib::Result &response) {
    if (response->status < 200 || response->status >= 300) {
        throw Error(
            "shotgrid: auth HTTP " + std::to_string(response->status) + ": " + response->body);
    }
    const nlohmann::json body = nlohmann::json::parse(response->body);
    if (!body.contains("access_token") || !body.contains("expires_in")) {
        throw Error("shotgrid: auth response missing token");
    }
    access_token_ = body.at("access_token").get<std::string>();
    if (body.contains("refresh_token") && body["refresh_token"].is_string()) {
        refresh_token_ = body.at("refresh_token").get<std::string>();
    }
    const int expiresIn = body.at("expires_in").get<int>();
    token_expires_ = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn);
    if (authLog_) {
        authLog_("auth ok expires_in=" + std::to_string(expiresIn));
    }
}

void ShotGridBackend::forgetTokens() {
    access_token_.reset();
    refresh_token_.reset();
}

nlohmann::json ShotGridBackend::fetchFieldSchema(
    const std::string &ownerType, const std::string &field) {
    const std::string url = std::string(kApiRoot) + "/schema/" + shotGridSchemaEntityName(ownerType) +
                            "/fields/" + field;
    auto send = [this, &url]() {
        connectIfNeeded(); ///< The schema read uses the same bearer token as search.
        auto client = openHttpClient(site_.base_url);
        const httplib::Headers headers = {
            {"Authorization", "Bearer " + *access_token_},
            {"Accept", kAcceptJson},
        };
        if (searchLog_) {
            searchLog_("GET " + url); ///< A name_* filter shows which schema leaf was read.
        }
        return client->Get(url, headers);
    };
    httplib::Result response = send();
    if (!response) {
        throwIfNoHttpResponse("schema request failed", response);
    }
    if (response->status == 401) {
        forgetTokens(); ///< A stale token is not reused for the retry.
        response = send();
        if (!response) {
            throwIfNoHttpResponse("schema request failed after re-auth", response);
        }
    }
    if (response->status < 200 || response->status >= 300) {
        throw Error(
            "shotgrid: schema HTTP " + std::to_string(response->status) + ": " + response->body);
    }
    return nlohmann::json::parse(response->body);
}

ShotGridFieldInfo ShotGridBackend::fieldSchema(const std::string &ownerType, const std::string &field) {
    return cachedFieldInfo(
        fieldSchemas_, ownerType, field, [this](const std::string &owner, const std::string &name) {
            return fetchFieldSchema(owner, name);
        });
}

nlohmann::json ShotGridBackend::buildRequestBody(const Ir &ir) {
    nlohmann::json body = nlohmann::json::object();
    const FieldSchemaResolve resolve = [this](const std::string &owner, const std::string &field) {
        return fieldSchema(owner, field); ///< name_* on multi_entity becomes cached_display_name.
    };
    const nlohmann::json filters = shotgridFiltersFromIr(ir, mapping_, resolve);
    if (filters.is_null()) {
        body["filters"] = nlohmann::json::array(); ///< No criterion: an empty array lists every row.
        return body;
    }
    body["filters"] = wrapAsHashFilter(filters);
    return body;
}

std::string ShotGridBackend::buildEntityUrl(const ShotGridSchema &schema) const {
    return std::string(kApiRoot) + "/entity/" + findCollectionName(schema.shotgridName) + "/_search";
}

std::string ShotGridBackend::buildSummarizeUrl(const ShotGridSchema &schema) const {
    return std::string(kApiRoot) + "/entity/" + findCollectionName(schema.shotgridName) +
           "/_summarize";
}

nlohmann::json ShotGridBackend::buildSummarizeBody(const Ir &ir) {
    nlohmann::json body = buildRequestBody(ir);
    body["summary_fields"] = nlohmann::json::array(
        {nlohmann::json{{"field", "id"}, {"type", "count"}}});
    return body;
}

std::size_t ShotGridBackend::readSummarizeCount(const httplib::Result &response) const {
    const nlohmann::json payload = nlohmann::json::parse(response->body);
    const nlohmann::json *summaries = nullptr;
    if (payload.contains("data") && payload["data"].is_object() &&
        payload["data"].contains("summaries")) {
        summaries = &payload["data"]["summaries"];
    } else if (payload.contains("summaries")) {
        summaries = &payload["summaries"];
    }
    if (!summaries || !summaries->is_object() || !summaries->contains("id")) {
        throw Error("shotgrid: summarize response missing summaries.id");
    }
    const nlohmann::json &id = (*summaries)["id"];
    if (id.is_number_integer()) {
        return static_cast<std::size_t>(id.get<std::int64_t>());
    }
    if (id.is_number_unsigned()) {
        return id.get<std::size_t>();
    }
    throw Error("shotgrid: summarize summaries.id is not an int");
}

std::string ShotGridBackend::appendFields(
    std::string url, const ShotGridSchema &schema, const Ir &ir) const {
    const std::vector<std::string> *domainFields =
        ir.fields ? &(*ir.fields) : nullptr;
    const std::vector<std::string> fields = shotgridFieldNames(schema, domainFields);
    url += "?fields=";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) {
            url += ',';
        }
        url += fields[i];
    }
    return url;
}

std::string ShotGridBackend::appendSort(std::string url, const ShotGridSchema &schema) const {
    const ShotGridSchemaField *idField = schema.findField("id");
    return url + "&sort=" + (idField ? idField->shotgrid : "id");
}

std::string ShotGridBackend::appendPage(std::string url, const Ir &ir) const {
    const int pageSize = ir.limit && *ir.limit < static_cast<std::size_t>(kMaxPageSize)
                             ? static_cast<int>(*ir.limit)
                             : kMaxPageSize;
    return url + "&page%5Bsize%5D=" + std::to_string(pageSize) + "&page%5Bnumber%5D=1";
}

void ShotGridBackend::logSearch(const std::string &url, const nlohmann::json &body) const {
    if (searchLog_) {
        searchLog_(url + " " + body.dump());
    }
}

httplib::Result ShotGridBackend::sendSearch(const std::string &url, const nlohmann::json &body) {
    const bool hashFilter = body.at("filters").is_object();
    const char *contentType = hashFilter ? kContentTypeHash : kContentTypeArray;
    auto client = openHttpClient(site_.base_url);
    const httplib::Headers headers = {
        {"Authorization", "Bearer " + *access_token_},
        {"Accept", kAcceptJson},
    };
    return client->Post(url.c_str(), headers, body.dump(), contentType);
}

httplib::Result ShotGridBackend::postSearch(const std::string &url, const nlohmann::json &body) {
    logSearch(url, body);
    httplib::Result response = sendSearch(url, body);
    if (!response) {
        throwIfNoHttpResponse("search request failed", response);
    }
    if (response->status != 401) {
        return response;
    }
    forgetTokens();
    connectIfNeeded();
    response = sendSearch(url, body);
    if (!response) {
        throwIfNoHttpResponse("search request failed after re-auth", response);
    }
    return response;
}

void ShotGridBackend::throwIfSearchFailed(const httplib::Result &response) const {
    if (response->status < 200 || response->status >= 300) {
        throw Error(
            "shotgrid: search HTTP " + std::to_string(response->status) + ": " + response->body);
    }
}

Page ShotGridBackend::readRecords(
    const httplib::Result &response, const ShotGridSchema &schema, const Ir &ir) const {
    const nlohmann::json payload = nlohmann::json::parse(response->body);
    if (!payload.contains("data") || !payload["data"].is_array()) {
        throw Error("shotgrid: search response missing data");
    }
    Page page;
    for (const nlohmann::json &row : payload["data"]) {
        page.rows.push_back(shotgridRecordFromJson(row, schema));
    }
    if (ir.limit && !page.rows.empty() && page.rows.size() == *ir.limit) {
        page.next_cursor = std::to_string(std::get<std::int64_t>(page.rows.back().at("id")));
    }
    return page;
}

Page ShotGridBackend::buildPage(
    const httplib::Result &response, const ShotGridSchema &schema, const Ir &ir) const {
    throwIfSearchFailed(response);
    return readRecords(response, schema, ir);
}

Page ShotGridBackend::find(const Ir &ir) {
    const ShotGridSchema &schema = extractSchemaFromIR(ir);
    connectIfNeeded();
    std::string url = buildEntityUrl(schema);
    url = appendFields(url, schema, ir);
    url = appendSort(url, schema);
    url = appendPage(url, ir);
    const nlohmann::json body = buildRequestBody(ir);
    const httplib::Result response = postSearch(url, body);
    return buildPage(response, schema, ir);
}

std::size_t ShotGridBackend::count(const Ir &ir) {
    const ShotGridSchema &schema = extractSchemaFromIR(ir);
    connectIfNeeded();
    const std::string url = buildSummarizeUrl(schema);
    const nlohmann::json body = buildSummarizeBody(ir);
    const httplib::Result response = postSearch(url, body);
    throwIfSearchFailed(response);
    return readSummarizeCount(response);
}

} // namespace pms
