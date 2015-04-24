#include "testsettings.hpp"
#ifdef TEST_INDEX_STRING

#include <realm.hpp>
#include <realm/index_string.hpp>
#include <set>
#include "test.hpp"
#include "util/misc.hpp"

using namespace realm;
using namespace util;
using namespace std;
using namespace realm;
using namespace realm::util;
using namespace realm::test_util;

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


namespace {

// strings used by tests
const char s1[] = "John";
const char s2[] = "Brian";
const char s3[] = "Samantha";
const char s4[] = "Tom";
const char s5[] = "Johnathan";
const char s6[] = "Johnny";
const char s7[] = "Sam";

// integers used by integer index tests
const int64_t ints[] = {
    0x1111,
    0x11112222,
    0x11113333,
    0x1111333,
    0x111122223333ull,
    0x1111222233334ull,
    0x22223333,
    0x11112227,
    0x11112227,
    0x78923
};

} // anonymous namespace


TEST(StringIndex_IsEmpty)
{
    // Create a column with string values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    // Create a new index on column
    const StringIndex& ndx = *col.create_search_index();

    CHECK(ndx.is_empty());

    // Clean up
    col.destroy();
}

TEST(StringIndex_BuildIndex)
{
    // Create a column with string values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value
    col.add(s5); // common prefix
    col.add(s6); // common prefix

    // Create a new index on column
    const StringIndex& ndx = *col.create_search_index();

    const size_t r1 = ndx.find_first(s1);
    const size_t r2 = ndx.find_first(s2);
    const size_t r3 = ndx.find_first(s3);
    const size_t r4 = ndx.find_first(s4);
    const size_t r5 = ndx.find_first(s5);
    const size_t r6 = ndx.find_first(s6);

    CHECK_EQUAL(0, r1);
    CHECK_EQUAL(1, r2);
    CHECK_EQUAL(2, r3);
    CHECK_EQUAL(3, r4);
    CHECK_EQUAL(5, r5);
    CHECK_EQUAL(6, r6);

    // Clean up
    col.destroy();
}

TEST(StringIndex_DeleteAll)
{
    // Create a column with string values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value
    col.add(s5); // common prefix
    col.add(s6); // common prefix

    // Create a new index on column
    const StringIndex& ndx = *col.create_search_index();

    // Delete all entries
    // (reverse order to avoid ref updates)
    col.erase(6, 6 == col.size()-1);
    col.erase(5, 5 == col.size()-1);
    col.erase(4, 4 == col.size()-1);
    col.erase(3, 3 == col.size()-1);
    col.erase(2, 2 == col.size()-1);
    col.erase(1, 1 == col.size()-1);
    col.erase(0, 0 == col.size()-1);
#ifdef REALM_DEBUG
    CHECK(ndx.is_empty());
#else
    static_cast<void>(ndx);
#endif

    // Re-insert values
    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value
    col.add(s5); // common prefix
    col.add(s6); // common prefix

    // Delete all entries
    // (in order to force constant ref updating)
    col.erase(0, 0 == col.size()-1);
    col.erase(0, 0 == col.size()-1);
    col.erase(0, 0 == col.size()-1);
    col.erase(0, 0 == col.size()-1);
    col.erase(0, 0 == col.size()-1);
    col.erase(0, 0 == col.size()-1);
    col.erase(0, 0 == col.size()-1);
#ifdef REALM_DEBUG
    CHECK(ndx.is_empty());
#else
    static_cast<void>(ndx);
#endif

    // Clean up
    col.destroy();
}

TEST(StringIndex_Delete)
{
    // Create a column with random values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value

    // Create a new index on column
    const StringIndex& ndx = *col.create_search_index();

    // Delete first item (in index)
    col.erase(1, 1 == col.size()-1);

    CHECK_EQUAL(0, col.find_first(s1));
    CHECK_EQUAL(1, col.find_first(s3));
    CHECK_EQUAL(2, col.find_first(s4));
    CHECK_EQUAL(not_found, ndx.find_first(s2));

    // Delete last item (in index)
    col.erase(2, 2 == col.size()-1);

    CHECK_EQUAL(0, col.find_first(s1));
    CHECK_EQUAL(1, col.find_first(s3));
    CHECK_EQUAL(not_found, col.find_first(s4));
    CHECK_EQUAL(not_found, col.find_first(s2));

    // Delete middle item (in index)
    col.erase(1, 1 == col.size()-1);

    CHECK_EQUAL(0, col.find_first(s1));
    CHECK_EQUAL(not_found, col.find_first(s3));
    CHECK_EQUAL(not_found, col.find_first(s4));
    CHECK_EQUAL(not_found, col.find_first(s2));

    // Delete all items
    col.erase(0, 0 == col.size()-1);
    col.erase(0, 0 == col.size()-1);
#ifdef REALM_DEBUG
    CHECK(ndx.is_empty());
#endif

    // Clean up
    col.destroy();
}

