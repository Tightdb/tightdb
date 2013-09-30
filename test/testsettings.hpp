

#ifndef TESTSETTINGS_H
#define TESTSETTINGS_H

#ifndef TEST_DURATION
    #define TEST_DURATION 0    // Only brief unit tests. < 1 sec
    //#define TEST_DURATION 1  // All unit tests, plus monkey tests. ~1 minute
    //#define TEST_DURATION 2  // Same as 2, but longer monkey tests. 8 minutes
    //#define TEST_DURATION 3
#endif

// Robustness tests are not enable by default, because they interfere badly with Valgrind.
// #define TEST_ROBUSTNESS

// Wrap pthread function calls with the pthread bug finding tool (program execution will be slower) by
// #including pthread_test.h. Works both in debug and release mode.
//#define TIGHTDB_PTHREADS_TEST

#define TEST_COLUMN_MIXED
#define TEST_ALLOC
#define TEST_ARRAY
#define TEST_ARRAY_BINARY
#define TEST_ARRAY_BLOB
#define TEST_ARRAY_FLOAT
#define TEST_ARRAY_STRING
#define TEST_ARRAY_STRING_LONG
#define TEST_COLUMN
#define TEST_COLUMN_BASIC
#define TEST_COLUMN_BINARY
#define TEST_COLUMN_FLOAT
#define TEST_COLUMN_MIXED
#define TEST_COLUMN_STRING
#define TEST_FILE
#define TEST_GROUP
#define TEST_INDEX_STRING
#define TEST_LANG_BIND_HELPER
#define TEST_QUERY
#define TEST_SHARED
#define TEST_STRING_DATA
#define TEST_TABLE
#define TEST_TABLE_VIEW
#define TEST_THREAD

// TEST_TRANSACTIONS temporarily disabled because it fails on Windows
#ifndef _WIN32
#define TEST_TRANSACTIONS
#endif

#define TEST_REPLICATION
#define TEST_UTF8
//#define TEST_TRANSACTIONS_LASSE // Takes a long time
//#define TEST_INDEX // not implemented yet

// Bypass an overflow bug in BinaryData. Todo/fixme
#define TIGHTDB_BYPASS_BINARYDATA_BUG

// Bypass as crash when doing optimize+set_index+clear+add. Todo/fixme
#define TIGHTDB_BYPASS_OPTIMIZE_CRASH_BUG

#endif
