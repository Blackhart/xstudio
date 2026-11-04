// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/value.hpp
 * @brief Scalar values and the flat record returned by find().
 */
#pragma once

#include "pms/error.hpp"

#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace pms {

/**
 * @brief Calendar day with no time and no time zone.
 *
 * A date is valid when month and day exist in that year, including leap days.
 */
struct Date {
    int year = 0;  ///< Four-digit year as parsed from the text.
    int month = 0; ///< Month in the range 1-12.
    int day = 0;   ///< Day in the range valid for `month`.

    /**
     * @brief Compares two dates by year, month, and day.
     * @param other Date to compare with.
     * @return True when the three fields are equal.
     */
    bool operator==(const Date &other) const {
        return year == other.year && month == other.month && day == other.day;
    }
};

/**
 * @brief Scalar stored in a filter leaf or in a record field.
 *
 * `string` maps to `std::string`, `int` to `std::int64_t`, and `date` to Date.
 * This slice has no nested record: a project row is a flat map of scalars.
 */
using Value = std::variant<std::string, std::int64_t, Date>;

/**
 * @brief One row returned by find(), keyed by the domain YAML names.
 *
 * A missing key means the backend did not send it. The library does not insert
 * a null in its place. `id` and `name` are required and checked after find().
 */
struct Record {
    std::map<std::string, Value> fields; ///< Domain keys and their scalar values.

    /**
     * @brief Returns the value stored at `key`.
     * @param key Domain field name, such as `"name"` or `"start_date"`.
     * @return The stored scalar.
     * @throw std::out_of_range When `key` is absent.
     */
    const Value &at(const std::string &key) const { return fields.at(key); }

    /**
     * @brief Looks up `key` without throwing.
     * @param key Domain field name.
     * @return Iterator to the entry, or `end()` when the key is absent.
     */
    std::map<std::string, Value>::const_iterator find(const std::string &key) const {
        return fields.find(key);
    }
};

/**
 * @brief Rows returned by find().
 *
 * An empty vector means the filter matched nothing. That is not an error.
 */
using Records = std::vector<Record>;

/**
 * @brief Reports whether `year` is a Gregorian leap year.
 * @param year Year to test.
 * @return True for years divisible by 4, except centuries not divisible by 400.
 */
inline bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * @brief Returns the number of days in a month.
 * @param year Year, used for February.
 * @param month Month in the range 1-12.
 * @return The day count, or 0 when `month` is outside 1-12.
 */
inline int daysInMonth(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    if (month < 1 || month > 12) {
        return 0;
    }
    return days[month];
}

/**
 * @brief Parses an ISO calendar date.
 * @param text Text in the exact form `YYYY-MM-DD`.
 * @return The parsed day.
 * @throw Error When the text is not that form or the day does not exist.
 */
inline Date parseDate(const std::string &text) {
    const auto bad = [&text]() { throw Error("invalid date \"" + text + "\""); };
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        bad();
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            bad();
        }
    }
    const int year = std::stoi(text.substr(0, 4));
    const int month = std::stoi(text.substr(5, 2));
    const int day = std::stoi(text.substr(8, 2));
    if (day < 1 || day > daysInMonth(year, month)) {
        bad();
    }
    return Date{year, month, day};
}

} // namespace pms
