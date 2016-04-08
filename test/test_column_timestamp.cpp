#include "testsettings.hpp"
#ifdef TEST_COLUMN_TIMESTAMP

#include <realm/column_timestamp.hpp>
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


TEST_TYPES(DateTimeColumn_Basic, std::true_type, std::false_type)
{
    const bool nullable = TEST_TYPE::value;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);
    c.add(Timestamp(123,123));
    Timestamp ts = c.get(0);
    CHECK_EQUAL(ts, Timestamp(123, 123));
}

TEST(TimestampColumn_Basic_Nulls)
{
    // Test that default value is null() for nullable column and non-null for non-nullable column
    Table t;
    t.add_column(type_Timestamp, "date", false /*not nullable*/);
    t.add_column(type_Timestamp, "date", true  /*nullable*/);

    t.add_empty_row();
    CHECK(!t.is_null(0, 0));
    CHECK(t.is_null(1, 0));

    CHECK_THROW_ANY(t.set_null(0, 0));
    t.set_null(1, 0);

    CHECK_THROW_ANY(t.set_timestamp(0, 0, Timestamp(null())));
}

TEST(TimestampColumn_Relocate)
{
    // Fill so much data in a column that it relocates, to check if relocation propagates up correctly
    Table t;
    t.add_column(type_Timestamp, "date", true  /*nullable*/);

    for (unsigned int i = 0; i < 10000; i++) {
        t.add_empty_row();
        t.set_timestamp(0, i, Timestamp(i, i));
    }
}

TEST_TYPES(TimestampColumn_Compare, std::true_type, std::false_type)
{
    const bool nullable = TEST_TYPE::value;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);

    for (unsigned int i = 0; i < 10000; i++) {
        c.add(Timestamp(i, i));
    }

    CHECK(c.compare(c));

    {
        ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
        TimestampColumn c2(Allocator::get_default(), ref, nullable);
        CHECK_NOT(c.compare(c2));
    }
}

TEST_TYPES(TimestampColumn_Index, std::true_type, std::false_type)
{
    const bool nullable = TEST_TYPE::value;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);
    StringIndex* index = c.create_search_index();
    CHECK(index);

    for (uint32_t i = 0; i < 100; ++i) {
        c.add(Timestamp{i + 10000, i});
    }

    Timestamp last_value{10099, 99};

    CHECK_EQUAL(index->find_first(last_value), 99);

    c.destroy_search_index();
    c.destroy();
}

TEST_TYPES(TimestampColumn_Is_Nullable, std::true_type, std::false_type)
{
    const bool nullable = TEST_TYPE::value;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);
    CHECK_EQUAL(c.is_nullable(), nullable);
    c.destroy();
}

TEST(TimestampColumn_Set_Null_With_Index)
{
    const bool nullable = true;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);
    c.add(Timestamp{1, 1});
    CHECK(!c.is_null(0));

    StringIndex* index = c.create_search_index();
    CHECK(index);

    c.set_null(0);
    CHECK(c.is_null(0));

    c.destroy_search_index();
    c.destroy();
}

TEST_TYPES(TimestampColumn_Insert_Rows_With_Index, std::true_type, std::false_type)
{
    const bool nullable = TEST_TYPE::value;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);

    StringIndex* index = c.create_search_index();
    CHECK(index);

    c.insert_rows(0, 1, 0, nullable);
    c.set(0, Timestamp{1, 1});
    c.insert_rows(1, 1, 1, nullable);

    c.destroy_search_index();
    c.destroy();
}

TEST(TimestampColumn_Move_Last_Over)
{
    const bool nullable = true;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);
    StringIndex* index = c.create_search_index();
    CHECK(index);

    c.add(Timestamp{1, 1});
    c.add(Timestamp{2, 2});
    c.add(Timestamp{3, 3});
    c.set_null(2);
    c.move_last_row_over(0, 2, false);
    CHECK(c.is_null(0));

    c.destroy_search_index();
    c.destroy();
}

TEST_TYPES(TimestampColumn_Clear, std::true_type, std::false_type)
{
    const bool nullable = TEST_TYPE::value;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);
    StringIndex* index = c.create_search_index();
    CHECK(index);

    c.add(Timestamp{1, 1});
    c.add(Timestamp{2, 2});
    c.clear(2, false);
    c.add(Timestamp{3, 3});

    Timestamp last_value{3, 3};
    CHECK_EQUAL(c.get(0), last_value);

    c.destroy_search_index();
    c.destroy();
}

TEST_TYPES(TimestampColumn_SwapRows, std::true_type, std::false_type)
{
    const bool nullable = TEST_TYPE::value;
    ref_type ref = TimestampColumn::create(Allocator::get_default(), 0, nullable);
    TimestampColumn c(Allocator::get_default(), ref, nullable);
    StringIndex* index = c.create_search_index();
    CHECK(index);

    Timestamp one {1, 1};
    Timestamp three {3, 3};
    c.add(one);
    c.add(Timestamp{2, 2});
    c.add(three);


    CHECK_EQUAL(c.get(0), one);
    CHECK_EQUAL(c.get(2), three);
    c.swap_rows(0, 2);
    CHECK_EQUAL(c.get(2), one);
    CHECK_EQUAL(c.get(0), three);

    c.destroy_search_index();
    c.destroy();

}

#endif // TEST_COLUMN_TIMESTAMP
