// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/caching_backend.hpp
 * @brief Backend decorator that caches limited find() and count() with TTL and LRU.
 */
#pragma once

#include "pms/backend.hpp"
#include "pms/ir.hpp"
#include "pms/page.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace pms {

/**
 * @brief Bounds and lifetime for CachingBackend entries.
 *
 * Limited finds and every count are stored. Unlimited finds forward to the
 * inner backend so unbounded catalogs never fill the cache.
 */
struct CacheOptions {
    std::chrono::seconds ttl{300};             ///< Lifetime of each cached entry. Five minutes.
    std::size_t max_bytes{64 * 1024 * 1024};   ///< Approximate byte budget for entries.
    /// Name of the wrapped backend, forwarded on a cache miss (`fake`, `shotgrid`).
    std::string backend_name{"backend"};
    /// Incoming IR, called before the cache lookup. Empty disables the trace.
    std::function<void(const Ir &)> trace_ir;
    /// Cache verdict: `hit`, `miss`, `miss expired`, or `bypass`.
    std::function<void(const char *verdict, const std::string &entity)> trace_cache;
    /// Called only when the inner backend runs. Receives `backend_name` and the entity.
    std::function<void(const std::string &backend, const std::string &entity)> trace_fallback;
    /// Page returned to the caller. `source` is `cache` or `backend`.
    std::function<void(const char *source, const std::string &entity, const Page &page)>
        trace_page;
};

/**
 * @brief Caches limited find pages and count totals keyed by dump(ir).
 *
 * Thread-safe. Misses call the inner backend. Entries expire after `ttl`, and
 * the least-recently-used entry is dropped when `max_bytes` is exceeded. Call
 * clear() on project change or explicit refresh.
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
     * @brief Returns a cached count when valid, else fetches.
     * @param ir Validated count query.
     * @return Match count.
     */
    std::size_t count(const Ir &ir) override;

    /**
     * @brief Drops every cached entry immediately.
     */
    void clear();

    /**
     * @brief Returns how many entries are currently stored.
     * @return Entry count under the mutex.
     */
    std::size_t size() const;

  private:
    using Payload = std::variant<Page, std::size_t>; ///< Find page or count total.

    struct Entry {
        Payload payload;                                ///< Cached response.
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
     * @brief Estimates the memory cost of a count entry.
     * @return Fixed scalar size.
     */
    static std::size_t approxCountBytes();

    /**
     * @brief Evicts least-recently-used entries until within capacity.
     * @note Caller must hold mutex_.
     */
    void evictIfNeeded();

    /**
     * @brief Stores `payload` under `key`, runs LRU eviction, and returns it.
     * @note Caller must hold mutex_.
     */
    Payload store(
        const std::string &key,
        Payload payload,
        std::chrono::steady_clock::time_point nowPoint);

    /**
     * @brief Returns the current time from clock_ or steady_clock.
     * @return Time point used for TTL checks.
     */
    std::chrono::steady_clock::time_point now() const;

    /** @brief Forwards the incoming IR when `trace_ir` is set. */
    void traceQuery(const Ir &ir) const;
    /** @brief Forwards a cache verdict when `trace_cache` is set. */
    void traceLookup(const char *verdict, const Ir &ir) const;
    /** @brief Forwards a miss to the inner backend when `trace_fallback` is set. */
    void traceMiss(const Ir &ir) const;
    /**
     * @brief Forwards the page handed back to the caller.
     * @param source `cache` or `backend`.
     */
    void traceResult(const char *source, const Ir &ir, const Page &page) const;

    Backend *inner_ = nullptr; ///< Non-owning wrapped backend.
    CacheOptions options_;     ///< TTL and capacity.
    Clock clock_;              ///< Optional test clock.
    mutable std::mutex mutex_; ///< Guards map_, order_, and total_bytes_.
    std::unordered_map<std::string, Entry> map_; ///< Key = dump(ir).
    std::list<std::string> order_; ///< LRU order: front = most recently used.
    std::size_t total_bytes_ = 0;  ///< Sum of Entry::bytes.
};

} // namespace pms
