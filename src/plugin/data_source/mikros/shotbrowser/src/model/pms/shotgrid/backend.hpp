// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/shotgrid/backend.hpp
 * @brief ShotGrid REST backend for validated find and count queries.
 */
#pragma once

#include "pms/backend.hpp"
#include "pms/shotgrid/mapping.hpp"

#include <cpp-httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pms {

/**
 * @brief Script credentials (OAuth client_credentials grant).
 *
 * ShotGrid "script name" → client_id, "script key" → client_secret.
 */
struct ShotGridScriptAuth {
    std::string script_name; ///< OAuth client_id / script name.
    std::string script_key;  ///< OAuth client_secret / application key.
};

/**
 * @brief User credentials (OAuth password grant).
 *
 * The documented password body is username, password, and grant_type.
 * Prefer script auth for a desktop plugin service account.
 */
struct ShotGridUserAuth {
    std::string username; ///< ShotGrid login.
    std::string password; ///< ShotGrid password.
    std::string client_id;     ///< Unused. The password grant does not send a client id.
    std::string client_secret; ///< Unused. The password grant does not send a client secret.
};

/**
 * @brief How ShotGridBackend authenticates.
 *
 * Script is the default for this plugin. User auth is available when the site
 * allows the password grant; the backend still talks HTTP sync, no CAF.
 */
using ShotGridAuth = std::variant<ShotGridScriptAuth, ShotGridUserAuth>;

/**
 * @brief Site URL and credentials for ShotGridBackend.
 */
struct ShotGridSite {
    std::string base_url; ///< Origin only, e.g. `https://studio.shotgunstudio.com`.
    ShotGridAuth auth;    ///< Script or user credentials.
};

/**
 * @brief Executes find() against ShotGrid using a domain↔REST mapping.
 *
 * Calls `POST /api/v1.1/entity/{collection}/_search` as documented at
 * https://developers.shotgridsoftware.com/rest-api/#searching . The collection
 * is the mapped type in plural snake_case. `fields`, `sort`, and `page` are
 * query parameters. The body always contains `filters`: an empty array when the
 * find has no criterion, otherwise a hash filter. Returned attributes are mapped
 * back to domain fields.
 * Keyset pagination uses `greater_than` on id with page number 1. A debug
 * sink logs the JSON body. A notice sink reports a successful token fetch.
 * Network work is synchronous; call from a worker.
 *
 */
class ShotGridBackend : public Backend {
  public:
    /**
     * @brief Builds a backend bound to a site and a mapping file.
     * @param site Base URL and credentials.
     * @param mapping_path Path to the ShotGrid mapping YAML. The DAL passes `SHOTGRID_MAPPING`.
     * @throw Error When the mapping file cannot be loaded.
     */
    ShotGridBackend(ShotGridSite site, const std::string &mapping_path);

    /**
     * @brief Builds a backend with an already loaded mapping (tests).
     * @param site Base URL and credentials.
     * @param mapping Domain ↔ ShotGrid map.
     */
    ShotGridBackend(ShotGridSite site, ShotGridMapping mapping);

    /** @brief Returns the mapping loaded at construction. */
    const ShotGridMapping &getMapping() const { return mapping_; }

    /** @brief Returns the site URL and credentials passed at construction. */
    const ShotGridSite &getSite() const { return site_; }

    /**
     * @brief Records each search path and body before it is sent.
     * @param searchLog Sink. Empty disables the trace. The text has no credentials.
     */
    void setSearchLog(std::function<void(const std::string &)> searchLog);

    /**
     * @brief Records a short line after a token is issued.
     * @param authLog Sink. Empty disables it. The text has no token or secret.
     */
    void setAuthLog(std::function<void(const std::string &)> authLog);

    /**
     * @brief Runs a validated find against ShotGrid.
     * @param ir Query with optional limit/cursor.
     * @return Page of domain rows.
     * @throw Error When the mapping is incomplete, auth fails, or HTTP fails.
     */
    Page find(const Ir &ir) override;

    /**
     * @brief Runs a validated count against ShotGrid `_summarize`.
     * @param ir Count query with the same filters as find().
     * @return Match count from `summaries.id`.
     * @throw Error When the mapping is incomplete, auth fails, or HTTP fails.
     */
    std::size_t count(const Ir &ir) override;

  private:
    /**
     * @brief Reads the ShotGrid schema for the entity named in the IR.
     * @param ir Incoming query.
     * @return Schema. The reference stays valid for the backend lifetime.
     * @throw Error When a name is missing, the URL is empty, or the entity is unknown.
     */
    const ShotGridSchema &extractSchemaFromIR(const Ir &ir) const;

    /** @brief Throws when the site URL is empty. */
    void throwIfBaseUrlEmpty() const;

    /** @brief True when the stored token is still valid for at least 30 seconds. */
    bool hasUsableToken() const;

    /** @brief Fetches a token when none is stored or the current one is about to expire. */
    void connectIfNeeded();

