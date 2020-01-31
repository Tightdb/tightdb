/*************************************************************************
 *
 * Copyright 2019 Realm Inc.
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

#include <realm.hpp>
#include <realm/array_decimal128.hpp>

#include "test.hpp"


using namespace realm;

TEST(Decimal_Basics)
{
    auto test_str = [&](const std::string& str, const std::string& ref) {
        Decimal128 d = Decimal128(str);
        CHECK_EQUAL(d.to_string(), ref);
        auto x = d.to_bid64();
        Decimal128 d1(x);
        CHECK_EQUAL(d, d1);
    };
    test_str("0", "0");
    test_str("0.000", "0E-3");
    test_str("0E-3", "0E-3");
    test_str("3.1416", "3.1416");
    test_str("3.1416e-4", "3.1416E-4");
    test_str("-3.1416e-4", "-3.1416E-4");
    test_str("10e2", "10E2");
    test_str("10e+2", "10E2");
    test_str("1e-00021", "1E-21");
    test_str("10.100e2", "1010.0");
    test_str(".00000001", "1E-8");
    test_str(".00000001000000000", "1.000000000E-8");
    test_str("+Infinity", "Inf");
    test_str("-INF", "-Inf");

    Decimal128 pi = Decimal128("3.141592653589793238"); // 19 significant digits
    CHECK_EQUAL(pi.to_string(), "3.141592653589793238");

    Decimal128 d = Decimal128("-10.5");
    Decimal128 d1 = Decimal128("20.25");
    CHECK(d < d1);
    Decimal128 d2 = Decimal128("100");
    CHECK(d1 < d2);
    Decimal128 d3 = Decimal128("-1000.5");
    CHECK(d3 < d1);
    CHECK(d3 < d2);
    CHECK(d1 > d3);
    CHECK(d2 > d3);
    CHECK(d3 + d3 < d3);

    Decimal128 y;
    CHECK(!y.is_null());
    y = d1;

    Decimal128 d10(10);
    CHECK(d10 < d2);
    CHECK(d10 >= d);
}

TEST(Decimal_Aritmethics)
{
    Decimal128 d(10);
    auto q = d / 4;
    CHECK_EQUAL(q.to_string(), "2.5");
    q = d + Decimal128(20);
    CHECK_EQUAL(q.to_string(), "30");
    q = d + Decimal128(-20);
    CHECK_EQUAL(q.to_string(), "-10");
    q = d / -4;
    CHECK_EQUAL(q.to_string(), "-2.5");
    q = d / size_t(4);
    CHECK_EQUAL(q.to_string(), "2.5");
}

TEST(Decimal_Array)
{
    const char str0[] = "12345.67";
    const char str1[] = "1000.00";
    const char str2[] = "-45";

    ArrayDecimal128 arr(Allocator::get_default());
    arr.create();

    arr.add(Decimal128(str0));
    arr.add(Decimal128(str1));
    arr.insert(1, Decimal128(str2));

    Decimal128 id2(str2);
    CHECK_EQUAL(arr.get(0), Decimal128(str0));
    CHECK_EQUAL(arr.get(1), id2);
    CHECK_EQUAL(arr.get(2), Decimal128(str1));
    CHECK_EQUAL(arr.find_first(id2), 1);

    arr.erase(1);
    CHECK_EQUAL(arr.get(1), Decimal128(str1));

    ArrayDecimal128 arr1(Allocator::get_default());
    arr1.create();
    arr.move(arr1, 1);

    CHECK_EQUAL(arr.size(), 1);
    CHECK_EQUAL(arr1.size(), 1);
    CHECK_EQUAL(arr1.get(0), Decimal128(str1));

    arr.destroy();
    arr1.destroy();
}

TEST(Decimal128_Table)
{
    const char str0[] = "12345.67";
    const char str1[] = "1000.00";

    Table t;
    auto col_price = t.add_column(type_Decimal, "id");
    auto obj0 = t.create_object().set(col_price, Decimal128(str0));
    auto obj1 = t.create_object().set(col_price, Decimal128(str1));
    CHECK_EQUAL(obj0.get<Decimal128>(col_price), Decimal128(str0));
    CHECK_EQUAL(obj1.get<Decimal128>(col_price), Decimal128(str1));
    auto key = t.find_first(col_price, Decimal128(str1));
    CHECK_EQUAL(key, obj1.get_key());
    auto d = obj1.get_any(col_price);
    CHECK_EQUAL(d.get<Decimal128>().to_string(), "1000.00");
}


TEST(Decimal128_Query)
{
    SHARED_GROUP_TEST_PATH(path);
    DBRef db = DB::create(path);

    {
        auto wt = db->start_write();
        auto table = wt->add_table("Foo");
        auto col_dec = table->add_column(type_Decimal, "price");
        for (int i = 0; i < 100; i++) {
            table->create_object().set(col_dec, Decimal128(i));
        }
        wt->commit();
    }
    {
        auto rt = db->start_read();
        auto table = rt->get_table("Foo");
        auto col = table->get_column_key("price");
        Query q = table->column<Decimal>(col) > Decimal128(0);
        CHECK_EQUAL(q.count(), 99);
        Query q1 = table->column<Decimal>(col) < Decimal128(25);
        CHECK_EQUAL(q1.count(), 25);
    }
}

TEST(Decimal128_Aggregates)
{
    SHARED_GROUP_TEST_PATH(path);
    DBRef db = DB::create(path);
    int sum = 0;
    size_t count = 0;
    {
        auto wt = db->start_write();
        auto table = wt->add_table("Foo");
        auto col_dec = table->add_column(type_Decimal, "price", true);
        for (int i = 0; i < 100; i++) {
            Obj obj = table->create_object();
            if (i % 10) {
                int val = i % 60;
                obj.set(col_dec, Decimal128(val));
                sum += val;
                count++;
            }
            else {
                CHECK(obj.get<Decimal128>(col_dec).is_null());
            }
        }
        wt->commit();
    }
    {
        auto rt = db->start_read();
        // rt->to_json(std::cout);
        auto table = rt->get_table("Foo");
        auto col = table->get_column_key("price");
        CHECK_EQUAL(table->count_decimal(col, Decimal128(51)), 1);
        CHECK_EQUAL(table->count_decimal(col, Decimal128(31)), 2);
        CHECK_EQUAL(table->sum_decimal(col), Decimal128(sum));
        CHECK_EQUAL(table->average_decimal(col), Decimal128(sum) / count);
        CHECK_EQUAL(table->maximum_decimal(col), Decimal128(59));
        CHECK_EQUAL(table->minimum_decimal(col), Decimal128(1));
    }
}
