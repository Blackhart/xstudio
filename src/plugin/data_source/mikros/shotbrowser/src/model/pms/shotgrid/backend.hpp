// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/shotgrid/backend.hpp
 * @brief ShotGrid REST backend for validated find queries.
 */
#pragma once

#include "pms/backend.hpp"
#include "pms/shotgrid/mapping.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <variant>

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
 * ShotGrid supports this for human logins. Prefer script auth for a desktop
 * plugin service account unless you need per-user permissions.
 */
struct ShotGridUserAuth {
    std::string username; ///< ShotGrid login.
    std::string password; ///< ShotGrid password.
    std::string client_id;     ///< OAuth client_id (often a script name still required).
    std::string client_secret; ///< OAuth client_secret for that client.
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
 * Translates domain IR into `/api/v1/entity/{Type}/_search` filters, maps
 * returned attributes back to domain fields, and applies keyset pagination
 * when `ir.limit` is set. Network work is synchronous; call from a worker.
 *
 */
class ShotGridBackend : public Backend {
  public:
    /**
     * @brief Builds a backend bound to a site and a mapping file.
     * @param site Base URL and credentials.
     * @param mapping_path Path to `shotgrid_mapping.yaml`.
     * @throw Error When the mapping file cannot be loaded.
     */
    ShotGridBackend(ShotGridSite site, const std::string &mapping_path);

    /**
     * @brief Builds a backend with an already loaded mapping (tests).
     * @param site Base URL and credentials.
     * @param mapping Domain ↔ ShotGrid map.
     */
    ShotGridBackend(ShotGridSite site, ShotGridMapping mapping);

    /**
     * @brief Returns the loaded mapping.
     * @return Mapping used for translation.
     */
    const ShotGridMapping &mapping() const { return mapping_; }

    /**
     * @brief Returns the configured site (URL + auth).
     * @return Site passed at construction.
     */
    const ShotGridSite &site() const { return site_; }

    /**
     * @brief Runs a validated find against ShotGrid.
     * @param ir Query with optional limit/cursor.
     * @return Page of domain rows.
     * @throw Error When the mapping is incomplete, auth fails, or HTTP fails.
     */
    Page find(const Ir &ir) override;

  private:
    /** @brief Acquires or refreshes the OAuth access token when needed. */
    void ensureToken();

    ShotGridSite site_;
    ShotGridMapping mapping_;
    std::optional<std::string> access_token_;
    std::optional<std::string> refresh_token_;
    std::chrono::steady_clock::time_point token_expires_{};
};

} // namespace pms