TEST(StringIndex_MoveLastOver)
{
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value
    col.add(s1); // duplicate value

    col.create_search_index();

    {
        size_t index_ref;
        FindRes fr = col.find_all_indexref(s1, index_ref);
        CHECK_EQUAL(fr, FindRes_column);
        if (fr != FindRes_column)
            return;

        Column matches(Column::unattached_root_tag(), col.get_alloc());
        matches.get_root_array()->init_from_ref(index_ref);

        CHECK_EQUAL(3, matches.size());
        CHECK_EQUAL(0, matches.get(0));
        CHECK_EQUAL(4, matches.get(1));
        CHECK_EQUAL(5, matches.get(2));
    }

    // Remove a non-s1 row and change the order of the s1 rows
    col.move_last_over(1);

    {
        size_t index_ref;
        FindRes fr = col.find_all_indexref(s1, index_ref);
        CHECK_EQUAL(fr, FindRes_column);
        if (fr != FindRes_column)
            return;

        Column matches(Column::unattached_root_tag(), col.get_alloc());
        matches.get_root_array()->init_from_ref(index_ref);

        CHECK_EQUAL(3, matches.size());
        CHECK_EQUAL(0, matches.get(0));
        CHECK_EQUAL(1, matches.get(1));
        CHECK_EQUAL(4, matches.get(2));
    }

    // Move a s1 row over a s1 row
    col.move_last_over(1);

    {
        size_t index_ref;
        FindRes fr = col.find_all_indexref(s1, index_ref);
        CHECK_EQUAL(fr, FindRes_column);
        if (fr != FindRes_column)
            return;

        Column matches(Column::unattached_root_tag(), col.get_alloc());
        matches.get_root_array()->init_from_ref(index_ref);

        CHECK_EQUAL(2, matches.size());
        CHECK_EQUAL(0, matches.get(0));
        CHECK_EQUAL(1, matches.get(1));
    }

    col.destroy();
}

TEST(StringIndex_ClearEmpty)
{
    // Create a column with string values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    // Create a new index on column
    const StringIndex& ndx = *col.create_search_index();

    // Clear to remove all entries
    col.clear();
#ifdef REALM_DEBUG
    CHECK(ndx.is_empty());
#else
    static_cast<void>(ndx);
#endif

    // Clean up
    col.destroy();
}

TEST(StringIndex_Clear)
{
    // Create a column with string values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value
    col.add(s5); // common prefix
    col.add(s6); // common prefix

    // Create a new index on column
    const StringIndex& ndx = *col.create_search_index();

    // Clear to remove all entries
    col.clear();
#ifdef REALM_DEBUG
    CHECK(ndx.is_empty());
#else
    static_cast<void>(ndx);
#endif

    // Re-insert values
    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value
    col.add(s5); // common prefix
    col.add(s6); // common prefix

    const size_t r1 = ndx.find_first(s1);
    const size_t r2 = ndx.find_first(s2);
    const size_t r3 = ndx.find_first(s3);
    const size_t r4 = ndx.find_first(s4);
    const size_t r5 = ndx.find_first(s5);
    const size_t r6 = ndx.find_first(s6);

    CHECK_EQUAL(0, r1);
    CHECK_EQUAL(1, r2);
    CHECK_EQUAL(2, r3);
    CHECK_EQUAL(3, r4);
    CHECK_EQUAL(5, r5);
    CHECK_EQUAL(6, r6);

    // Clean up
    col.destroy();
}