    /**
     * @brief Builds the token request body from the refresh token or the site credentials.
     * @return Form fields for `/auth/access_token`.
     */
    httplib::Params buildAuthParams() const;

    /**
     * @brief Posts the token request.
     * @return HTTP result. The caller stores the token.
     * @throw Error When the connection fails before an HTTP status.
     */
    httplib::Result requestAccessToken();

    /**
     * @brief Saves the access token from a successful auth response.
     * @param response Result of `requestAccessToken`.
     * @throw Error When the status is not 2xx or the token is missing.
     */
    void storeAccessToken(const httplib::Result &response);

    /** @brief Drops the stored tokens so the next connect asks for a new one. */
    void forgetTokens();

    /**
     * @brief Builds the search body. An empty array when the find has no criterion.
     * @param ir Incoming query.
     * @return JSON object sent as the POST body.
     */
    nlohmann::json buildRequestBody(const Ir &ir) const;

    /**
     * @brief Builds `/api/v1.1/entity/{collection}/_search` with no query parameters.
     * @param schema ShotGrid schema for the queried entity.
     * @return The path.
     */
    std::string buildEntityUrl(const ShotGridSchema &schema) const;

    /**
     * @brief Builds `/api/v1.1/entity/{collection}/_summarize` with no query parameters.
     * @param schema ShotGrid schema for the queried entity.
     * @return The path.
     */
    std::string buildSummarizeUrl(const ShotGridSchema &schema) const;

    /**
     * @brief Builds the summarize body: filters plus a count on `id`.
     * @param ir Incoming count query.
     * @return JSON object sent as the POST body.
     */
    nlohmann::json buildSummarizeBody(const Ir &ir) const;

    /**
     * @brief Reads `data.summaries.id` from a summarize response.
     * @param response Successful summarize HTTP result.
     * @return Match count.
     * @throw Error When the summary is missing or not an integer.
     */
    std::size_t readSummarizeCount(const httplib::Result &response) const;

    /**
     * @brief Appends the `fields` query parameter.
     * @param url Path so far.
     * @param schema ShotGrid schema for the queried entity.
     * @param ir Incoming query. Uses `ir.fields` when set.
     * @return URL including `?fields=`.
     */
    std::string appendFields(
        std::string url, const ShotGridSchema &schema, const Ir &ir) const;

    /**
     * @brief Appends the `sort` query parameter. Sort is the mapped id field.
     * @param url Path so far.
     * @param schema ShotGrid schema for the queried entity.
     * @return URL including `&sort=`.
     */
    std::string appendSort(std::string url, const ShotGridSchema &schema) const;

    /**
     * @brief Appends `page[size]` and `page[number]`. Size is the limit, capped at 500. Number is 1.
     * @param url Path so far.
     * @param ir Incoming query.
     * @return URL including the page parameters.
     */
    std::string appendPage(std::string url, const Ir &ir) const;

    /**
     * @brief Posts the search. On 401, connects again and posts once more.
     * @param url Full path and query.
     * @param body Request body from `buildRequestBody`.
     * @return HTTP result. Status is checked by `buildPage`.
     * @throw Error When the connection fails before an HTTP status.
     */
    httplib::Result postSearch(const std::string &url, const nlohmann::json &body);

    /**
     * @brief Posts one search. Does not retry.
     * @param url Full path and query.
     * @param body Request body.
     * @return HTTP result, which may have no status.
     */
    httplib::Result sendSearch(const std::string &url, const nlohmann::json &body);

    /** @brief Logs the search path and body when a sink is set. */
    void logSearch(const std::string &url, const nlohmann::json &body) const;

    /**
     * @brief Turns a search response into a domain page.
     * @param response HTTP result from `postSearch`.
     * @param schema ShotGrid schema used to read each row.
     * @param ir Incoming query. A full page sets `next_cursor` to the last id.
     * @return Domain rows and an optional cursor.
     * @throw Error When the status is not 2xx or `data` is missing.
     */
    Page buildPage(
        const httplib::Result &response, const ShotGridSchema &schema, const Ir &ir) const;

    /**
     * @brief Throws when the search status is not 2xx.
     * @param response HTTP result from `postSearch`.
     */
    void throwIfSearchFailed(const httplib::Result &response) const;

    /**
     * @brief Reads `data` into domain rows and sets the cursor when the page is full.
     * @param response Successful search response.
     * @param schema ShotGrid schema used to read each row.
     * @param ir Incoming query.
     * @return Domain page.
     * @throw Error When `data` is missing.
     */
    Page readRecords(
        const httplib::Result &response, const ShotGridSchema &schema, const Ir &ir) const;

    ShotGridSite site_;
    ShotGridMapping mapping_;
    std::function<void(const std::string &)> searchLog_; ///< Search path and body. Empty when off.
    std::function<void(const std::string &)> authLog_;   ///< Auth result line. Empty when off.
    std::optional<std::string> access_token_;
    std::optional<std::string> refresh_token_;
    std::chrono::steady_clock::time_point token_expires_{};
};

} // namespace pms
