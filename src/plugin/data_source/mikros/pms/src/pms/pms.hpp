// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/pms.hpp
 * @brief Umbrella header for the production-tracking query library.
 *
 * Include this header to build and run a query. This slice exposes `project`
 * and `find`. ShotGrid names never appear here.
 */
#pragma once

#include "pms/backend.hpp"
#include "pms/caching_backend.hpp"
#include "pms/env.hpp"
#include "pms/error.hpp"
#include "pms/expr_json.hpp"
#include "pms/page.hpp"
#include "pms/query.hpp"
#include "pms/session.hpp"
#include "pms/shared_session.hpp"
#include "pms/shotgrid/backend.hpp"
#include "pms/shotgrid/mapping.hpp"

/**
 * @namespace pms
 * @brief Domain query language for production tracking.
 *
 * A query is built from a generated function such as Project(), then executed
 * with find(). Limit() and After() on the same query select a page. Callers may
 * use a local Session or the process-wide `sharedSession()` (schema + cache)
 * that Mikros plugins share. It is not a user login.
 */
