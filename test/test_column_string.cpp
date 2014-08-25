#include "testsettings.hpp"
#ifdef TEST_COLUMN_STRING

#include <tightdb/column_string.hpp>
#include <tightdb/column_string_enum.hpp>
#include <tightdb/index_string.hpp>

#include "test.hpp"

using namespace tightdb;


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


TEST(ColumnString_Basic)
{
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn c(Allocator::get_default(), ref);

    // TEST(ColumnString_MultiEmpty)

    c.add("");
    c.add("");
    c.add("");
    c.add("");
    c.add("");
    c.add("");
    CHECK_EQUAL(6, c.size());

    CHECK_EQUAL("", c.get(0));
    CHECK_EQUAL("", c.get(1));
    CHECK_EQUAL("", c.get(2));
    CHECK_EQUAL("", c.get(3));
    CHECK_EQUAL("", c.get(4));
    CHECK_EQUAL("", c.get(5));


    // TEST(ColumnString_SetExpand4)

    c.set(0, "hey");

    CHECK_EQUAL(6, c.size());
    CHECK_EQUAL("hey", c.get(0));
    CHECK_EQUAL("", c.get(1));
    CHECK_EQUAL("", c.get(2));
    CHECK_EQUAL("", c.get(3));
    CHECK_EQUAL("", c.get(4));
    CHECK_EQUAL("", c.get(5));


    // TEST(ColumnString_SetExpand8)

    c.set(1, "test");

    CHECK_EQUAL(6, c.size());
    CHECK_EQUAL("hey", c.get(0));
    CHECK_EQUAL("test", c.get(1));
    CHECK_EQUAL("", c.get(2));
    CHECK_EQUAL("", c.get(3));
    CHECK_EQUAL("", c.get(4));
    CHECK_EQUAL("", c.get(5));


    // TEST(ColumnString_Add0)

    c.clear();
    c.add();
    CHECK_EQUAL("", c.get(0));
    CHECK_EQUAL(1, c.size());


    // TEST(ColumnString_Add1)

    c.add("a");
    CHECK_EQUAL("",  c.get(0));
    CHECK_EQUAL("a", c.get(1));
    CHECK_EQUAL(2, c.size());


    // TEST(ColumnString_Add2)

    c.add("bb");
    CHECK_EQUAL("",   c.get(0));
    CHECK_EQUAL("a",  c.get(1));
    CHECK_EQUAL("bb", c.get(2));
    CHECK_EQUAL(3, c.size());


    // TEST(ColumnString_Add3)

    c.add("ccc");
    CHECK_EQUAL("",    c.get(0));
    CHECK_EQUAL("a",   c.get(1));
    CHECK_EQUAL("bb",  c.get(2));
    CHECK_EQUAL("ccc", c.get(3));
    CHECK_EQUAL(4, c.size());


    // TEST(ColumnString_Add4)

    c.add("dddd");
    CHECK_EQUAL("",     c.get(0));
    CHECK_EQUAL("a",    c.get(1));
    CHECK_EQUAL("bb",   c.get(2));
    CHECK_EQUAL("ccc",  c.get(3));
    CHECK_EQUAL("dddd", c.get(4));
    CHECK_EQUAL(5, c.size());


    // TEST(ColumnString_Add8)

    c.add("eeeeeeee");
    CHECK_EQUAL("",     c.get(0));
    CHECK_EQUAL("a",    c.get(1));
    CHECK_EQUAL("bb",   c.get(2));
    CHECK_EQUAL("ccc",  c.get(3));
    CHECK_EQUAL("dddd", c.get(4));
    CHECK_EQUAL("eeeeeeee", c.get(5));
    CHECK_EQUAL(6, c.size());


    // TEST(ColumnString_Add16)

    c.add("ffffffffffffffff");
    CHECK_EQUAL("",     c.get(0));
    CHECK_EQUAL("a",    c.get(1));
    CHECK_EQUAL("bb",   c.get(2));
    CHECK_EQUAL("ccc",  c.get(3));
    CHECK_EQUAL("dddd", c.get(4));
    CHECK_EQUAL("eeeeeeee", c.get(5));
    CHECK_EQUAL("ffffffffffffffff", c.get(6));
    CHECK_EQUAL(7, c.size());


    // TEST(ColumnString_Add32)

    c.add("gggggggggggggggggggggggggggggggg");

    CHECK_EQUAL("",     c.get(0));
    CHECK_EQUAL("a",    c.get(1));
    CHECK_EQUAL("bb",   c.get(2));
    CHECK_EQUAL("ccc",  c.get(3));
    CHECK_EQUAL("dddd", c.get(4));
    CHECK_EQUAL("eeeeeeee", c.get(5));
    CHECK_EQUAL("ffffffffffffffff", c.get(6));
    CHECK_EQUAL("gggggggggggggggggggggggggggggggg", c.get(7));
    CHECK_EQUAL(8, c.size());


    // TEST(ColumnString_Add64)

    // Add a string longer than 64 bytes to trigger long strings
    c.add("xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx");

    CHECK_EQUAL("",     c.get(0));
    CHECK_EQUAL("a",    c.get(1));
    CHECK_EQUAL("bb",   c.get(2));
    CHECK_EQUAL("ccc",  c.get(3));
    CHECK_EQUAL("dddd", c.get(4));
    CHECK_EQUAL("eeeeeeee", c.get(5));
    CHECK_EQUAL("ffffffffffffffff", c.get(6));
    CHECK_EQUAL("gggggggggggggggggggggggggggggggg", c.get(7));
    CHECK_EQUAL("xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx", c.get(8));
    CHECK_EQUAL(9, c.size());


    // TEST(ColumnString_Set1)

    c.set(0, "ccc");
    c.set(1, "bb");
    c.set(2, "a");
    c.set(3, "");

    CHECK_EQUAL(9, c.size());

    CHECK_EQUAL("ccc",  c.get(0));
    CHECK_EQUAL("bb",   c.get(1));
    CHECK_EQUAL("a",    c.get(2));
    CHECK_EQUAL("",     c.get(3));
    CHECK_EQUAL("dddd", c.get(4));
    CHECK_EQUAL("eeeeeeee", c.get(5));
    CHECK_EQUAL("ffffffffffffffff", c.get(6));
    CHECK_EQUAL("gggggggggggggggggggggggggggggggg", c.get(7));
    CHECK_EQUAL("xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx", c.get(8));


    // TEST(ColumnString_Insert1)

    // Insert in middle
    c.insert(4, "xx");

    CHECK_EQUAL(10, c.size());

    CHECK_EQUAL("ccc",  c.get(0));
    CHECK_EQUAL("bb",   c.get(1));
    CHECK_EQUAL("a",    c.get(2));
    CHECK_EQUAL("",     c.get(3));
    CHECK_EQUAL("xx",   c.get(4));
    CHECK_EQUAL("dddd", c.get(5));
    CHECK_EQUAL("eeeeeeee", c.get(6));
    CHECK_EQUAL("ffffffffffffffff", c.get(7));
    CHECK_EQUAL("gggggggggggggggggggggggggggggggg", c.get(8));
    CHECK_EQUAL("xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx", c.get(9));


    // TEST(ColumnString_Delete1)

    // Delete from end
    c.erase(9, 9 == c.size()-1);

    CHECK_EQUAL(9, c.size());

    CHECK_EQUAL("ccc",  c.get(0));
    CHECK_EQUAL("bb",   c.get(1));
    CHECK_EQUAL("a",    c.get(2));
    CHECK_EQUAL("",     c.get(3));
    CHECK_EQUAL("xx",   c.get(4));
    CHECK_EQUAL("dddd", c.get(5));
    CHECK_EQUAL("eeeeeeee", c.get(6));
    CHECK_EQUAL("ffffffffffffffff", c.get(7));
    CHECK_EQUAL("gggggggggggggggggggggggggggggggg", c.get(8));


    // TEST(ColumnString_Delete2)

    // Delete from top
    c.erase(0, 0 == c.size()-1);

    CHECK_EQUAL(8, c.size());

    CHECK_EQUAL("bb",   c.get(0));
    CHECK_EQUAL("a",    c.get(1));
    CHECK_EQUAL("",     c.get(2));
    CHECK_EQUAL("xx",   c.get(3));
    CHECK_EQUAL("dddd", c.get(4));
    CHECK_EQUAL("eeeeeeee", c.get(5));
    CHECK_EQUAL("ffffffffffffffff", c.get(6));
    CHECK_EQUAL("gggggggggggggggggggggggggggggggg", c.get(7));


    // TEST(ColumnString_Delete3)

    // Delete from middle
    c.erase(3, 3 == c.size()-1);

    CHECK_EQUAL(7, c.size());

    CHECK_EQUAL("bb",   c.get(0));
    CHECK_EQUAL("a",    c.get(1));
    CHECK_EQUAL("",     c.get(2));
    CHECK_EQUAL("dddd", c.get(3));
    CHECK_EQUAL("eeeeeeee", c.get(4));
    CHECK_EQUAL("ffffffffffffffff", c.get(5));
    CHECK_EQUAL("gggggggggggggggggggggggggggggggg", c.get(6));


    // TEST(ColumnString_DeleteAll)

    // Delete all items one at a time
    c.erase(0, 0 == c.size()-1);
    CHECK_EQUAL(6, c.size());
    c.erase(0, 0 == c.size()-1);
    CHECK_EQUAL(5, c.size());
    c.erase(0, 0 == c.size()-1);
    CHECK_EQUAL(4, c.size());
    c.erase(0, 0 == c.size()-1);
    CHECK_EQUAL(3, c.size());
    c.erase(0, 0 == c.size()-1);
    CHECK_EQUAL(2, c.size());
    c.erase(0, 0 == c.size()-1);
    CHECK_EQUAL(1, c.size());
    c.erase(0, 0 == c.size()-1);
    CHECK_EQUAL(0, c.size());

    CHECK(c.is_empty());


    // TEST(ColumnString_Insert2)

    // Create new list
    c.clear();
    c.add("a");
    c.add("b");
    c.add("c");
    c.add("d");

    // Insert in top with expansion
    c.insert(0, "xxxxx");

    CHECK_EQUAL("xxxxx", c.get(0));
    CHECK_EQUAL("a",     c.get(1));
    CHECK_EQUAL("b",     c.get(2));
    CHECK_EQUAL("c",     c.get(3));
    CHECK_EQUAL("d",     c.get(4));
    CHECK_EQUAL(5, c.size());


    // TEST(ColumnString_Insert3)

    // Insert in middle with expansion
    c.insert(3, "xxxxxxxxxx");

    CHECK_EQUAL("xxxxx", c.get(0));
    CHECK_EQUAL("a",     c.get(1));
    CHECK_EQUAL("b",     c.get(2));
    CHECK_EQUAL("xxxxxxxxxx", c.get(3));
    CHECK_EQUAL("c",     c.get(4));
    CHECK_EQUAL("d",     c.get(5));
    CHECK_EQUAL(6, c.size());


    // TEST(ColumnString_SetLeafToLong)

    // Test "Replace string array with long string array" when doing
    // it through LeafSet()
    c.clear();

    {
        ref_type col_ref = Column::create(Allocator::get_default());
        Column col(Allocator::get_default(), col_ref);

        c.add("foobar");
        c.add("bar abc");
        c.add("baz");

        c.set(1, "40 chars  40 chars  40 chars  40 chars  ");

        CHECK_EQUAL(c.size(), c.size());
        CHECK_EQUAL("foobar", c.get(0));
        CHECK_EQUAL("40 chars  40 chars  40 chars  40 chars  ", c.get(1));
        CHECK_EQUAL("baz", c.get(2));

        col.destroy();
    }


    // TEST(ColumnString_SetLeafToBig)

    // Test "Replace string array with long string array" when doing
    // it through LeafSet()
    c.clear();

    {
        ref_type col_ref = Column::create(Allocator::get_default());
        Column col(Allocator::get_default(), col_ref);

        c.add("foobar");
        c.add("bar abc");
        c.add("baz");

        c.set(1, "70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");

        CHECK_EQUAL(c.size(), c.size());
        CHECK_EQUAL("foobar", c.get(0));
        CHECK_EQUAL("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ",
                    c.get(1));
        CHECK_EQUAL("baz", c.get(2));

        col.destroy();
    }


    // TEST(ColumnString_FindAjacentLong)

    // Test against a bug where FindWithLen() would fail finding
    // ajacent hits
    c.clear();

    {
        ref_type col_ref = Column::create(Allocator::get_default());
        Column col(Allocator::get_default(), col_ref);

        c.add("40 chars  40 chars  40 chars  40 chars  ");
        c.add("baz");
        c.add("baz");
        c.add("foo");

        c.find_all(col, "baz");

        CHECK_EQUAL(2, col.size());

        col.destroy();
    }


    // TEST(ColumnString_FindAjacentBig)

    c.clear();

    {
        ref_type col_ref = Column::create(Allocator::get_default());
        Column col(Allocator::get_default(), col_ref);

        c.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
        c.add("baz");
        c.add("baz");
        c.add("foo");

        c.find_all(col, "baz");

        CHECK_EQUAL(2, col.size());

        col.destroy();
    }


    // TEST(ColumnString_Destroy)

    c.destroy();
}


