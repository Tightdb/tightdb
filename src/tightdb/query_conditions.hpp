/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of TightDB Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to TightDB Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from TightDB Incorporated.
 *
 **************************************************************************/
#ifndef TIGHTDB_QUERY_CONDITIONS_HPP
#define TIGHTDB_QUERY_CONDITIONS_HPP

#include <stdint.h>
#include <string>

#include <tightdb/binary_data.hpp>
#include <tightdb/utf8.hpp>

namespace tightdb {

enum {cond_Equal, cond_NotEqual, cond_Greater, cond_GreaterEqual, cond_Less, cond_LessEqual, cond_None, cond_Count};


// Does v2 contain v1?
struct Contains {
    bool operator()(StringData v1, const char*, const char*, StringData v2) const { return v2.contains(v1); }
    bool operator()(BinaryData v1, BinaryData v2) const { return v2.contains(v1); }
};

// Does v2 begin with v1?
struct BeginsWith {
    bool operator()(StringData v1, const char*, const char*, StringData v2) const { return v2.begins_with(v1); }
    bool operator()(BinaryData v1, BinaryData v2) const { return v2.begins_with(v1); }
};

// Does v2 end with v1?
struct EndsWith {
    bool operator()(StringData v1, const char*, const char*, StringData v2) const { return v2.ends_with(v1); }
    bool operator()(BinaryData v1, BinaryData v2) const { return v2.ends_with(v1); }
};

struct Equal {
    bool operator()(const bool v1, const bool v2) const { return v1 == v2; }

    // To avoid a "performance warning" in VC++
    bool operator()(const int64_t v1, const bool v2) const { return (v1 != 0) == v2; }

    bool operator()(StringData v1, const char*, const char*, StringData v2) const { return v1 == v2; }
    bool operator()(BinaryData v1, BinaryData v2) const { return v1 == v2; }

    template<class T> bool operator()(const T& v1, const T& v2) const {return v1 == v2;}
    static const int condition = cond_Equal;
    bool can_match(int64_t v, int64_t lbound, int64_t ubound) { return (v >= lbound && v <= ubound); }
    bool will_match(int64_t v, int64_t lbound, int64_t ubound) { return (v == 0 && ubound == 0 && lbound == 0); }
};

struct NotEqual {
    bool operator()(StringData v1, const char*, const char*, StringData v2) const { return v1 != v2; }
    bool operator()(BinaryData v1, BinaryData v2) const { return v1 != v2; }
    template<class T> bool operator()(const T& v1, const T& v2) const { return v1 != v2; }
    static const int condition = cond_NotEqual;
    bool can_match(int64_t v, int64_t lbound, int64_t ubound) { return !(v == 0 && ubound == 0 && lbound == 0); }
    bool will_match(int64_t v, int64_t lbound, int64_t ubound) { return (v > ubound || v < lbound); }
};

// Does v2 contain v1?
struct ContainsIns {
    bool operator()(StringData v1, const char* v1_upper, const char* v1_lower, StringData v2) const
    {
        return search_case_fold(v2, v1_upper, v1_lower, v1.size()) != v2.size();
    }
    static const int condition = -1;
};

// Does v2 begin with v1?
struct BeginsWithIns {
    bool operator()(StringData v1, const char* v1_upper, const char* v1_lower, StringData v2) const
    {
        return v1.size() <= v2.size() && equal_case_fold(v2.prefix(v1.size()), v1_upper, v1_lower);
    }
    static const int condition = -1;
};

// Does v2 end with v1?
struct EndsWithIns {
    bool operator()(StringData v1, const char* v1_upper, const char* v1_lower, StringData v2) const
    {
        return v1.size() <= v2.size() && equal_case_fold(v2.suffix(v1.size()), v1_upper, v1_lower);
    }
    static const int condition = -1;
};

struct EqualIns {
    bool operator()(StringData v1, const char* v1_upper, const char* v1_lower, StringData v2) const
    {
        return v1.size() == v2.size() && equal_case_fold(v2, v1_upper, v1_lower);
    }
    static const int condition = -1;
};

struct NotEqualIns {
    bool operator()(StringData v1, const char* v1_upper, const char* v1_lower, StringData v2) const
    {
        return v1.size() != v2.size() || !equal_case_fold(v2, v1_upper, v1_lower);
    }
    static const int condition = -1;
};

struct Greater {
    template<class T> bool operator()(const T& v1, const T& v2) const {return v1 > v2;}
    static const int condition = cond_Greater;
    bool can_match(int64_t v, int64_t lbound, int64_t ubound) { static_cast<void>(lbound); return ubound > v; }
    bool will_match(int64_t v, int64_t lbound, int64_t ubound) { static_cast<void>(ubound); return lbound > v; }
};

struct None {
    template<class T> bool operator()(const T& v1, const T& v2) const {static_cast<void>(v1); static_cast<void>(v2); return true;}
    static const int condition = cond_None;
    bool can_match(int64_t v, int64_t lbound, int64_t ubound) {static_cast<void>(lbound); static_cast<void>(ubound); static_cast<void>(v); return true; }
    bool will_match(int64_t v, int64_t lbound, int64_t ubound) {static_cast<void>(lbound); static_cast<void>(ubound); static_cast<void>(v); return true; }

};

struct Less {
    template<class T> bool operator()(const T& v1, const T& v2) const {return v1 < v2;}
    static const int condition = cond_Less;
    bool can_match(int64_t v, int64_t lbound, int64_t ubound) { static_cast<void>(ubound); return lbound < v; }
    bool will_match(int64_t v, int64_t lbound, int64_t ubound) { static_cast<void>(lbound); return ubound < v; }
};

struct LessEqual {
    template<class T> bool operator()(const T& v1, const T& v2) const {return v1 <= v2;}
    static const int condition = cond_LessEqual;
};

struct GreaterEqual {
    template<class T> bool operator()(const T& v1, const T& v2) const {return v1 >= v2;}
    static const int condition = cond_GreaterEqual;
};
} // namespace tightdb

#endif // TIGHTDB_QUERY_CONDITIONS_HPP
