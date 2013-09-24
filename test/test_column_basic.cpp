#include "testsettings.hpp"
#ifdef TEST_COLUMN_BASIC

#include <UnitTest++.h>

#include <tightdb/column_basic.hpp>

#include "testsettings.hpp"

using namespace tightdb;


TEST(ColumnBasic_LowerUpperBound)
{
    // Create column with sorted members
    BasicColumn<int> col;
    col.add(5);
    for (size_t i = 5; i < 100; i += 5)
        col.add(i);

    // before first entry
    CHECK_EQUAL(0, col.lower_bound(0));
    CHECK_EQUAL(0, col.upper_bound(0));

    // first entry (duplicate)
    CHECK_EQUAL(0, col.lower_bound(5));
    CHECK_EQUAL(2, col.upper_bound(5));

    // middle entry
    CHECK_EQUAL(10, col.lower_bound(50));
    CHECK_EQUAL(11, col.upper_bound(50));

    // non-existent middle entry
    CHECK_EQUAL(11, col.lower_bound(52));
    CHECK_EQUAL(11, col.upper_bound(52));

    // last entry
    CHECK_EQUAL(19, col.lower_bound(95));
    CHECK_EQUAL(20, col.upper_bound(95));

    // beyond last entry
    CHECK_EQUAL(20, col.lower_bound(96));
    CHECK_EQUAL(20, col.upper_bound(96));

    // Clean up
    col.destroy();
}

#endif // TEST_COLUMN_BASIC
