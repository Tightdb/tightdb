/*************************************************************************
 *
 * Copyright 2024 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#include "testsettings.hpp"

#include <cstring>
#include <string>
#include <sstream>

#include <realm.hpp>
#include <realm/string_data.hpp>
#include <realm/unicode.hpp>
#include <realm/string_interner.hpp>

#include "test.hpp"

using namespace realm;


TEST(StringInterner_Basic_Creation)
{
    Array parent(Allocator::get_default());
    parent.create(NodeHeader::type_HasRefs, false, 1, 0);
    StringInterner interner(Allocator::get_default(), parent, ColKey(0), true);
    StringData my_string = "aaaaaaaaaaaaaaa";

    auto id = interner.intern(my_string);

    const auto stored_id = interner.lookup(my_string);
    CHECK(stored_id);
    CHECK(*stored_id == id);

    CHECK(interner.compare(my_string, *stored_id) == 0); // should be equal
    const auto origin_string = interner.get(id);
    CHECK_EQUAL(my_string, origin_string);

    CHECK(interner.compare(*stored_id, id) == 0); // compare agaist self.
    parent.destroy_deep();
}

TEST(StringInterner_InternMultipleStrings)
{
    Array parent(Allocator::get_default());
    parent.create(NodeHeader::type_HasRefs, false, 1, 0);
    StringInterner interner(Allocator::get_default(), parent, ColKey(0), true);

    std::vector<std::string> strings;
    for (size_t i = 0; i < 100; i++)
        strings.push_back("aaaaaaaaaaaaa" + std::to_string(i));

    size_t i = 0;
    for (const auto& s : strings) {
        const auto id = interner.intern(s);
        const auto& str = interner.get(id);
        CHECK(str == strings[i++]);
        auto stored_id = interner.lookup(str);
        CHECK_EQUAL(*stored_id, id);
        CHECK_EQUAL(interner.compare(str, id), 0);
    }
    parent.destroy_deep();
}

TEST(StringInterner_TestLookup)
{
    Array parent(Allocator::get_default());
    parent.create(NodeHeader::type_HasRefs, false, 1, 0);
    StringInterner interner(Allocator::get_default(), parent, ColKey(0), true);

    std::vector<std::string> strings;
    for (size_t i = 0; i < 500; ++i) {
        std::string my_string = "aaaaaaaaaaaaaaa" + std::to_string(i);
        strings.push_back(my_string);
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(strings.begin(), strings.end(), g);

    for (const auto& s : strings) {
        interner.intern(s);
        auto id = interner.lookup(StringData(s));
        CHECK(id);
        CHECK(interner.compare(StringData(s), *id) == 0);
    }

    parent.destroy_deep();
}

TEST(StringInterner_VerifyInterningNull)
{
    Array parent(Allocator::get_default());
    parent.create(NodeHeader::type_HasRefs, false, 1, 0);
    StringInterner interner(Allocator::get_default(), parent, ColKey(0), true);
    auto null_id = interner.intern({});
    CHECK_EQUAL(null_id, 0);
    CHECK_EQUAL(interner.get(null_id), StringData{});
    const auto stored_id = interner.lookup({});
    CHECK_EQUAL(stored_id, 0);
    // comparison StringID vs StringID
    CHECK_EQUAL(interner.compare({}, 0), 0);
    // interned string id vs null id
    auto str_id = interner.intern(StringData("test"));
    CHECK_EQUAL(interner.compare(str_id, null_id), 1);
    // null id vs interned string id
    CHECK_EQUAL(interner.compare(null_id, str_id), -1);

    // comparison String vs StringID
    CHECK_EQUAL(interner.compare(StringData{}, null_id), 0);
    CHECK_EQUAL(interner.compare(StringData{}, str_id), 1);
    CHECK_EQUAL(interner.compare(StringData{"test"}, null_id), -1);

    parent.destroy_deep();
}

TEST(StringInterner_VerifyLongString)
{
    Array parent(Allocator::get_default());
    parent.create(NodeHeader::type_HasRefs, false, 1, 0);
    StringInterner interner(Allocator::get_default(), parent, ColKey(0), true);

    const auto N = 7000000; // a lot of characters for triggering long string handling.
    std::string long_string(N, 'a');

    const auto id = interner.intern(StringData(long_string));
    CHECK_EQUAL(id, 1);
    const auto stored_id = interner.lookup(StringData(long_string));
    CHECK_EQUAL(stored_id, 1);
    CHECK(interner.compare(StringData(long_string), *stored_id) == 0);

    parent.destroy_deep();
}

TEST(StringInterner_VerifyExpansionFromSmallStringToLongString)
{
    Array parent(Allocator::get_default());
    parent.create(NodeHeader::type_HasRefs, false, 1, 0);
    StringInterner interner(Allocator::get_default(), parent, ColKey(0), true);

    const auto M = 1000;
    std::string small_string = "";
    for (size_t i = 0; i < M; ++i)
        small_string += 'a';

    auto id = interner.intern(StringData(small_string));
    CHECK_EQUAL(id, 1);
    auto stored_id = interner.lookup(StringData(small_string));
    CHECK_EQUAL(stored_id, 1);
    CHECK(interner.compare(StringData(small_string), *stored_id) == 0);

    const auto N = 7000000; // a lot of characters for triggering long string handling.
    std::string long_string(N, 'b');
    id = interner.intern(StringData(long_string));
    CHECK_EQUAL(id, 2);
    stored_id = interner.lookup(StringData(long_string));
    CHECK_EQUAL(stored_id, id);
    CHECK(interner.compare(StringData(long_string), *stored_id) == 0);

    parent.destroy_deep();
}