TEST(StringIndex_Insert)
{
    // Create a column with random values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value

    // Create a new index on column
    col.create_search_index();

    // Insert item in top of column
    col.insert(0, s5);

    CHECK_EQUAL(0, col.find_first(s5));
    CHECK_EQUAL(1, col.find_first(s1));
    CHECK_EQUAL(2, col.find_first(s2));
    CHECK_EQUAL(3, col.find_first(s3));
    CHECK_EQUAL(4, col.find_first(s4));
    //CHECK_EQUAL(5, ndx.find_first(s1)); // duplicate

    // Append item in end of column
    col.insert(6, s6);

    CHECK_EQUAL(0, col.find_first(s5));
    CHECK_EQUAL(1, col.find_first(s1));
    CHECK_EQUAL(2, col.find_first(s2));
    CHECK_EQUAL(3, col.find_first(s3));
    CHECK_EQUAL(4, col.find_first(s4));
    CHECK_EQUAL(6, col.find_first(s6));

    // Insert item in middle
    col.insert(3, s7);

    CHECK_EQUAL(0, col.find_first(s5));
    CHECK_EQUAL(1, col.find_first(s1));
    CHECK_EQUAL(2, col.find_first(s2));
    CHECK_EQUAL(3, col.find_first(s7));
    CHECK_EQUAL(4, col.find_first(s3));
    CHECK_EQUAL(5, col.find_first(s4));
    CHECK_EQUAL(7, col.find_first(s6));

    // Clean up
    col.destroy();
}

TEST(StringIndex_Set)
{
    // Create a column with random values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s3);
    col.add(s4);
    col.add(s1); // duplicate value

    // Create a new index on column
    col.create_search_index();

    // Set top value
    col.set(0, s5);

    CHECK_EQUAL(0, col.find_first(s5));
    CHECK_EQUAL(1, col.find_first(s2));
    CHECK_EQUAL(2, col.find_first(s3));
    CHECK_EQUAL(3, col.find_first(s4));
    CHECK_EQUAL(4, col.find_first(s1));

    // Set bottom value
    col.set(4, s6);

    CHECK_EQUAL(not_found, col.find_first(s1));
    CHECK_EQUAL(0, col.find_first(s5));
    CHECK_EQUAL(1, col.find_first(s2));
    CHECK_EQUAL(2, col.find_first(s3));
    CHECK_EQUAL(3, col.find_first(s4));
    CHECK_EQUAL(4, col.find_first(s6));

    // Set middle value
    col.set(2, s7);

    CHECK_EQUAL(not_found, col.find_first(s3));
    CHECK_EQUAL(not_found, col.find_first(s1));
    CHECK_EQUAL(0, col.find_first(s5));
    CHECK_EQUAL(1, col.find_first(s2));
    CHECK_EQUAL(2, col.find_first(s7));
    CHECK_EQUAL(3, col.find_first(s4));
    CHECK_EQUAL(4, col.find_first(s6));

    // Clean up
    col.destroy();
}

TEST(StringIndex_Count)
{
    // Create a column with duplcate values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s2);
    col.add(s3);
    col.add(s3);
    col.add(s3);
    col.add(s4);
    col.add(s4);
    col.add(s4);
    col.add(s4);

    // Create a new index on column
    col.create_search_index();

    // Counts
    const size_t c0 = col.count(s5);
    const size_t c1 = col.count(s1);
    const size_t c2 = col.count(s2);
    const size_t c3 = col.count(s3);
    const size_t c4 = col.count(s4);
    CHECK_EQUAL(0, c0);
    CHECK_EQUAL(1, c1);
    CHECK_EQUAL(2, c2);
    CHECK_EQUAL(3, c3);
    CHECK_EQUAL(4, c4);

    // Clean up
    col.destroy();
}

TEST(StringIndex_Distinct)
{
    // Create a column with duplcate values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s2);
    col.add(s3);
    col.add(s3);
    col.add(s3);
    col.add(s4);
    col.add(s4);
    col.add(s4);
    col.add(s4);

    // Create a new index on column
    StringIndex& ndx = *col.create_search_index();

    // Get view of unique values
    // (sorted in alphabetical order, each ref to first match)
    ref_type results_ref = Column::create(Allocator::get_default());
    Column results(Allocator::get_default(), results_ref);
    ndx.distinct(results);

    CHECK_EQUAL(4, results.size());
    CHECK_EQUAL(1, results.get(0)); // s2 = Brian
    CHECK_EQUAL(0, results.get(1)); // s1 = John
    CHECK_EQUAL(3, results.get(2)); // s3 = Samantha
    CHECK_EQUAL(6, results.get(3)); // s4 = Tom

    // Clean up
    results.destroy();
    col.destroy();
}