TEST(ColumnString_Find1)
{
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn c(Allocator::get_default(), ref);

    c.add("a");
    c.add("bc");
    c.add("def");
    c.add("ghij");
    c.add("klmop");

    size_t res1 = c.find_first("");
    CHECK_EQUAL(size_t(-1), res1);

    size_t res2 = c.find_first("xlmno hiuh iuh uih i huih i biuhui");
    CHECK_EQUAL(size_t(-1), res2);

    size_t res3 = c.find_first("klmop");
    CHECK_EQUAL(4, res3);

    // Cleanup
    c.destroy();
}

TEST(ColumnString_Find2)
{
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn c(Allocator::get_default(), ref);

    c.add("a");
    c.add("bc");
    c.add("def");
    c.add("ghij");
    c.add("klmop");

    // Add a string longer than 64 bytes to expand to long strings
    c.add("xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx");

    size_t res1 = c.find_first("");
    CHECK_EQUAL(size_t(-1), res1);

    size_t res2 = c.find_first("xlmno hiuh iuh uih i huih i biuhui");
    CHECK_EQUAL(size_t(-1), res2);

    size_t res3 = c.find_first("klmop");
    CHECK_EQUAL(4, res3);

    size_t res4 = c.find_first("xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx");
    CHECK_EQUAL(5, res4);

    // Cleanup
    c.destroy();
}

