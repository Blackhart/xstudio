// SPDX-License-Identifier: Apache-2.0
/**
 * @file caching_backend.cpp
 * @brief TTL + LRU cache for limited find results.
 */

#include "pms/caching_backend.hpp"

#include "pms/error.hpp"

#include <sstream>
#include <utility>
#include <variant>

namespace pms {
namespace {

/**
 * @brief Renders one scalar for a debug page dump.
 * @param value Domain field value.
 * @return Quoted string, decimal int, or `YYYY-MM-DD`.
 */
std::string formatValue(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return "\"" + *text + "\"";
    }
    if (const auto *number = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*number);
    }
    const Date &date = std::get<Date>(value);
    std::ostringstream out;
    out << date.year << '-' << date.month << '-' << date.day;
    return out.str();
}

/**
 * @brief Renders one record as a flat field map.
 * @param row Domain row.
 * @return Text such as `{ id: 1, name: "paw01" }`.
 */
std::string formatRecord(const Record &row) {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto &[key, value] : row.fields) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << ' ' << key << ": " << formatValue(value);
    }
    if (!first) {
        out << ' ';
    }
    out << '}';
    return out.str();
}

/**
 * @brief Renders page rows for a debug trace.
 * @param page Cached or freshly fetched page.
 * @return Multi-line list, or `[]` when empty.
 */
std::string formatPage(const Page &page) {
    std::ostringstream out;
    out << "rows (" << page.rows.size() << "), next_cursor="
        << (page.next_cursor ? ("\"" + *page.next_cursor + "\"") : std::string("-")) << '\n';
    if (page.rows.empty()) {
        out << "[]";
        return out.str();
    }
    out << "[\n";
    for (std::size_t index = 0; index < page.rows.size(); ++index) {
        out << "  " << formatRecord(page.rows[index]);
        if (index + 1 < page.rows.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << ']';
    return out.str();
}

} // namespace

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

void CachingBackend::debugTrace(const char *verdict, const Ir &ir, const Page &page) const {
    if (!options_.debug) {
        return;
    }
    std::ostringstream out;
    out << verdict << '\n' << dump(ir) << formatPage(page);
    options_.debug(out.str());
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
    if (!ir.limit) {
        Page page = inner_->find(ir);
        debugTrace("bypass (unlimited)", ir, page);
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
            debugTrace("hit", ir, found->second.page);
            return found->second.page;
        }
        total_bytes_ -= found->second.bytes;
        order_.erase(found->second.order);
        map_.erase(found);
        Page page = store(key, inner_->find(ir), nowPoint);
        debugTrace("miss (expired)", ir, page);
        return page;
    }

    Page page = store(key, inner_->find(ir), nowPoint);
    debugTrace("miss", ir, page);
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