TEST(StringIndex_FindAllNoCopy)
{
    // Create a column with duplcate values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref);

    col.add(s1);
    col.add(s2);
    col.add(s2);
    col.add(s3);
    col.add(s3);
    col.add(s3);
    col.add(s4);
    col.add(s4);
    col.add(s4);
    col.add(s4);

    // Create a new index on column
    StringIndex& ndx = *col.create_search_index();

    size_t ref_2 = not_found;
    FindRes res1 = ndx.find_all(StringData("not there"), ref_2);
    CHECK_EQUAL(FindRes_not_found, res1);

    FindRes res2 = ndx.find_all(s1, ref_2);
    CHECK_EQUAL(FindRes_single, res2);
    CHECK_EQUAL(0, ref_2);

    FindRes res3 = ndx.find_all(s4, ref_2);
    CHECK_EQUAL(FindRes_column, res3);
    const Column results(Allocator::get_default(), ref_type(ref_2));
    CHECK_EQUAL(4, results.size());
    CHECK_EQUAL(6, results.get(0));
    CHECK_EQUAL(7, results.get(1));
    CHECK_EQUAL(8, results.get(2));
    CHECK_EQUAL(9, results.get(3));

    // Clean up
    col.destroy();
}


TEST(StringIndex_FindAllNoCopy2_Int)
{
    // Create a column with duplcate values
    ref_type ref = Column::create(Allocator::get_default());
    Column col(Allocator::get_default(), ref);

    for (size_t t = 0; t < sizeof(ints) / sizeof(ints[0]); t++)
        col.add(ints[t]);

    // Create a new index on column
    col.create_search_index();
    StringIndex& ndx = *col.get_search_index();
    size_t results = not_found;

    for (size_t t = 0; t < sizeof(ints) / sizeof(ints[0]); t++) {
        FindRes res = ndx.find_all(ints[t], results);

        size_t real = 0;
        for (size_t y = 0; y < sizeof(ints) / sizeof(ints[0]); y++) {
            if (ints[t] == ints[y])
                real++;
        }

        if (real == 1) {
            CHECK_EQUAL(res, FindRes_single);
            CHECK_EQUAL(ints[t], ints[results]);
        }
        else if (real > 1) {
            CHECK_EQUAL(FindRes_column, res);
            const Column results2(Allocator::get_default(), ref_type(results));
            CHECK_EQUAL(real, results2.size());
            for (size_t y = 0; y < real; y++)
                CHECK_EQUAL(ints[t], ints[results2.get(y)]);
        }
    }

    // Clean up
    col.destroy();
}


TEST(StringIndex_Count_Int)
{
    // Create a column with duplcate values
    ref_type ref = Column::create(Allocator::get_default());
    Column col(Allocator::get_default(), ref);

    for (size_t t = 0; t < sizeof(ints) / sizeof(ints[0]); t++)
        col.add(ints[t]);

    // Create a new index on column
    col.create_search_index();
    StringIndex& ndx = *col.get_search_index();

    for (size_t t = 0; t < sizeof(ints) / sizeof(ints[0]); t++) {
        size_t count = ndx.count(ints[t]);

        size_t real = 0;
        for (size_t y = 0; y < sizeof(ints) / sizeof(ints[0]); y++) {
            if (ints[t] == ints[y])
                real++;
        }
        
        CHECK_EQUAL(real, count);
    }
    // Clean up
    col.destroy();
}


TEST(StringIndex_Distinct_Int)
{
    // Create a column with duplcate values
    ref_type ref = Column::create(Allocator::get_default());
    Column col(Allocator::get_default(), ref);

    for (size_t t = 0; t < sizeof(ints) / sizeof(ints[0]); t++)
        col.add(ints[t]);

    // Create a new index on column
    col.create_search_index();

    
    StringIndex& ndx = *col.get_search_index();
    
    ref_type results_ref = Column::create(Allocator::get_default());
    Column results(Allocator::get_default(), results_ref);

    ndx.distinct(results);
    
    std::set<int64_t> s;    
    for (size_t t = 0; t < sizeof(ints) / sizeof(ints[0]); t++) {
        s.insert(ints[t]);
    }

    CHECK_EQUAL(s.size(), results.size());
    
    // Clean up
    col.destroy();
    results.destroy();
}


