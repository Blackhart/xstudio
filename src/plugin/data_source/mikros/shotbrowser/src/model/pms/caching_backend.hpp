// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/caching_backend.hpp
 * @brief Backend decorator that caches limited find() results with TTL and LRU.
 */
#pragma once

#include "pms/backend.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pms {

/**
 * @brief Bounds and lifetime for CachingBackend entries.
 *
 * Only limited finds are stored. Unlimited finds forward to the inner backend
 * so unbounded catalogs never fill the cache.
 */
struct CacheOptions {
    std::chrono::seconds ttl{60};      ///< Lifetime of each cached page.
    std::size_t max_entries{128};      ///< Hard cap on distinct queries.
    std::size_t max_bytes{4 * 1024 * 1024}; ///< Approximate byte budget for pages.
    /// Optional sink for hit/miss traces (query dump + page payload).
    std::function<void(const std::string &)> debug;
};

/**
 * @brief Caches limited find results keyed by dump(ir).
 *
 * Thread-safe. Misses call the inner backend. Entries expire after `ttl`, and
 * the least-recently-used entry is dropped when max_entries or max_bytes is
 * exceeded. Call clear() on project change or explicit refresh.
 */
class CachingBackend : public Backend {
  public:
    /**
     * @brief Clock used to evaluate TTL. Defaults to steady_clock::now.
     */
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    /**
     * @brief Wraps an existing backend.
     * @param inner Execution target. Must outlive this decorator.
     * @param options TTL and capacity bounds.
     * @param clock Optional clock for tests. Empty uses steady_clock::now.
     */
    explicit CachingBackend(
        Backend &inner,
        CacheOptions options = {},
        Clock clock = {});

    /**
     * @brief Returns a cached page when limited and valid, else fetches.
     * @param ir Validated find query. Cached only when `limit` is set.
     * @return Page of rows and an optional next cursor.
     */
    Page find(const Ir &ir) override;

    /**
     * @brief Drops every cached page immediately.
     */
    void clear();

    /**
     * @brief Returns how many pages are currently stored.
     * @return Entry count under the mutex.
     */
    std::size_t size() const;

  private:
    struct Entry {
        Page page;                                      ///< Cached response.
        std::chrono::steady_clock::time_point expires;  ///< Absolute expiry.
        std::size_t bytes = 0;                          ///< Approximate size.
        std::list<std::string>::iterator order;         ///< LRU node for this key.
    };

    /**
     * @brief Estimates the memory cost of a page for the byte budget.
     * @param page Cached page.
     * @return Rough byte count of rows and cursor.
     */
    static std::size_t approxBytes(const Page &page);

    /**
     * @brief Evicts least-recently-used entries until within capacity.
     * @note Caller must hold mutex_.
     */
    void evictIfNeeded();

    /**
     * @brief Stores `page` under `key`, runs LRU eviction, and returns `page`.
     * @note Caller must hold mutex_.
     */
    Page store(
        const std::string &key,
        Page page,
        std::chrono::steady_clock::time_point nowPoint);

    /**
     * @brief Returns the current time from clock_ or steady_clock.
     * @return Time point used for TTL checks.
     */
    std::chrono::steady_clock::time_point now() const;

    /**
     * @brief Emits a debug line when options_.debug is set.
     * @param verdict hit, miss, expired, or bypass.
     * @param ir Query that keyed the lookup.
     * @param page Rows returned to the caller.
     */
    void debugTrace(const char *verdict, const Ir &ir, const Page &page) const;

    Backend *inner_ = nullptr; ///< Non-owning wrapped backend.
    CacheOptions options_;     ///< TTL and capacity.
    Clock clock_;              ///< Optional test clock.
    mutable std::mutex mutex_; ///< Guards map_, order_, and total_bytes_.
    std::unordered_map<std::string, Entry> map_; ///< Key = dump(ir).
    std::list<std::string> order_; ///< LRU order: front = most recently used.
    std::size_t total_bytes_ = 0;  ///< Sum of Entry::bytes.
};

} // namespace pms