TEST(ColumnString_AutoEnumerate)
{
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn c(Allocator::get_default(), ref);

    // Add duplicate values
    for (size_t i = 0; i < 5; ++i) {
        c.add("a");
        c.add("bc");
        c.add("def");
        c.add("ghij");
        c.add("klmop");
    }

    // Create StringEnum
    ref_type keys;
    ref_type values;
    bool res = c.auto_enumerate(keys, values);
    CHECK(res);
    ColumnStringEnum e(Allocator::get_default(), values, keys);

    // Verify that all entries match source
    CHECK_EQUAL(c.size(), e.size());
    for (size_t i = 0; i < c.size(); ++i) {
        StringData s1 = c.get(i);
        StringData s2 = e.get(i);
        CHECK_EQUAL(s1, s2);
    }

    // Search for a value that does not exist
    size_t res1 = e.find_first("nonexist");
    CHECK_EQUAL(size_t(-1), res1);

    // Search for an existing value
    size_t res2 = e.find_first("klmop");
    CHECK_EQUAL(4, res2);

    // Cleanup
    c.destroy();
    e.destroy();
}


#if !defined DISABLE_INDEX

TEST(ColumnString_AutoEnumerateIndex)
{
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn c(Allocator::get_default(), ref);

    // Add duplicate values
    for (size_t i = 0; i < 5; ++i) {
        c.add("a");
        c.add("bc");
        c.add("def");
        c.add("ghij");
        c.add("klmop");
    }

    // Create StringEnum
    ref_type keys;
    ref_type values;
    bool res = c.auto_enumerate(keys, values);
    CHECK(res);
    ColumnStringEnum e(Allocator::get_default(), values, keys);

    // Set index
    e.create_index();
    CHECK(e.has_index());

    // Search for a value that does not exist
    size_t res1 = e.find_first("nonexist");
    CHECK_EQUAL(not_found, res1);

    ref_type results_ref = Column::create(Allocator::get_default());
    Column results(Allocator::get_default(), results_ref);
    e.find_all(results, "nonexist");
    CHECK(results.is_empty());

    // Search for an existing value
    size_t res2 = e.find_first("klmop");
    CHECK_EQUAL(4, res2);

    e.find_all(results, "klmop");
    CHECK_EQUAL(5, results.size());
    CHECK_EQUAL(4, results.get(0));
    CHECK_EQUAL(9, results.get(1));
    CHECK_EQUAL(14, results.get(2));
    CHECK_EQUAL(19, results.get(3));
    CHECK_EQUAL(24, results.get(4));

    // Set a value
    e.set(1, "newval");
    size_t res3 = e.count("a");
    size_t res4 = e.count("bc");
    size_t res5 = e.count("newval");
    CHECK_EQUAL(5, res3);
    CHECK_EQUAL(4, res4);
    CHECK_EQUAL(1, res5);

    results.clear();
    e.find_all(results, "newval");
    CHECK_EQUAL(1, results.size());
    CHECK_EQUAL(1, results.get(0));

    // Insert a value
    e.insert(4, "newval");
    size_t res6 = e.count("newval");
    CHECK_EQUAL(2, res6);

    // Delete values
    e.erase(1, 1 == e.size()-1);
    e.erase(0, 0 == e.size()-1);
    size_t res7 = e.count("a");
    size_t res8 = e.count("newval");
    CHECK_EQUAL(4, res7);
    CHECK_EQUAL(1, res8);

    // Clear all
    e.clear();
    size_t res9 = e.count("a");
    CHECK_EQUAL(0, res9);

    // Cleanup
    c.destroy();
    e.destroy();
    results.destroy();
}

