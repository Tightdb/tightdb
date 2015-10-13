/*************************************************************************
 *
 * REALM CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] Realm Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of Realm Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to Realm Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from Realm Incorporated.
 *
 **************************************************************************/
#ifndef REALM_TEST_UTIL_WILDCARD_HPP
#define REALM_TEST_UTIL_WILDCARD_HPP

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <realm/util/features.h>


namespace realm {
namespace test_util {


class wildcard_pattern {
public:
    explicit wildcard_pattern(const std::string& text);

    bool match(const char* begin, const char* end) const noexcept;

    bool match(const char* c_str) const noexcept;

private:
    std::string m_text;

    struct card {
        size_t m_offset, m_size;
        card(size_t begin, size_t end) noexcept;
    };

    // Must contain at least one card. The first, and the last card
    // may be empty strings. All other cards must be non-empty. If
    // there is exactly one card, the pattern matches a string if, and
    // only if the string is equal to the card. Otherwise, the first
    // card must be a prefix of the string, and the last card must be
    // a suffix.
    std::vector<card> m_cards;
};



// Implementation

inline bool wildcard_pattern::match(const char* c_str) const noexcept
{
    const char* begin = c_str;
    const char* end   = begin + std::strlen(c_str);
    return match(begin, end);
}

inline wildcard_pattern::card::card(size_t begin, size_t end) noexcept
{
    m_offset = begin;
    m_size   = end - begin;
}


} // namespace test_util
} // namespace realm

#endif // REALM_TEST_UTIL_WILDCARD_HPP