TEST(StringIndex_Set_Add_Erase_Insert_Int)
{
    ref_type ref = Column::create(Allocator::get_default());
    Column col(Allocator::get_default(), ref);

    col.add(1);
    col.add(2);
    col.add(3);
    col.add(2);

    // Create a new index on column
    col.create_search_index();
    StringIndex& ndx = *col.get_search_index();

    size_t f = ndx.find_first(int64_t(2));
    CHECK_EQUAL(1, f);

    col.set(1, 5);

    f = ndx.find_first(int64_t(2));
    CHECK_EQUAL(3, f);

    col.erase(1, false);

    f = ndx.find_first(int64_t(2));
    CHECK_EQUAL(2, f);

    col.insert(1, 5);

    f = ndx.find_first(int64_t(2));
    CHECK_EQUAL(3, f);

    col.add(7);
    col.set(4, 10);

    f = ndx.find_first(int64_t(10));
    CHECK_EQUAL(col.size() - 1, f);

    col.add(9);
    f = ndx.find_first(int64_t(9));
    CHECK_EQUAL(col.size() - 1, f);

    col.clear();
    f = ndx.find_first(int64_t(2));
    CHECK_EQUAL(not_found, f);

    // Clean up
    col.destroy();
}

TEST(StringIndex_FuzzyTest_Int)
{
    ref_type ref = Column::create(Allocator::get_default());
    Column col(Allocator::get_default(), ref);
    Random random(random_int<unsigned long>());
    const size_t n = static_cast<size_t>(1.2 * REALM_MAX_BPNODE_SIZE);

    col.create_search_index();

    for (size_t t = 0; t < n; ++t) {
        col.add(random.draw_int_max(0xffffffffffffffff));
    }

    for (size_t t = 0; t < n; ++t) {
        int64_t r;
        if (random.draw_bool())
            r = col.get(t);
        else
            r = random.draw_int_max(0xffffffffffffffff);

        size_t m = col.find_first(r);
        for (size_t t_2 = 0; t_2 < n; ++t_2) {
            if (col.get(t_2) == r) {
                CHECK_EQUAL(t_2, m);
                break;
            }
        }
    }
    col.destroy();
}


// Tests for a bug with strings containing zeroes
TEST(StringIndex_EmbeddedZeroes)
{
    // String index
    ref_type ref2 = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col2(Allocator::get_default(), ref2);
    const StringIndex& ndx2 = *col2.create_search_index();

#if REALM_NULL_STRINGS == 1
    // FIXME: re-enable once embedded nuls work
    col2.add(StringData("\0", 1));
    col2.add(StringData("\1", 1));
    col2.add(StringData("\0\0", 2));
    col2.add(StringData("\0\1", 2));
    col2.add(StringData("\1\0", 2));

    CHECK_EQUAL(ndx2.find_first(StringData("\0", 1)), 0);
    CHECK_EQUAL(ndx2.find_first(StringData("\1", 1)), 1);
    CHECK_EQUAL(ndx2.find_first(StringData("\2", 1)), not_found);
    CHECK_EQUAL(ndx2.find_first(StringData("\0\0", 2)), 2);
    CHECK_EQUAL(ndx2.find_first(StringData("\0\1", 2)), 3);
    CHECK_EQUAL(ndx2.find_first(StringData("\1\0", 2)), 4);
    CHECK_EQUAL(ndx2.find_first(StringData("\1\0\0", 3)), not_found);
#else
    static_cast<void>(ndx2);
    CHECK_THROW_ANY(col2.add(StringData("\0", 1)));
#endif

    // Integer index (uses String index internally)
    int64_t v = 1ULL << 41;
    ref_type ref = Column::create(Allocator::get_default());
    Column col(Allocator::get_default(), ref);
    col.create_search_index();
    col.add(1ULL << 40);
    StringIndex& ndx = *col.get_search_index();
    size_t f = ndx.find_first(v);
    CHECK_EQUAL(f, not_found);

    col.destroy();
    col2.destroy();
}

#if REALM_NULL_STRINGS == 1
TEST(StringIndex_Null)
{
    // Create a column with string values
    ref_type ref = AdaptiveStringColumn::create(Allocator::get_default());
    AdaptiveStringColumn col(Allocator::get_default(), ref, true);

    col.add("");
    col.add(realm::null());

    const StringIndex& ndx = *col.create_search_index();

    const size_t r1 = ndx.find_first(realm::null());
    CHECK_EQUAL(r1, 1);

    col.destroy();
}
#endif