TEST(ColumnString_AutoEnumerateIndexReuse)
{
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn c(Allocator::get_default(), ref);

    // Add duplicate values
    for (size_t i = 0; i < 5; ++i) {
        c.add("a");
        c.add("bc");
        c.add("def");
        c.add("ghij");
        c.add("klmop");
    }

    // Set index
    c.create_index();
    CHECK(c.has_index());

    // Create StringEnum
    ref_type keys;
    ref_type values;
    bool res = c.auto_enumerate(keys, values);
    CHECK(res);
    ColumnStringEnum e(Allocator::get_default(), values, keys);

    // Reuse the index from original column
    StringIndex* index = c.release_index();
    e.install_index(index);
    CHECK(e.has_index());

    // Search for a value that does not exist
    size_t res1 = e.find_first("nonexist");
    CHECK_EQUAL(not_found, res1);

    // Search for an existing value
    size_t res2 = e.find_first("klmop");
    CHECK_EQUAL(4, res2);

    // Cleanup
    c.destroy();
    e.destroy();
}

#endif // !defined DISABLE_INDEX


TEST(ColumnString_FindAllExpand)
{
    ref_type asc_ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn asc(Allocator::get_default(), asc_ref);

    ref_type col_ref = Column::create(Allocator::get_default());
    Column c(Allocator::get_default(), col_ref);

    asc.add("HEJ");
    asc.add("sdfsd");
    asc.add("HEJ");
    asc.add("sdfsd");
    asc.add("HEJ");

    asc.find_all(c, "HEJ");

    CHECK_EQUAL(5, asc.size());
    CHECK_EQUAL(3, c.size());
    CHECK_EQUAL(0, c.get(0));
    CHECK_EQUAL(2, c.get(1));
    CHECK_EQUAL(4, c.get(2));

    // Expand to ArrayStringLong
    asc.add("dfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfs");
    asc.add("HEJ");
    asc.add("dfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfs");
    asc.add("HEJ");
    asc.add("dfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfsdfsdfsdkfjds gfgdfg djf gjkfdghkfds");

    // Todo, should the API behaviour really require us to clear c manually?
    c.clear();
    asc.find_all(c, "HEJ");

    CHECK_EQUAL(10, asc.size());
    CHECK_EQUAL(5, c.size());
    CHECK_EQUAL(0, c.get(0));
    CHECK_EQUAL(2, c.get(1));
    CHECK_EQUAL(4, c.get(2));
    CHECK_EQUAL(6, c.get(3));
    CHECK_EQUAL(8, c.get(4));

    asc.destroy();
    c.destroy();

}

