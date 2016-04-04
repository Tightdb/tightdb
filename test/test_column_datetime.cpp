#include "testsettings.hpp"
#ifdef TEST_COLUMN_DATETIME

#include <realm/column_datetime.hpp>
#include <realm.hpp>

#include "test.hpp"

using namespace realm;


// Test independence and thread-safety
// -----------------------------------
//
// All tests must be thread safe and independent of each other. This
// is required because it allows for both shuffling of the execution
// order and for parallelized testing.
//
// In particular, avoid using std::rand() since it is not guaranteed
// to be thread safe. Instead use the API offered in
// `test/util/random.hpp`.
//
// All files created in tests must use the TEST_PATH macro (or one of
// its friends) to obtain a suitable file system path. See
// `test/util/test_path.hpp`.
//
//
// Debugging and the ONLY() macro
// ------------------------------
//
// A simple way of disabling all tests except one called `Foo`, is to
// replace TEST(Foo) with ONLY(Foo) and then recompile and rerun the
// test suite. Note that you can also use filtering by setting the
// environment varible `UNITTEST_FILTER`. See `README.md` for more on
// this.
//
// Another way to debug a particular test, is to copy that test into
// `experiments/testcase.cpp` and then run `sh build.sh
// check-testcase` (or one of its friends) from the command line.


TEST(DateTimeColumn_Basic)
{
    ref_type ref = DateTimeColumn::create(Allocator::get_default());
    DateTimeColumn c(Allocator::get_default(), ref);
    c.add(NewDate(123,123));
    NewDate ndt = c.get(0);
    CHECK(ndt == NewDate(123, 123));
}

TEST(DateTimeColumn_Basic_Nulls)
{
    // Test that default value is null() for nullable column and non-null for non-nullable column
    Table t;
    t.add_column(type_NewDate, "date", false /*nullable*/);
    t.add_column(type_NewDate, "date", true  /*nullable*/);

    t.add_empty_row();
    CHECK(!t.is_null(0, 0));
    CHECK(t.is_null(1, 0));

    CHECK_THROW_ANY(t.set_null(0, 0));
    t.set_null(1, 0);

    CHECK_THROW_ANY(t.set_newdate(0, 0, NewDate(null())));
}

TEST(DateTimeColumn_Relocate)
{
    // Fill so much data in a column that it relocates, to check if relocation propagates up correctly
    Table t;
    t.add_column(type_NewDate, "date", true  /*nullable*/);

    for (unsigned int i = 0; i < 10000; i++) {
        t.add_empty_row();
        t.set_newdate(0, i, NewDate(i, i));
    }
}

TEST(DateTimeColumn_Compare)
{
    ref_type ref = DateTimeColumn::create(Allocator::get_default());
    DateTimeColumn c(Allocator::get_default(), ref);

    for (unsigned int i = 0; i < 10000; i++) {
        c.add(NewDate(i, i));
    }

    CHECK(c.compare(c));

    {
        ref_type ref = DateTimeColumn::create(Allocator::get_default());
        DateTimeColumn c2(Allocator::get_default(), ref);
        CHECK_NOT(c.compare(c2));
    }
}

TEST(DateTimeColumn_Index)
{
    ref_type ref = DateTimeColumn::create(Allocator::get_default());
    DateTimeColumn c(Allocator::get_default(), ref);
    StringIndex* index = c.create_search_index();
    CHECK(index);

    for (uint32_t i = 0; i < 100; ++i) {
        c.add(NewDate{i + 10000, i});
    }

    NewDate last_value{10099, 99};

    CHECK_EQUAL(index->find_first(last_value), 99);

    c.destroy_search_index();
    c.destroy();
}

#endif // TEST_COLUMN_DATETIME
