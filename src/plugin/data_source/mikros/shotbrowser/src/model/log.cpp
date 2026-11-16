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
std::string escapeQuoted(const std::string &text) {
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
bool bodyIsHtml(const std::string &body) {
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
std::string shortenDetail(const std::string &body) {
    if (bodyIsHtml(body)) {
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
 * @brief HTTP status parsed out of a ShotGrid error message.
 */
struct ParsedHttpStatus {
    bool present = false; ///< True when `code` was read after `"HTTP "`.
    int code = 0;         ///< Status digits, or 0 when `present` is false.
    std::string detail;   ///< Body after `": "`, empty when the separator is absent.
};

/**
 * @brief Reads an HTTP status that follows `"HTTP "` in a shotgrid message.
 * @param text Text after the `shotgrid: ` prefix.
 * @return The status when the digits are present, otherwise `present == false`.
 */
ParsedHttpStatus parseHttpStatus(const std::string &text) {
    ParsedHttpStatus parsed;
    const std::string marker = "HTTP ";
    const std::size_t at = text.find(marker);
    if (at == std::string::npos) {
        return parsed;
    }
    const std::size_t digits = at + marker.size();
    if (digits >= text.size() || !std::isdigit(static_cast<unsigned char>(text[digits]))) {
        return parsed;
    }
    parsed.present = true;
    std::size_t cursor = digits;
    while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor]))) {
        parsed.code = parsed.code * 10 + (text[cursor] - '0');
        ++cursor;
    }
    const std::string sep = ": ";
    const std::size_t body = text.find(sep, cursor);
    parsed.detail = body == std::string::npos ? std::string() : text.substr(body + sep.size());
    return parsed;
}

/**
 * @brief True when the exception text was produced by the ShotGrid backend.
 * @param what `exception::what()`.
 * @return True when the text starts with `shotgrid: `.
 */
bool isShotGridMessage(const std::string &what) {
    constexpr std::string_view kPrefix = "shotgrid: ";
    return what.rfind(kPrefix, 0) == 0;
}

/**
 * @brief Formats a ShotGrid failure as one log line.
 * @param what Exception text, including the `shotgrid: ` prefix.
 * @return `shotgrid error status=... detail="..."`.
 */
std::string formatShotGridFailure(const std::string &what) {
    constexpr std::string_view kPrefix = "shotgrid: ";
    const std::string rest = what.substr(kPrefix.size());
    const ParsedHttpStatus http = parseHttpStatus(rest);
    const std::string status = http.present ? std::to_string(http.code) : "-";
    const std::string detail = http.present ? http.detail : rest;
    return "shotgrid error status=" + status + " detail=\"" + escapeQuoted(shortenDetail(detail)) +
           "\"";
}

/**
 * @brief Formats a non-ShotGrid failure as one log line.
 * @param what Exception text.
 * @return `dal error detail="..."`.
 */
std::string formatDalFailure(const std::string &what) {
    return "dal error detail=\"" + escapeQuoted(what) + "\"";
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
        escapeQuoted(name) + "\"");
}

void drop(const std::string &catalog, std::uint64_t request) {
    info(
        "drop catalog=" + catalog + " request=" + std::to_string(request) + " reason=stale");
}

void failure(const std::exception &caught) {
    const std::string what = caught.what();
    if (isShotGridMessage(what)) {
        error(formatShotGridFailure(what)); ///< ShotGrid text keeps its HTTP status.
        return;
    }
    error(formatDalFailure(what));
}

} // namespace xstudio::mikros_shotbrowser::log
