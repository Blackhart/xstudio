// SPDX-License-Identifier: Apache-2.0
/**
 * @file caching_backend.cpp
 * @brief TTL + LRU cache for limited find results.
 */

#include "pms/caching_backend.hpp"

#include "pms/error.hpp"

#include <utility>

namespace pms {

CachingBackend::CachingBackend(Backend &inner, CacheOptions options, Clock clock)
    : inner_(&inner), options_(std::move(options)), clock_(std::move(clock)) {
    if (options_.max_entries == 0) {
        throw Error("caching backend: max_entries must be greater than zero");
    }
    if (options_.max_bytes == 0) {
        throw Error("caching backend: max_bytes must be greater than zero");
    }
}

std::chrono::steady_clock::time_point CachingBackend::now() const {
    if (clock_) {
        return clock_();
    }
    return std::chrono::steady_clock::now();
}

void CachingBackend::traceQuery(const Ir &ir) const {
    if (options_.trace_ir) {
        options_.trace_ir(ir);
    }
}

void CachingBackend::traceLookup(const char *verdict, const Ir &ir) const {
    if (options_.trace_cache) {
        options_.trace_cache(verdict, ir.entity);
    }
}

void CachingBackend::traceMiss(const Ir &ir) const {
    if (options_.trace_fallback) {
        options_.trace_fallback(options_.backend_name, ir.entity);
    }
}

void CachingBackend::traceResult(const char *source, const Ir &ir, const Page &page) const {
    if (options_.trace_page) {
        options_.trace_page(source, ir.entity, page);
    }
}

std::size_t CachingBackend::approxBytes(const Page &page) {
    std::size_t bytes = sizeof(Page);
    for (const Record &row : page.rows) {
        bytes += sizeof(Record);
        for (const auto &[key, value] : row.fields) {
            bytes += key.size();
            if (const auto *text = std::get_if<std::string>(&value)) {
                bytes += text->size();
            } else {
                bytes += 16;
            }
        }
    }
    if (page.next_cursor) {
        bytes += page.next_cursor->size();
    }
    return bytes;
}

void CachingBackend::evictIfNeeded() {
    while ((!map_.empty()) &&
           (map_.size() > options_.max_entries || total_bytes_ > options_.max_bytes)) {
        const std::string &victim = order_.back();
        const auto found = map_.find(victim);
        if (found != map_.end()) {
            total_bytes_ -= found->second.bytes;
            map_.erase(found);
        }
        order_.pop_back();
    }
}

/**
 * @brief Inserts a page under `key` and returns it.
 * @note Caller must hold mutex_.
 */
Page CachingBackend::store(
    const std::string &key,
    Page page,
    std::chrono::steady_clock::time_point nowPoint) {
    Entry entry;
    entry.page = page;
    entry.expires = nowPoint + options_.ttl;
    entry.bytes = approxBytes(page);
    order_.push_front(key);
    entry.order = order_.begin();
    total_bytes_ += entry.bytes;
    map_[key] = std::move(entry);
    evictIfNeeded();
    return page;
}

Page CachingBackend::find(const Ir &ir) {
    std::lock_guard<std::mutex> lock(mutex_);
    traceQuery(ir);
    if (!ir.limit) {
        traceLookup("bypass", ir);
        traceMiss(ir);
        Page page = inner_->find(ir);
        traceResult("backend", ir, page);
        return page;
    }

    const std::string key = dump(ir);
    const auto nowPoint = now();
    const auto found = map_.find(key);
    if (found != map_.end()) {
        if (found->second.expires > nowPoint) {
            order_.erase(found->second.order);
            order_.push_front(key);
            found->second.order = order_.begin();
            traceLookup("hit", ir);
            traceResult("cache", ir, found->second.page);
            return found->second.page;
        }
        total_bytes_ -= found->second.bytes;
        order_.erase(found->second.order);
        map_.erase(found);
        traceLookup("miss expired", ir);
        traceMiss(ir);
        Page page = store(key, inner_->find(ir), nowPoint);
        traceResult("backend", ir, page);
        return page;
    }

    traceLookup("miss", ir);
    traceMiss(ir);
    Page page = store(key, inner_->find(ir), nowPoint);
    traceResult("backend", ir, page);
    return page;
}

void CachingBackend::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    map_.clear();
    order_.clear();
    total_bytes_ = 0;
}

std::size_t CachingBackend::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.size();
}

} // namespace pms