// FindAll using ranges, when expanded ArrayStringLong
TEST(ColumnString_FindAllRangesLong)
{
    ref_type asc_ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn asc(Allocator::get_default(), asc_ref);

    ref_type col_ref = Column::create(Allocator::get_default());
    Column c(Allocator::get_default(), col_ref);

    // 17 elements, to test node splits with TIGHTDB_MAX_BPNODE_SIZE = 3 or other small number
    asc.add("HEJSA"); // 0
    asc.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    asc.add("HEJSA");
    asc.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    asc.add("HEJSA");
    asc.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    asc.add("HEJSA");
    asc.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    asc.add("HEJSA");
    asc.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    asc.add("HEJSA");
    asc.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    asc.add("HEJSA");
    asc.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    asc.add("HEJSA");
    asc.add("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    asc.add("HEJSA"); // 16

    c.clear();
    asc.find_all(c, "HEJSA", 0, 17);
    CHECK_EQUAL(9, c.size());
    CHECK_EQUAL(0, c.get(0));
    CHECK_EQUAL(2, c.get(1));
    CHECK_EQUAL(4, c.get(2));
    CHECK_EQUAL(6, c.get(3));
    CHECK_EQUAL(8, c.get(4));
    CHECK_EQUAL(10, c.get(5));
    CHECK_EQUAL(12, c.get(6));
    CHECK_EQUAL(14, c.get(7));
    CHECK_EQUAL(16, c.get(8));

    c.clear();
    asc.find_all(c, "HEJSA", 1, 16);
    CHECK_EQUAL(7, c.size());
    CHECK_EQUAL(2, c.get(0));
    CHECK_EQUAL(4, c.get(1));
    CHECK_EQUAL(6, c.get(2));
    CHECK_EQUAL(8, c.get(3));
    CHECK_EQUAL(10, c.get(4));
    CHECK_EQUAL(12, c.get(5));
    CHECK_EQUAL(14, c.get(6));

    // Clean-up
    asc.destroy();
    c.destroy();
}

// FindAll using ranges, when not expanded (using ArrayString)
TEST(ColumnString_FindAllRanges)
{
    ref_type asc_ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn asc(Allocator::get_default(), asc_ref);

    ref_type col_ref = Column::create(Allocator::get_default());
    Column c(Allocator::get_default(), col_ref);

    // 17 elements, to test node splits with TIGHTDB_MAX_BPNODE_SIZE = 3 or other small number
    asc.add("HEJSA"); // 0
    asc.add("1");
    asc.add("HEJSA");
    asc.add("3");
    asc.add("HEJSA");
    asc.add("5");
    asc.add("HEJSA");
    asc.add("7");
    asc.add("HEJSA");
    asc.add("9");
    asc.add("HEJSA");
    asc.add("11");
    asc.add("HEJSA");
    asc.add("13");
    asc.add("HEJSA");
    asc.add("15");
    asc.add("HEJSA"); // 16

    c.clear();
    asc.find_all(c, "HEJSA", 0, 17);
    CHECK_EQUAL(9, c.size());
    CHECK_EQUAL(0, c.get(0));
    CHECK_EQUAL(2, c.get(1));
    CHECK_EQUAL(4, c.get(2));
    CHECK_EQUAL(6, c.get(3));
    CHECK_EQUAL(8, c.get(4));
    CHECK_EQUAL(10, c.get(5));
    CHECK_EQUAL(12, c.get(6));
    CHECK_EQUAL(14, c.get(7));
    CHECK_EQUAL(16, c.get(8));

    c.clear();
    asc.find_all(c, "HEJSA", 1, 16);
    CHECK_EQUAL(7, c.size());
    CHECK_EQUAL(2, c.get(0));
    CHECK_EQUAL(4, c.get(1));
    CHECK_EQUAL(6, c.get(2));
    CHECK_EQUAL(8, c.get(3));
    CHECK_EQUAL(10, c.get(4));
    CHECK_EQUAL(12, c.get(5));
    CHECK_EQUAL(14, c.get(6));

    // Clean-up
    asc.destroy();
    c.destroy();
}

TEST(ColumnString_Count)
{
    ref_type asc_ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn asc(Allocator::get_default(), asc_ref);

    // 17 elements, to test node splits with TIGHTDB_MAX_BPNODE_SIZE = 3 or other small number
    asc.add("HEJSA"); // 0
    asc.add("1");
    asc.add("HEJSA");
    asc.add("3");
    asc.add("HEJSA");
    asc.add("5");
    asc.add("HEJSA");
    asc.add("7");
    asc.add("HEJSA");
    asc.add("9");
    asc.add("HEJSA");
    asc.add("11");
    asc.add("HEJSA");
    asc.add("13");
    asc.add("HEJSA");
    asc.add("15");
    asc.add("HEJSA"); // 16

    CHECK_EQUAL(9, asc.count("HEJSA"));

    // Create StringEnum
    size_t keys;
    size_t values;
    CHECK(asc.auto_enumerate(keys, values));
    ColumnStringEnum e(Allocator::get_default(), values, keys);

    // Check that enumerated column return same result
    CHECK_EQUAL(9, e.count("HEJSA"));

    // Clean-up
    asc.destroy();
    e.destroy();
}


#if !defined DISABLE_INDEX

TEST(ColumnString_Index)
{
    ref_type asc_ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn asc(Allocator::get_default(), asc_ref);

    // 17 elements, to test node splits with TIGHTDB_MAX_BPNODE_SIZE = 3 or other small number
    asc.add("HEJSA"); // 0
    asc.add("1");
    asc.add("HEJSA");
    asc.add("3");
    asc.add("HEJSA");
    asc.add("5");
    asc.add("HEJSA");
    asc.add("7");
    asc.add("HEJSA");
    asc.add("9");
    asc.add("HEJSA");
    asc.add("11");
    asc.add("HEJSA");
    asc.add("13");
    asc.add("HEJSA");
    asc.add("15");
    asc.add("HEJSA"); // 16

    const StringIndex& ndx = asc.create_index();
    CHECK(asc.has_index());
#ifdef TIGHTDB_DEBUG
    ndx.verify_entries(asc);
#else
    static_cast<void>(ndx);
#endif

    size_t count0 = asc.count("HEJ");
    size_t count1 = asc.count("HEJSA");
    size_t count2 = asc.count("1");
    size_t count3 = asc.count("15");
    CHECK_EQUAL(0, count0);
    CHECK_EQUAL(9, count1);
    CHECK_EQUAL(1, count2);
    CHECK_EQUAL(1, count3);

    size_t ndx0 = asc.find_first("HEJS");
    size_t ndx1 = asc.find_first("HEJSA");
    size_t ndx2 = asc.find_first("1");
    size_t ndx3 = asc.find_first("15");
    CHECK_EQUAL(not_found, ndx0);
    CHECK_EQUAL(0, ndx1);
    CHECK_EQUAL(1, ndx2);
    CHECK_EQUAL(15, ndx3);

    // Set some values
    asc.set(1, "one");
    asc.set(15, "fifteen");
    size_t set1 = asc.find_first("1");
    size_t set2 = asc.find_first("15");
    size_t set3 = asc.find_first("one");
    size_t set4 = asc.find_first("fifteen");
    CHECK_EQUAL(not_found, set1);
    CHECK_EQUAL(not_found, set2);
    CHECK_EQUAL(1, set3);
    CHECK_EQUAL(15, set4);

    // Insert some values
    asc.insert(0, "top");
    asc.insert(8, "middle");
    asc.add("bottom");
    size_t ins1 = asc.find_first("top");
    size_t ins2 = asc.find_first("middle");
    size_t ins3 = asc.find_first("bottom");
    CHECK_EQUAL(0, ins1);
    CHECK_EQUAL(8, ins2);
    CHECK_EQUAL(19, ins3);

    // Delete some values
    asc.erase(0,  0  == asc.size()-1);  // top
    asc.erase(7,  7  == asc.size()-1);  // middle
    asc.erase(17, 17 == asc.size()-1); // bottom
    size_t del1 = asc.find_first("top");
    size_t del2 = asc.find_first("middle");
    size_t del3 = asc.find_first("bottom");
    size_t del4 = asc.find_first("HEJSA");
    size_t del5 = asc.find_first("fifteen");
    CHECK_EQUAL(not_found, del1);
    CHECK_EQUAL(not_found, del2);
    CHECK_EQUAL(not_found, del3);
    CHECK_EQUAL(0, del4);
    CHECK_EQUAL(15, del5);

    // Remove all
    asc.clear();
    size_t c1 = asc.find_first("HEJSA");
    size_t c2 = asc.find_first("fifteen");
    CHECK_EQUAL(not_found, c1);
    CHECK_EQUAL(not_found, c2);

    // Clean-up
    asc.destroy();
}

#endif // !defined DISABLE_INDEX

#endif // TEST_COLUMN_STRING
