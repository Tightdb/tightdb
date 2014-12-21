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
#ifndef TIGHTDB_TEST_CRYPT_KEY_HPP
#define TIGHTDB_TEST_CRYPT_KEY_HPP

#include <stdint.h>
#include <stdlib.h>

namespace {

const char* crypt_key(bool always=false)
{
    static const char key[] = "12345678901234567890123456789011234567890123456789012345678901";
    if (always) {
#ifdef TIGHTDB_ENABLE_ENCRYPTION
        return key;
#else
        return 0;
#endif
    }

    const char* str = getenv("UNITTEST_ENCRYPT_ALL");
    if (str && *str) {
        return key;
    }

    return 0;
}

} // anonymous namespace

#endif // TIGHTDB_TEST_CRYPT_KEY_HPP