#if REALM_NULL_STRINGS == 1
TEST(StringIndex_Zero_Crash)
{
    // StringIndex could crash if strings ended with one or more 0-bytes
    Table table;
    table.add_column(type_String, "");
    table.add_empty_row(3);

    table.set_string(0, 0, StringData(""));
    table.set_string(0, 1, StringData("\0", 1));
    table.set_string(0, 2, StringData("\0\0", 2));
    table.add_search_index(0);

    size_t t;

    t = table.find_first_string(0, StringData(""));
    CHECK_EQUAL(0, t);

    t = table.find_first_string(0, StringData("\0", 1));
    CHECK_EQUAL(1, t);

    t = table.find_first_string(0, StringData("\0\0", 2));
    CHECK_EQUAL(2, t);
}
#endif

#if REALM_NULL_STRINGS == 1
TEST(StringIndex_Zero_Crash2)
{
    Random random(random_int<unsigned long>());

    for (size_t iter = 0; iter < 10 + TEST_DURATION * 100 ; iter++) {
        // StringIndex could crash if strings ended with one or more 0-bytes
        Table table;
        table.add_column(type_String, "", true);

        table.add_search_index(0);

        for (size_t i = 0; i < 100 + TEST_DURATION * 1000; i++) {
            unsigned char action = static_cast<unsigned char>(random.draw_int_max<unsigned int>(100));
            if (action == 0) {
//                table.remove_search_index(0);
                table.add_search_index(0);
            }
            else if (action > 48 && table.size() < 10) {
                // Generate string with equal probability of being empty, null, short, medium and long, and with 
                // their contents having equal proability of being either random or a duplicate of a previous 
                // string. When it's random, each char must have equal probability of being 0 or non-0e
                char buf[] = "This string is around 90 bytes long, which falls in the long-string type of Realm strings";
                char* buf1 = static_cast<char*>(malloc(sizeof(buf)));
                memcpy(buf1, buf, sizeof(buf));
                char buf2[] = "                                                                                         ";
                StringData sd;

                size_t len = random.draw_int_max<size_t>(3);
                if (len == 0)
                    len = 0;
                else if (len == 1)
                    len = 7;
                else if (len == 2)
                    len = 27;
                else
                    len = random.draw_int_max<size_t>(90);

                if (random.draw_int_max<int>(1) == 0) {
                    // duplicate string
                    sd = StringData(buf1, len);
                }
                else {
                    // random string
                    for (size_t t = 0; t < len; t++) {
                        if (random.draw_int_max<int>(100) > 20)
                            buf2[t] = 0;                        // zero byte
                        else
                            buf2[t] = static_cast<char>(random.draw_int<int>());  // random byte
                    }
                    // no generated string can equal "null" (our vector magic value for null) because 
                    // len == 4 is not possible
                    sd = StringData(buf2, len);
                }

                size_t pos = random.draw_int_max<size_t>(table.size());
                table.insert_empty_row(pos);
                table.set_string(0, pos, sd);
                free(buf1);
            }
            else if (table.size() > 0) {
                // delete
                size_t row = random.draw_int_max<size_t>(table.size() - 1);
                table.remove(row);
            }

            action = static_cast<unsigned char>(random.draw_int_max<unsigned int>(100));
            if (table.size() > 0) {
                // Search for value that exists
                size_t row = random.draw_int_max<size_t>(table.size() - 1);
                StringData sd = table.get_string(0, row);
                size_t t = table.find_first_string(0, sd);
                StringData sd2 = table.get_string(0, t);
                CHECK_EQUAL(sd, sd2);
            }

        }
    }
}
#endif

TEST(StringIndex_Integer_Increasing)
{
    const size_t rows = 2000 + 1000000 * TEST_DURATION;

    // StringIndex could crash if strings ended with one or more 0-bytes
    Table table;
    table.add_column(type_Int, "int");
    table.add_search_index(0);

    vector<int64_t> reference;

    for (size_t row = 0; row < rows; row++) {
        int64_t r = fastrand(0x100000);
        table.add_empty_row();
        table.set_int(0, row, r);
        reference.push_back(r);
    }

    std::sort(reference.begin(), reference.end());

    for (size_t row = 0; row < rows; row++) {
        int64_t v = table.get_int(0, row);
        size_t c = table.count_int(0, v);
        
        size_t start = std::lower_bound(reference.begin(), reference.end(), v) - reference.begin();
        size_t ref_count = 0;
        for (size_t t = start; t < reference.size(); t++) {
            if (reference[t] == v)
                ref_count++;
        }

        CHECK_EQUAL(c, ref_count);

    }


}

#endif // TEST_INDEX_STRING
