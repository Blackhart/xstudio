// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/log.cpp
 * @brief Formats Shot Browser trace lines for spdlog.
 */

#include "model/log.hpp"

#include "xstudio/utility/logging.hpp"

#include <algorithm>
#include <cctype>

namespace xstudio::mikros_shotbrowser::log {
namespace {

constexpr std::size_t kDetailLimit = 200;

/**
 * @brief Escapes quotes and backslashes for a quoted log field.
 * @param text Raw text.
 * @return Text safe to wrap in double quotes.
 */
std::string escape(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

/**
 * @brief True when `body` looks like an HTML document.
 * @param body HTTP response body.
 * @return True for a doctype or html tag, ignoring case and leading space.
 */
bool isHtml(const std::string &body) {
    std::size_t start = 0;
    while (start < body.size() && std::isspace(static_cast<unsigned char>(body[start]))) {
        ++start;
    }
    const std::string head = body.substr(start, 15);
    std::string lower;
    lower.reserve(head.size());
    for (const char ch : head) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lower.rfind("<!doctype", 0) == 0 || lower.rfind("<html", 0) == 0;
}

/**
 * @brief Collapses whitespace and caps the length of an error body.
 * @param body Raw HTTP or exception detail.
 * @return A single short field, or `html response (N bytes)`.
 */
std::string brief(const std::string &body) {
    if (isHtml(body)) {
        return "html response (" + std::to_string(body.size()) + " bytes)";
    }
    std::string flat;
    flat.reserve(std::min(body.size(), kDetailLimit));
    bool space = false;
    for (const char ch : body) {
        if (flat.size() >= kDetailLimit) {
            break;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!space && !flat.empty()) {
                flat.push_back(' ');
                space = true;
            }
            continue;
        }
        space = false;
        flat.push_back(ch);
    }
    return flat;
}

/**
 * @brief Reads an HTTP status that follows `"HTTP "` in a shotgrid message.
 * @param text Text after the `shotgrid: ` prefix.
 * @param status Receives the parsed status when one is present.
 * @param detail Receives the body after `": "`, or the whole text when no status.
 * @return True when `text` contains an HTTP status.
 */
bool httpStatus(const std::string &text, int &status, std::string &detail) {
    const std::string marker = "HTTP ";
    const std::size_t at = text.find(marker);
    if (at == std::string::npos) {
        detail = text;
        return false;
    }
    const std::size_t digits = at + marker.size();
    if (digits >= text.size() || !std::isdigit(static_cast<unsigned char>(text[digits]))) {
        detail = text;
        return false;
    }
    status = 0;
    std::size_t cursor = digits;
    while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor]))) {
        status = status * 10 + (text[cursor] - '0');
        ++cursor;
    }
    const std::string sep = ": ";
    const std::size_t body = text.find(sep, cursor);
    detail = body == std::string::npos ? std::string() : text.substr(body + sep.size());
    return true;
}

} // namespace

void info(std::string_view message) { spdlog::info("[mikros] {}", message); }

void error(std::string_view message) { spdlog::error("[mikros] {}", message); }

void select(const char *entity, bool chosen, std::int64_t id, const std::string &name) {
    if (!chosen) {
        info(std::string("select ") + entity + " none");
        return;
    }
    info(
        "select " + std::string(entity) + " id=" + std::to_string(id) + " name=\"" +
        escape(name) + "\"");
}

void drop(const std::string &catalog, std::uint64_t request) {
    info(
        "drop catalog=" + catalog + " request=" + std::to_string(request) + " reason=stale");
}

void failure(const std::exception &failure) {
    const std::string what = failure.what();
    constexpr std::string_view kPrefix = "shotgrid: ";
    if (what.rfind(kPrefix, 0) == 0) {
        const std::string rest = what.substr(kPrefix.size());
        int status = 0;
        std::string detail;
        const bool http = httpStatus(rest, status, detail);
        error(
            "shotgrid error status=" + (http ? std::to_string(status) : std::string("-")) +
            " detail=\"" + escape(brief(http ? detail : rest)) + "\"");
        return;
    }
    error("dal error detail=\"" + escape(what) + "\"");
}

} // namespace xstudio::mikros_shotbrowser::log
