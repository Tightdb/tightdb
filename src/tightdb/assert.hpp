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
#ifndef TIGHTDB_ASSERT_HPP
#define TIGHTDB_ASSERT_HPP

#include <tightdb/config.h>


#ifdef TIGHTDB_DEBUG
#  include <tightdb/terminate.hpp>
#  define TIGHTDB_ASSERT(condition) \
    (condition ? static_cast<void>(0) : tightdb::terminate("Assertion failed: " #condition, __FILE__, __LINE__))
#else
#  define TIGHTDB_ASSERT(condition) static_cast<void>(0)
#endif


#ifdef TIGHTDB_HAVE_CXX11_STATIC_ASSERT
#  define TIGHTDB_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#  define TIGHTDB_STATIC_ASSERT(condition, message) typedef \
    tightdb::static_assert_dummy<sizeof(tightdb::TIGHTDB_STATIC_ASSERTION_FAILURE<bool(condition)>)> \
    TIGHTDB_JOIN(_tightdb_static_assert_, __LINE__) TIGHTDB_UNUSED
#  define TIGHTDB_JOIN(x,y) TIGHTDB_JOIN2(x,y)
#  define TIGHTDB_JOIN2(x,y) x ## y
namespace tightdb {
    template<bool> struct TIGHTDB_STATIC_ASSERTION_FAILURE;
    template<> struct TIGHTDB_STATIC_ASSERTION_FAILURE<true> {};
    template<int> struct static_assert_dummy {};
}
#endif


#endif // TIGHTDB_ASSERT_HPP
