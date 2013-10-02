#include "testsettings.hpp"
#ifdef TEST_QUERY

#include <cstdlib> // itoa()
#include <vector>

#include <UnitTest++.h>

#include <tightdb.hpp>
#include <tightdb/lang_bind_helper.hpp>

#include "testsettings.hpp"

#include <tightdb/column.hpp>
#include <tightdb/query_engine.hpp>

using namespace tightdb;
using namespace std;


namespace {

TIGHTDB_TABLE_2(TwoIntTable,
                first,  Int,
                second, Int)

TIGHTDB_TABLE_1(SingleStringTable,
                first, String)

TIGHTDB_TABLE_3(TripleTable,
                first, String,
                second, String,
                third, Int)

TIGHTDB_TABLE_1(OneIntTable,
                first,  Int)

TIGHTDB_TABLE_2(TupleTableType,
                first,  Int,
                second, String)

TIGHTDB_TABLE_2(TupleTableTypeBin,
                first,  Int,
                second, Binary)

TIGHTDB_TABLE_2(BoolTupleTable,
                first,  Int,
                second, Bool)

TIGHTDB_TABLE_5(PeopleTable,
                name,  String,
                age,   Int,
                male,  Bool,
                hired, DateTime,
                photo, Binary)

TIGHTDB_TABLE_2(FloatTable,
                col_float,  Float,
                col_double, Double)

TIGHTDB_TABLE_3(FloatTable3,
                col_float,  Float,
                col_double, Double,
                col_int, Int)

TIGHTDB_TABLE_3(PHPMinimumCrash,
                firstname,  String,
                lastname, String,
                salary, Int)

TIGHTDB_TABLE_3(TableViewSum,
                col_float,  Float,
                col_double, Double,
                col_int, Int)

TIGHTDB_TABLE_5(GATable,
                     user_id, String,
                     country, String,
                     build,   String,
                     event_1, Int,
                     event_2, Int)

TIGHTDB_TABLE_2(PeopleTable2,
                name, String,
                age, Int)

TIGHTDB_TABLE_5(ThreeColTable,
    first,  Int,
    second, Float,
    third, Double,
    fourth, Bool,
    fifth, String)


} // anonymous namespace


TEST(NextGenSyntax) 
{
    volatile size_t match;

    // Setup untyped table
    Table untyped;
    untyped.add_column(type_Int, "firs1");
    untyped.add_column(type_Float, "second");
    untyped.add_column(type_Double, "third");
    untyped.add_column(type_Bool, "third2");
    untyped.add_column(type_String, "fourth");
    untyped.add_empty_row(2);
    untyped.set_int(0, 0, 20);
    untyped.set_float(1, 0, 19.9f);
    untyped.set_double(2, 0, 3.0);
    untyped.set_bool(3, 0, true);
    untyped.set_string(4, 0, "hello");

    untyped.set_int(0, 1, 20);
    untyped.set_float(1, 1, 20.1f);
    untyped.set_double(2, 1, 4.0);
    untyped.set_bool(3, 1, false);
    untyped.set_string(4, 1, "world");

    // Setup typed table, same contents as untyped
    ThreeColTable typed;
    typed.add(20, 19.9f, 3.0, true, "hello");
    typed.add(20, 20.1f, 4.0, false, "world");

    
    match = (untyped.column<String>(4) == "world").find_next();
    CHECK(match == 1);
    
    match = ("world" == untyped.column<String>(4)).find_next();
    CHECK(match == 1);
    
    match = ("hello" != untyped.column<String>(4)).find_next();
    CHECK(match == 1);

    match = (untyped.column<String>(4) != StringData("hello")).find_next();
    CHECK(match == 1);

    // This is a demonstration of fallback to old query_engine for the specific cases where it's possible
    // because old engine is faster. This will return a ->less(...) query
    match = (untyped.column<int64_t>(0) == untyped.column<int64_t>(0)).find_next();
    CHECK(match == 0);


    match = (untyped.column<bool>(3) == false).find_next();
    CHECK(match == 1);

    match = (20.3 > untyped.column<double>(2) + 2).find_next();
    CHECK(match == 0);


    match = (untyped.column<int64_t>(0) > untyped.column<int64_t>(0)).find_next();
    CHECK(match == not_found);


    // Small typed table test:
    match = (typed.column().second + 100 > 120 && typed.column().first > 2).find_next();
    CHECK(match == 1);

    // Untyped &&

    // Left condition makes first row non-match
    match = (untyped.column<float>(1) + 1 > 21 && untyped.column<double>(2) > 2).find_next();
    CHECK(match == 1);

    // Right condition makes first row a non-match
    match = (untyped.column<float>(1) > 10 && untyped.column<double>(2) > 3.5).find_next();
    CHECK(match == 1);

    // Both make first row match
    match = (untyped.column<float>(1) < 20 && untyped.column<double>(2) > 2).find_next();
    CHECK(match == 0);

    // Both make first row non-match
    match = (untyped.column<float>(1) > 20 && untyped.column<double>(2) > 3.5).find_next();
    CHECK(match == 1);

    // Left cond match 0, right match 1
    match = (untyped.column<float>(1) < 20 && untyped.column<double>(2) > 3.5).find_next();
    CHECK(match == not_found);

    // Left match 1, right match 0
    match = (untyped.column<float>(1) > 20 && untyped.column<double>(2) < 3.5).find_next();
    CHECK(match == not_found);

    // Untyped ||

    // Left match 0
    match = (untyped.column<float>(1) < 20 || untyped.column<double>(2) < 3.5).find_next();
    CHECK(match == 0);

    // Right match 0
    match = (untyped.column<float>(1) > 20 || untyped.column<double>(2) < 3.5).find_next();
    CHECK(match == 0);

    // Left match 1

    match = (untyped.column<float>(1) > 20 || untyped.column<double>(2) > 9.5).find_next();
    
    CHECK(match == 1);

    Query q4 = untyped.column<float>(1) + untyped.column<int64_t>(0) > 40;



    Query q5 = 20 < untyped.column<float>(1);

    match = q4.and_query(q5).find_next();
    CHECK(match == 1);


    // Untyped, direct column addressing
    Value<int64_t> uv1(1);

    Columns<float> uc1 = untyped.column<float>(1);

    Query q2 = uv1 <= uc1;
    match = q2.find_next();
    CHECK(match == 0);


    Query q0 = uv1 <= uc1;
    match = q0.find_next();
    CHECK(match == 0);

    Query q99 = uv1 <= untyped.column<float>(1);
    match = q99.find_next();
    CHECK(match == 0);


    Query q8 = 1 > untyped.column<float>(1) + 5;
    match = q8.find_next();
    CHECK(match == not_found);

    Query q3 = untyped.column<float>(1) + untyped.column<int64_t>(0) > 10 + untyped.column<int64_t>(0);
    match = q3.find_next();

    match = q2.find_next();
    CHECK(match == 0);    


    // Typed, direct column addressing
    Query q1 = typed.column().second + typed.column().first > 40;
    match = q1.find_next();
    CHECK(match == 1);   


    match = (typed.column().first + typed.column().second > 40).find_next();
    CHECK(match == 1);   


    Query tq1 = typed.column().first + typed.column().second >= typed.column().first + typed.column().second;
    match = tq1.find_next();
    CHECK(match == 0);   


    // Typed, column objects
    Columns<int64_t> t0 = typed.column().first;
    Columns<float> t1 = typed.column().second;

    match = (t0 + t1 > 40).find_next();
    CHECK(match == 1);

    match = q1.find_next();
    CHECK(match == 1);   

    match = (untyped.column<int64_t>(0) + untyped.column<float>(1) > 40).find_next();
    CHECK(match == 1);    

    match = (untyped.column<int64_t>(0) + untyped.column<float>(1) < 40).find_next();
    CHECK(match == 0);    

    match = (untyped.column<float>(1) <= untyped.column<int64_t>(0)).find_next();
    CHECK(match == 0);    

    match = (untyped.column<int64_t>(0) + untyped.column<float>(1) >= untyped.column<int64_t>(0) + untyped.column<float>(1)).find_next();
    CHECK(match == 0);    

    // Untyped, column objects
    Columns<int64_t> u0 = untyped.column<int64_t>(0);
    Columns<float> u1 = untyped.column<float>(1);

    match = (u0 + u1 > 40).find_next();
    CHECK(match == 1);    
    
    
    // Flexible language binding style
    Subexpr* first = new Columns<int64_t>(0);
    Subexpr* second = new Columns<float>(1);
    Subexpr* third = new Columns<double>(2);
    Subexpr* constant = new Value<int64_t>(40);    
    Subexpr* plus = new Operator<Plus<float> >(*first, *second);  
    Expression *e = new Compare<Greater, float>(*plus, *constant);


    // Bind table and do search
    match = untyped.where().expression(e).find_next();
    CHECK(match == 1);    

    Query q9 = untyped.where().expression(e);
    match = q9.find_next();
    CHECK(match == 1);    


    Subexpr* first2 = new Columns<int64_t>(0);
    Subexpr* second2 = new Columns<float>(1);
    Subexpr* third2 = new Columns<double>(2);
    Subexpr* constant2 = new Value<int64_t>(40);    
    Subexpr* plus2 = new Operator<Plus<float> >(*first, *second);  
    Expression *e2 = new Compare<Greater, float>(*plus, *constant);

    match = untyped.where().expression(e).expression(e2).find_next();
    CHECK(match == 1);    

    Query q10 = untyped.where().and_query(q9).expression(e2);
    match = q10.find_next();
    CHECK(match == 1);    


    Query tq3 = tq1;
    match = tq3.find_next();
    CHECK(match == 0);   
 
    delete e;
    delete plus;
    delete constant;
    delete third;
    delete second;
    delete first;


    delete e2;
    delete plus2;
    delete constant2;
    delete third2;
    delete second2;
    delete first2;
}

TEST(LimitUntyped)
{
    Table table;
    table.add_column(type_Int, "first1");
    table.add_column(type_Int, "second1");

    table.add_empty_row(3);
    table.set_int(0, 0, 10000);
    table.set_int(0, 1, 30000);
    table.set_int(0, 2, 10000);

    Query q = table.where();
    int64_t sum;
    
    sum = q.sum_int(0, NULL, 0, -1, 1);
    CHECK_EQUAL(10000, sum);

    sum = q.sum_int(0, NULL, 0, -1, 2);
    CHECK_EQUAL(40000, sum);

    sum = q.sum_int(0, NULL, 0, -1, 3);
    CHECK_EQUAL(50000, sum);

}


TEST(MergeQueriesOverloads)
{
    // Tests && and || overloads of Query class
    Table table;
    table.add_column(type_Int, "first");
    table.add_column(type_Int, "second");

    table.add_empty_row(3);
    table.set_int(0, 0, 20);
    table.set_int(1, 0, 20);        

    table.set_int(0, 1, 20);
    table.set_int(1, 1, 30);        

    table.set_int(0, 2, 30);
    table.set_int(1, 2, 30);        

    size_t c;



        // q1_0 && q2_0
    tightdb::Query q1_110 = table.where().equal(0, 20);
    tightdb::Query q2_110 = table.where().equal(1, 30);
    tightdb::Query q3_110 = q1_110.and_query(q2_110);
    c = q1_110.count();
    c = q2_110.count();
    c = q3_110.count();


    // The overloads must behave such as if each side of the operator is inside parentheses, that is,
    // (first == 1 || first == 20) operator&& (second == 30), regardless of order of operands
    
    // q1_0 && q2_0
    tightdb::Query q1_0 = table.where().equal(0, 10).Or().equal(0, 20);
    tightdb::Query q2_0 = table.where().equal(1, 30);
    tightdb::Query q3_0 = q1_0 && q2_0;
    c = q3_0.count();
    CHECK_EQUAL(1, c);
    
    // q2_0 && q1_0 (reversed operand order)
    tightdb::Query q1_1 = table.where().equal(0, 10).Or().equal(0, 20);
    tightdb::Query q2_1 = table.where().equal(1, 30);
    c = q1_1.count();

    tightdb::Query q3_1 = q2_1 && q1_1;
    c = q3_1.count();
    CHECK_EQUAL(1, c);

    // Short test for ||
    tightdb::Query q1_2 = table.where().equal(0, 10);
    tightdb::Query q2_2 = table.where().equal(1, 30);
    tightdb::Query q3_2 = q2_2 || q1_2;
    c = q3_2.count();
    CHECK_EQUAL(2, c);

}


TEST(MergeQueries)
{
    // test OR vs AND precedence
    Table table;
    table.add_column(type_Int, "first");
    table.add_column(type_Int, "second");

    table.add_empty_row(3);
    table.set_int(0, 0, 10);
    table.set_int(1, 0, 20);        

    table.set_int(0, 1, 20);
    table.set_int(1, 1, 30);        

    table.set_int(0, 2, 30);
    table.set_int(1, 2, 20);        

    // Must evaluate as if and_query is inside paranthesis, that is, (first == 1 || first == 20) && second == 30
    tightdb::Query q1_0 = table.where().equal(0, 10).Or().equal(0, 20);
    tightdb::Query q2_0 = table.where().and_query(q1_0).equal(1, 30);

    size_t c = q2_0.count();
    CHECK_EQUAL(1, c);
}





TEST(MergeQueriesMonkey)
{
    for(int iter = 0; iter < 5; iter++)
    {
        const size_t rows = 4000;
        Table table;
        table.add_column(type_Int, "first");
        table.add_column(type_Int, "second");
        table.add_column(type_Int, "third");

        for(size_t r = 0; r < rows; r++) {
            table.add_empty_row();
            table.set_int(0, r, rand() % 3);
            table.set_int(1, r, rand() % 3);        
            table.set_int(2, r, rand() % 3);        
        }

        size_t tvpos;

        // and_query(second == 1)
        tightdb::Query q1_0 = table.where().equal(1, 1);
        tightdb::Query q2_0 = table.where().and_query(q1_0);
        tightdb::TableView tv_0 = q2_0.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(1, r) == 1) {
                CHECK_EQUAL(r, tv_0.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // (first == 0 || first == 1) && and_query(second == 1)
        tightdb::Query q1_1 = table.where().equal(1, 1);
        tightdb::Query q2_1 = table.where().group().equal(0, 0).Or().equal(0, 1).end_group().and_query(q1_1);
        tightdb::TableView tv_1 = q2_1.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if((table.get_int(0, r) == 0 || table.get_int(0, r) == 1) && table.get_int(1, r) == 1) {
                CHECK_EQUAL(r, tv_1.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // first == 0 || (first == 1 && and_query(second == 1)) 
        tightdb::Query q1_2 = table.where().equal(1, 1);
        tightdb::Query q2_2 = table.where().equal(0, 0).Or().equal(0, 1).and_query(q1_2);
        tightdb::TableView tv_2 = q2_2.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(0, r) == 0 || (table.get_int(0, r) == 1 && table.get_int(1, r) == 1)) {
                CHECK_EQUAL(r, tv_2.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // and_query(first == 0) || (first == 1 && second == 1) 
        tightdb::Query q1_3 = table.where().equal(0, 0);
        tightdb::Query q2_3 = table.where().and_query(q1_3).Or().equal(0, 1).equal(1, 1);
        tightdb::TableView tv_3 = q2_3.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(0, r) == 0 || (table.get_int(0, r) == 1 && table.get_int(1, r) == 1)) {
                CHECK_EQUAL(r, tv_3.get_source_ndx(tvpos));
                tvpos++;
            }
        }


        // first == 0 || and_query(first == 1 && second == 1) 
        tightdb::Query q2_4 = table.where().equal(0, 1).equal(1, 1);
        tightdb::Query q1_4 = table.where().equal(0, 0).Or().and_query(q2_4);
        tightdb::TableView tv_4 = q1_4.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(0, r) == 0 || (table.get_int(0, r) == 1 && table.get_int(1, r) == 1)) {
                CHECK_EQUAL(r, tv_4.get_source_ndx(tvpos));
                tvpos++;
            }
        }


        // and_query(first == 0 || first == 2) || and_query(first == 1 && second == 1) 
        tightdb::Query q2_5 = table.where().equal(0, 0).Or().equal(0, 2);
        tightdb::Query q1_5 = table.where().equal(0, 1).equal(1, 1);
        tightdb::Query q3_5 = table.where().and_query(q2_5).Or().and_query(q1_5);
        tightdb::TableView tv_5 = q3_5.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if((table.get_int(0, r) == 0 || table.get_int(0, r) == 2) || (table.get_int(0, r) == 1 && table.get_int(1, r) == 1)) {
                CHECK_EQUAL(r, tv_5.get_source_ndx(tvpos));
                tvpos++;
            }
        }


        // and_query(first == 0) && and_query(second == 1)
        tightdb::Query q1_6 = table.where().equal(0, 0);
        tightdb::Query q2_6 = table.where().equal(1, 1);
        tightdb::Query q3_6 = table.where().and_query(q1_6).and_query(q2_6);
        tightdb::TableView tv_6 = q3_6.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(0, r) == 0 && table.get_int(1, r) == 1) {
                CHECK_EQUAL(r, tv_6.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // and_query(first == 0 || first == 2) && and_query(first == 1 || second == 1) 
        tightdb::Query q2_7 = table.where().equal(0, 0).Or().equal(0, 2);
        tightdb::Query q1_7 = table.where().equal(0, 1).equal(0, 1).Or().equal(1, 1);
        tightdb::Query q3_7 = table.where().and_query(q2_7).and_query(q1_7);
        tightdb::TableView tv_7 = q3_7.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if((table.get_int(0, r) == 0 || table.get_int(0, r) == 2) && (table.get_int(0, r) == 1 || table.get_int(1, r) == 1)) {
                CHECK_EQUAL(r, tv_7.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // Nested and_query

        // second == 0 && and_query(first == 0 || and_query(first == 2))
        tightdb::Query q2_8 = table.where().equal(0, 2);
        tightdb::Query q3_8 = table.where().equal(0, 0).Or().and_query(q2_8);
        tightdb::Query q4_8 = table.where().equal(1, 0).and_query(q3_8);    
        tightdb::TableView tv_8 = q4_8.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(1, r) == 0 && ((table.get_int(0, r) == 0) || table.get_int(0, r) == 2)) {
                CHECK_EQUAL(r, tv_8.get_source_ndx(tvpos));
                tvpos++;
            }
        }


        // Nested as above but constructed differently

        // second == 0 && and_query(first == 0 || and_query(first == 2))
        tightdb::Query q2_9 = table.where().equal(0, 2);
        tightdb::Query q5_9 = table.where().equal(0, 0);
        tightdb::Query q3_9 = table.where().and_query(q5_9).Or().and_query(q2_9);
        tightdb::Query q4_9 = table.where().equal(1, 0).and_query(q3_9);    
        tightdb::TableView tv_9 = q4_9.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(1, r) == 0 && ((table.get_int(0, r) == 0) || table.get_int(0, r) == 2)) {
                CHECK_EQUAL(r, tv_9.get_source_ndx(tvpos));
                tvpos++;
            }
        }
   

        // Nested

        // and_query(and_query(and_query(first == 0)))
        tightdb::Query q2_10 = table.where().equal(0, 0);
        tightdb::Query q5_10 = table.where().and_query(q2_10);
        tightdb::Query q3_10 = table.where().and_query(q5_10);
        tightdb::Query q4_10 = table.where().and_query(q3_10);    
        tightdb::TableView tv_10 = q4_10.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(0, r) == 0) {
                CHECK_EQUAL(r, tv_10.get_source_ndx(tvpos));
                tvpos++;
            }
        }

    }


}




TEST(MergeQueriesMonkeyOverloads)
{
    for(int iter = 0; iter < 5; iter++)
    {
        const size_t rows = 4000;
        Table table;
        table.add_column(type_Int, "first");
        table.add_column(type_Int, "second");
        table.add_column(type_Int, "third");


        for(size_t r = 0; r < rows; r++) {
            table.add_empty_row();
            table.set_int(0, r, rand() % 3);
            table.set_int(1, r, rand() % 3);        
            table.set_int(2, r, rand() % 3);        
        }

        size_t tvpos;

        // Left side of operator&& is empty query
        // and_query(second == 1)
        tightdb::Query q1_0 = table.where().equal(1, 1);
        tightdb::Query q2_0 = table.where() && q1_0;
        tightdb::TableView tv_0 = q2_0.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(1, r) == 1) {
                CHECK_EQUAL(r, tv_0.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // Right side of operator&& is empty query
        // and_query(second == 1)
        tightdb::Query q1_10 = table.where().equal(1, 1);
        tightdb::Query q2_10 = q1_10 && table.where();
        tightdb::TableView tv_10 = q2_10.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(1, r) == 1) {
                CHECK_EQUAL(r, tv_10.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // (first == 0 || first == 1) && and_query(second == 1)
        tightdb::Query q1_1 = table.where().equal(0, 0);
        tightdb::Query q2_1 = table.where().equal(0, 1);
        tightdb::Query q3_1 = q1_1 || q2_1;
        tightdb::Query q4_1 = table.where().equal(1, 1);
        tightdb::Query q5_1 = q3_1 && q4_1;

        tightdb::TableView tv_1 = q5_1.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if((table.get_int(0, r) == 0 || table.get_int(0, r) == 1) && table.get_int(1, r) == 1) {
                CHECK_EQUAL(r, tv_1.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // (first == 0 || first == 1) && and_query(second == 1) as above, written in another way
        tightdb::Query q1_20 = table.where().equal(0, 0).Or().equal(0, 1) && table.where().equal(1, 1);
        tightdb::TableView tv_20 = q1_20.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if((table.get_int(0, r) == 0 || table.get_int(0, r) == 1) && table.get_int(1, r) == 1) {
                CHECK_EQUAL(r, tv_20.get_source_ndx(tvpos));
                tvpos++;
            }
        }

        // and_query(first == 0) || (first == 1 && second == 1) 
        tightdb::Query q1_3 = table.where().equal(0, 0);
        tightdb::Query q2_3 = table.where().equal(0, 1);
        tightdb::Query q3_3 = table.where().equal(1, 1);
        tightdb::Query q4_3 = q1_3 || (q2_3 && q3_3); 
        tightdb::TableView tv_3 = q4_3.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(0, r) == 0 || (table.get_int(0, r) == 1 && table.get_int(1, r) == 1)) {
                CHECK_EQUAL(r, tv_3.get_source_ndx(tvpos));
                tvpos++;
            }
        }


        // and_query(first == 0) || (first == 1 && second == 1) written in another way
        tightdb::Query q1_30 = table.where().equal(0, 0);
        tightdb::Query q3_30 = table.where().equal(1, 1);
        tightdb::Query q4_30 = table.where().equal(0, 0) || (table.where().equal(0, 1) && q3_30); 
        tightdb::TableView tv_30 = q4_30.find_all();    
        tvpos = 0;
        for(size_t r = 0; r < rows; r++) {
            if(table.get_int(0, r) == 0 || (table.get_int(0, r) == 1 && table.get_int(1, r) == 1)) {
                CHECK_EQUAL(r, tv_30.get_source_ndx(tvpos));
                tvpos++;
            }
        }

    }


}


TEST(CountLimit)
{
    PeopleTable2 table;

    table.add("Mary",  14);
    table.add("Joe",   17);
    table.add("Alice", 42);
    table.add("Jack",  22);
    table.add("Bob",   50);
    table.add("Frank", 12);

    // Select rows where age < 18
    PeopleTable2::Query query = table.where().age.less(18);

    // Count all matching rows of entire table
    size_t count1 = query.count();
    CHECK_EQUAL(3, count1);

    // Very fast way to test if there are at least 2 matches in the table
    size_t count2 = query.count(0, size_t(-1), 2);
    CHECK_EQUAL(2, count2);

    // Count matches in latest 3 rows
    size_t count3 = query.count(table.size() - 3, table.size());
    CHECK_EQUAL(1, count3);
}

TEST(QueryExpressions0)
{
/*
    We have following variables to vary in the tests:

    left        right
    +           -           *           /
    Subexpr    Column       Value    
    >           <           ==          !=          >=          <=
    float       int         double      int64_t

    Many of them are combined and tested together in equality classes below
*/
    Table table;
    table.add_column(type_Int, "first1");
    table.add_column(type_Float, "second1");
    table.add_column(type_Double, "third");

    size_t match;

    Columns<int64_t> first = table.column<int64_t>(0);
    Columns<float> second = table.column<float>(1);
    Columns<double> third = table.column<double>(2);

    table.add_empty_row(2);

    table.set_int(0, 0, 20);
    table.set_float(1, 0, 19.9f);
    table.set_double(2, 0, 3.0);

    table.set_int(0, 1, 20);
    table.set_float(1, 1, 20.1f);
    table.set_double(2, 1, 4.0);
   
    // 20 must convert to float    
    match = (second + 0.2f > 20).find_next();
    CHECK(match == 0);

    match = (first >= 20.0f).find_next();
    CHECK(match == 0);

    // 20.1f must remain float
    match = (first >= 20.1f).find_next();
    CHECK(match == not_found);

    // first must convert to float
    match = (second >= first).find_next();
    CHECK(match == 1);

    // 20 and 40 must convert to float
    match = (second + 20 > 40).find_next();
    CHECK(match == 1);

    // first and 40 must convert to float
    match = (second + first >= 40).find_next();
    CHECK(match == 1);

    // 20 must convert to float
    match = (0.2f + second > 20).find_next();
    CHECK(match == 0);

    // Compare, left = Subexpr, right = Value
    match = (second + first >= 40).find_next();
    CHECK(match == 1);

    match = (second + first > 40).find_next();
    CHECK(match == 1);

    match = (first - second < 0).find_next();
    CHECK(match == 1);

    match = (second - second == 0).find_next();
    CHECK(match == 0);

    match = (first - second <= 0).find_next();
    CHECK(match == 1);

    match = (first * first != 400).find_next();
    CHECK(match == size_t(-1));
  
    // Compare, left = Column, right = Value
    match = (second >= 20).find_next();
    CHECK(match == 1);

    match = (second > 20).find_next();
    CHECK(match == 1);

    match = (second < 20).find_next();
    CHECK(match == 0);

    match = (second == 20.1f).find_next();
    CHECK(match == 1);

    match = (second != 19.9f).find_next();
    CHECK(match == 1);

    match = (second <= 21).find_next();
    CHECK(match == 0);

    // Compare, left = Column, right = Value
    match = (20 <= second).find_next();
    CHECK(match == 1);

    match = (20 < second).find_next();
    CHECK(match == 1);

    match = (20 > second).find_next();
    CHECK(match == 0);

    match = (20.1f == second).find_next();
    CHECK(match == 1);

    match = (19.9f != second).find_next();
    CHECK(match == 1);

    match = (21 >= second).find_next();
    CHECK(match == 0);

    // Compare, left = Subexpr, right = Value
    match = (40 <= second + first).find_next();
    CHECK(match == 1);

    match = (40 < second + first).find_next();
    CHECK(match == 1);

    match = (0 > first - second).find_next();
    CHECK(match == 1);

    match = (0 == second - second).find_next();
    CHECK(match == 0);

    match = (0 >= first - second).find_next();
    CHECK(match == 1);

    match = (400 != first * first).find_next();
    CHECK(match == size_t(-1));

    // Col compare Col
    match = (second > first).find_next();
    CHECK(match == 1);

    match = (second >= first).find_next();
    CHECK(match == 1);

    match = (second == first).find_next();
    CHECK(match == not_found);

    match = (second != second).find_next();
    CHECK(match == not_found);

    match = (first < second).find_next();
    CHECK(match == 1);

    match = (first <= second).find_next();
    CHECK(match == 1);

    // Subexpr compare Subexpr
    match = (second + 0 > first + 0).find_next();
    CHECK(match == 1);

    match = (second + 0 >= first + 0).find_next();
    CHECK(match == 1);

    match = (second + 0 == first + 0).find_next();
    CHECK(match == not_found);

    match = (second + 0 != second + 0).find_next();
    CHECK(match == not_found);

    match = (first + 0 < second + 0).find_next();
    CHECK(match == 1);

    match = (first + 0 <= second + 0).find_next();
    CHECK(match == 1);

    // Conversions, again
    table.clear();
    table.add_empty_row(1);

    table.set_int(0, 0, 20);
    table.set_float(1, 0, 3.0);
    table.set_double(2, 0, 3.0);

    match = (1 / second == 1 / second).find_next();
    CHECK(match == 0);

    match = (1 / third == 1 / third).find_next();
    CHECK(match == 0);

    // Compare operator must preserve precision of each side, hence no match
    match = (1 / second == 1 / third).find_next();
    CHECK(match == not_found);
}

TEST(LimitUntyped2)
{
    Table table;
    table.add_column(type_Int, "first1");
    table.add_column(type_Float, "second1");
    table.add_column(type_Double, "second1");

    table.add_empty_row(3);
    table.set_int(0, 0, 10000);
    table.set_int(0, 1, 30000);
    table.set_int(0, 2, 40000);

    table.set_float(1, 0, 10000.);
    table.set_float(1, 1, 30000.);
    table.set_float(1, 2, 40000.);

    table.set_double(2, 0, 10000.);
    table.set_double(2, 1, 30000.);
    table.set_double(2, 2, 40000.);


    Query q = table.where();
    int64_t sum;
    float sumf;
    double sumd;
    
    // sum, limited by 'limit'
    sum = q.sum_int(0, NULL, 0, -1, 1);
    CHECK_EQUAL(10000, sum);
    sum = q.sum_int(0, NULL, 0, -1, 2);
    CHECK_EQUAL(40000, sum);
    sum = q.sum_int(0, NULL, 0, -1);
    CHECK_EQUAL(80000, sum);

    sumd = q.sum_float(1, NULL, 0, -1, 1);
    CHECK_EQUAL(10000., sumd);
    sumd = q.sum_float(1, NULL, 0, -1, 2);
    CHECK_EQUAL(40000., sumd);
    sumd = q.sum_float(1, NULL, 0, -1);
    CHECK_EQUAL(80000., sumd);

    sumd = q.sum_double(2, NULL, 0, -1, 1);
    CHECK_EQUAL(10000., sumd);
    sumd = q.sum_double(2, NULL, 0, -1, 2);
    CHECK_EQUAL(40000., sumd);
    sumd = q.sum_double(2, NULL, 0, -1);
    CHECK_EQUAL(80000., sumd);

    // sum, limited by 'end', but still having 'limit' specified
    sum = q.sum_int(0, NULL, 0, 1, 3);
    CHECK_EQUAL(10000, sum);
    sum = q.sum_int(0, NULL, 0, 2, 3);
    CHECK_EQUAL(40000, sum);

    sumd = q.sum_float(1, NULL, 0, 1, 3);
    CHECK_EQUAL(10000., sumd);
    sumd = q.sum_float(1, NULL, 0, 2, 3);
    CHECK_EQUAL(40000., sumd);

    sumd = q.sum_double(2, NULL, 0, 1, 3);
    CHECK_EQUAL(10000., sumd);
    sumd = q.sum_double(2, NULL, 0, 2, 3);
    CHECK_EQUAL(40000., sumd);

    // max, limited by 'limit'
    sum = q.maximum_int(0, NULL, 0, -1, 1);
    CHECK_EQUAL(10000, sum);
    sum = q.maximum_int(0, NULL, 0, -1, 2);
    CHECK_EQUAL(30000, sum);
    sum = q.maximum_int(0, NULL, 0, -1);
    CHECK_EQUAL(40000, sum);

    sumf = q.maximum_float(1, NULL, 0, -1, 1);
    CHECK_EQUAL(10000., sumf);
    sumf = q.maximum_float(1, NULL, 0, -1, 2);
    CHECK_EQUAL(30000., sumf);
    sumf = q.maximum_float(1, NULL, 0, -1);
    CHECK_EQUAL(40000., sumf);

    sumd = q.maximum_double(2, NULL, 0, -1, 1);
    CHECK_EQUAL(10000., sumd);
    sumd = q.maximum_double(2, NULL, 0, -1, 2);
    CHECK_EQUAL(30000., sumd);
    sumd = q.maximum_double(2, NULL, 0, -1);
    CHECK_EQUAL(40000., sumd);


    // max, limited by 'end', but still having 'limit' specified
    sum = q.maximum_int(0, NULL, 0, 1, 3);
    CHECK_EQUAL(10000, sum);
    sum = q.maximum_int(0, NULL, 0, 2, 3);
    CHECK_EQUAL(30000, sum);

    sumf = q.maximum_float(1, NULL, 0, 1, 3);
    CHECK_EQUAL(10000., sumf);
    sumf = q.maximum_float(1, NULL, 0, 2, 3);
    CHECK_EQUAL(30000., sumf);

    sumd = q.maximum_double(2, NULL, 0, 1, 3);
    CHECK_EQUAL(10000., sumd);
    sumd = q.maximum_double(2, NULL, 0, 2, 3);
    CHECK_EQUAL(30000., sumd);


    // avg
    sumd = q.average_int(0, NULL, 0, -1, 1);
    CHECK_EQUAL(10000, sumd);
    sumd = q.average_int(0, NULL, 0, -1, 2);
    CHECK_EQUAL((10000 + 30000) / 2, sumd);

    sumd = q.average_float(1, NULL, 0, -1, 1);
    CHECK_EQUAL(10000., sumd);
    sumd = q.average_float(1, NULL, 0, -1, 2);
    CHECK_EQUAL((10000. + 30000.) / 2., sumd);


    // avg, limited by 'end', but still having 'limit' specified
    sumd = q.average_int(0, NULL, 0, 1, 3);
    CHECK_EQUAL(10000, sumd);
    sumd = q.average_int(0, NULL, 0, 2, 3);
    CHECK_EQUAL((10000 + 30000) / 2, sumd);

    sumd = q.average_float(1, NULL, 0, 1, 3);
    CHECK_EQUAL(10000., sumd);
    sumd = q.average_float(1, NULL, 0, 2, 3);
    CHECK_EQUAL((10000. + 30000.) / 2., sumd);

}


TEST(TestQueryStrIndexCrash)
{
    // Rasmus "8" index crash
    for(int iter = 0; iter < 5; ++iter) {
        Group group;
        TableRef table = group.get_table("test");

        Spec& s = table->get_spec();
        s.add_column(type_String, "first");
        table->update_from_spec();

        size_t eights = 0;

        for(int i = 0; i < 2000; ++i) {
            int v = rand() % 10;
            if(v == 8) {
                eights++;
            }
            char dst[100];
            memset(dst, 0, sizeof(dst));
            sprintf(dst,"%d",v);
            table->insert_string(0, i, dst);
            table->insert_done();
        }

        table->set_index(0);
        TableView v = table->where().equal(0, StringData("8")).find_all();
        CHECK_EQUAL(eights, v.size());

        v = table->where().equal(0, StringData("10")).find_all();

        v = table->where().equal(0, StringData("8")).find_all();
        CHECK_EQUAL(eights, v.size());
    }
}


TEST(QueryTwoColsEqualVaryWidthAndValues)
{
    vector<size_t> ints1;
    vector<size_t> ints2;
    vector<size_t> ints3;

    vector<size_t> floats;
    vector<size_t> doubles;

    Table table;
    table.add_column(type_Int, "first1");
    table.add_column(type_Int, "second1");

    table.add_column(type_Int, "first2");
    table.add_column(type_Int, "second2");

    table.add_column(type_Int, "first3");
    table.add_column(type_Int, "second3");

    table.add_column(type_Float, "third");
    table.add_column(type_Float, "fourth");
    table.add_column(type_Double, "fifth");
    table.add_column(type_Double, "sixth");

#ifdef TIGHTDB_DEBUG
    for (int i = 0; i < 5000; i++) {
#else
    for (int i = 0; i < 50000; i++) {
#endif
        table.add_empty_row();

        // Important thing to test is different bitwidths because we might use SSE and/or bithacks on 64-bit blocks

        // Both are bytes
        table.set_int(0, i, rand() % 100);
        table.set_int(1, i, rand() % 100);

        // Second column widest
        table.set_int(2, i, rand() % 10);
        table.set_int(3, i, rand() % 100);

        // First column widest
        table.set_int(4, i, rand() % 100);
        table.set_int(5, i, rand() % 10);

        table.set_float(6, i, float(rand() % 10));
        table.set_float(7, i, float(rand() % 10));

        table.set_double(8, i, double(rand() % 10));
        table.set_double(9, i, double(rand() % 10));

        if (table.get_int(0, i) == table.get_int(1, i))
            ints1.push_back(i);

        if (table.get_int(2, i) == table.get_int(3, i))
            ints2.push_back(i);

        if (table.get_int(4, i) == table.get_int(5, i))
            ints3.push_back(i);

        if (table.get_float(6, i) == table.get_float(7, i))
            floats.push_back(i);

        if (table.get_double(8, i) == table.get_double(9, i))
            doubles.push_back(i);

    }

    tightdb::TableView t1 = table.where().equal_int(size_t(0), size_t(1)).find_all();
    tightdb::TableView t2 = table.where().equal_int(size_t(2), size_t(3)).find_all();
    tightdb::TableView t3 = table.where().equal_int(size_t(4), size_t(5)).find_all();

    tightdb::TableView t4 = table.where().equal_float(size_t(6), size_t(7)).find_all();
    tightdb::TableView t5 = table.where().equal_double(size_t(8), size_t(9)).find_all();


    CHECK_EQUAL(ints1.size(), t1.size());
    for (size_t t = 0; t < ints1.size(); t++)
        CHECK_EQUAL(ints1[t], t1.get_source_ndx(t));

    CHECK_EQUAL(ints2.size(), t2.size());
    for (size_t t = 0; t < ints2.size(); t++)
        CHECK_EQUAL(ints2[t], t2.get_source_ndx(t));

    CHECK_EQUAL(ints3.size(), t3.size());
    for (size_t t = 0; t < ints3.size(); t++)
        CHECK_EQUAL(ints3[t], t3.get_source_ndx(t));

    CHECK_EQUAL(floats.size(), t4.size());
    for (size_t t = 0; t < floats.size(); t++)
        CHECK_EQUAL(floats[t], t4.get_source_ndx(t));

    CHECK_EQUAL(doubles.size(), t5.size());
    for (size_t t = 0; t < doubles.size(); t++)
        CHECK_EQUAL(doubles[t], t5.get_source_ndx(t));
}

TEST(QueryTwoColsVaryOperators)
{
    vector<size_t> ints1;
    vector<size_t> floats;
    vector<size_t> doubles;

    Table table;
    table.add_column(type_Int, "first1");
    table.add_column(type_Int, "second1");

    table.add_column(type_Float, "third");
    table.add_column(type_Float, "fourth");
    table.add_column(type_Double, "fifth");
    table.add_column(type_Double, "sixth");

    // row 0
    table.add_empty_row();
    table.set_int(0, 0, 5);
    table.set_int(1, 0, 10);
    table.set_float(2, 0, 5);
    table.set_float(3, 0, 10);
    table.set_double(4, 0, 5);
    table.set_double(5, 0, 10);

    // row 1
    table.add_empty_row();
    table.set_int(0, 1, 10);
    table.set_int(1, 1, 5);
    table.set_float(2, 1, 10);
    table.set_float(3, 1, 5);
    table.set_double(4, 1, 10);
    table.set_double(5, 1, 5);

    // row 2
    table.add_empty_row();
    table.set_int(0, 2, -10);
    table.set_int(1, 2, -5);
    table.set_float(2, 2, -10);
    table.set_float(3, 2, -5);
    table.set_double(4, 2, -10);
    table.set_double(5, 2, -5);


    CHECK_EQUAL(not_found, table.where().equal_int(size_t(0), size_t(1)).find_next());
    CHECK_EQUAL(0, table.where().not_equal_int(size_t(0), size_t(1)).find_next());
    CHECK_EQUAL(0, table.where().less_int(size_t(0), size_t(1)).find_next());
    CHECK_EQUAL(1, table.where().greater_int(size_t(0), size_t(1)).find_next());
    CHECK_EQUAL(1, table.where().greater_equal_int(size_t(0), size_t(1)).find_next());
    CHECK_EQUAL(0, table.where().less_equal_int(size_t(0), size_t(1)).find_next());

    CHECK_EQUAL(not_found, table.where().equal_float(size_t(2), size_t(3)).find_next());
    CHECK_EQUAL(0, table.where().not_equal_float(size_t(2), size_t(3)).find_next());
    CHECK_EQUAL(0, table.where().less_float(size_t(2), size_t(3)).find_next());
    CHECK_EQUAL(1, table.where().greater_float(size_t(2), size_t(3)).find_next());
    CHECK_EQUAL(1, table.where().greater_equal_float(size_t(2), size_t(3)).find_next());
    CHECK_EQUAL(0, table.where().less_equal_float(size_t(2), size_t(3)).find_next());

    CHECK_EQUAL(not_found, table.where().equal_double(size_t(4), size_t(5)).find_next());
    CHECK_EQUAL(0, table.where().not_equal_double(size_t(4), size_t(5)).find_next());
    CHECK_EQUAL(0, table.where().less_double(size_t(4), size_t(5)).find_next());
    CHECK_EQUAL(1, table.where().greater_double(size_t(4), size_t(5)).find_next());
    CHECK_EQUAL(1, table.where().greater_equal_double(size_t(4), size_t(5)).find_next());
    CHECK_EQUAL(0, table.where().less_equal_double(size_t(4), size_t(5)).find_next());
}



TEST(QueryTwoCols0)
{
    Table table;
    table.add_column(type_Int, "first1");
    table.add_column(type_Int, "second1");


    for (int i = 0; i < 50; i++) {
        table.add_empty_row();
        table.set_int(0, i, 0);
        table.set_int(1, i, 0);
    }

    tightdb::TableView t1 = table.where().equal_int(size_t(0), size_t(1)).find_all();
    CHECK_EQUAL(50, t1.size());

    tightdb::TableView t2 = table.where().less_int(size_t(0), size_t(1)).find_all();
    CHECK_EQUAL(0, t2.size());
}


TEST(QueryTwoColsNoRows)
{
    Table table;
    table.add_column(type_Int, "first1");
    table.add_column(type_Int, "second1");

    CHECK_EQUAL(not_found, table.where().equal_int(size_t(0), size_t(1)).find_next());
    CHECK_EQUAL(not_found, table.where().not_equal_int(size_t(0), size_t(1)).find_next());
}


TEST(TestQueryHuge)
{
#if TEST_DURATION == 0
    for (int N = 0; N < 2; N++) {
#elif TEST_DURATION == 1
    for (int N = 0; N < 100; N++) {
#elif TEST_DURATION == 2
    for (int N = 0; N < 1000; N++) {
#elif TEST_DURATION == 3
    for (int N = 0; N < 10000; N++) {
#endif
        srand(N + 123);    // Makes you reproduce a bug in a certain run, without having to run all successive runs

        TripleTable tt;
        TripleTable::View v;
        bool long1 = false;
        bool long2 = false;

        size_t mdist1 = 1;
        size_t mdist2 = 1;
        size_t mdist3 = 1;

        string first;
        string second;
        int64_t third;

        size_t res1 = 0;
        size_t res2 = 0;
        size_t res3 = 0;
        size_t res4 = 0;
        size_t res5 = 0;
        size_t res6 = 0;
        size_t res7 = 0;
        size_t res8 = 0;

        size_t start = rand() % 6000;
        size_t end = start + rand() % (6000 - start);
        size_t limit;
        if(rand() % 2 == 0)
            limit = rand() % 10000;
        else
            limit = size_t(-1);


        size_t blocksize = rand() % 800 + 1;

        for (size_t row = 0; row < 6000; row++) {

            if (row % blocksize == 0) {
                long1 = (rand() % 2 == 0);
                long2 = (rand() % 2 == 0);

                if (rand() % 2 == 0) {
                    mdist1 = rand() % 500 + 1;
                    mdist2 = rand() % 500 + 1;
                    mdist3 = rand() % 500 + 1;
                }
                else {
                    mdist1 = rand() % 5 + 1;
                    mdist2 = rand() % 5 + 1;
                    mdist3 = rand() % 5 + 1;
                }
            }

            tt.add_empty_row();

            if (long1) {
                if (rand() % mdist1 == 0)
                    first = "longlonglonglonglonglonglong A";
                else
                    first = "longlonglonglonglonglonglong B";
            }
            else {
                if (rand() % mdist1 == 0)
                    first = "A";
                else
                    first = "B";
            }

            if (long2) {
                if (rand() % mdist2 == 0)
                    second = "longlonglonglonglonglonglong A";
                else
                    second = "longlonglonglonglonglonglong B";
            }
            else {
                if (rand() % mdist2 == 0)
                    second = "A";
                else
                    second = "B";
            }

            if (rand() % mdist3 == 0)
                third = 1;
            else
                third = 2;

            tt[row].first  = first;
            tt[row].second = second;
            tt[row].third  = third;






            if ((row >= start && row < end && limit > res1) && (first == "A" && second == "A" && third == 1))
                res1++;

            if ((row >= start && row < end && limit > res2) && ((first == "A" || second == "A") && third == 1))
                res2++;

            if ((row >= start && row < end && limit > res3) && (first == "A" && (second == "A" || third == 1)))
                res3++;

            if ((row >= start && row < end && limit > res4) && (second == "A" && (first == "A" || third == 1)))
                res4++;

            if ((row >= start && row < end && limit > res5) && (first == "A" || second == "A" || third == 1))
                res5++;

            if ((row >= start && row < end && limit > res6) && (first != "A" && second == "A" && third == 1))
                res6++;

            if ((row >= start && row < end && limit > res7) && (first != "longlonglonglonglonglonglong A" && second == "A" && third == 1))
                res7++;

            if ((row >= start && row < end && limit > res8) && (first != "longlonglonglonglonglonglong A" && second == "A" && third == 2))
                res8++;
        }

        for (size_t t = 0; t < 4; t++) {

            if (t == 1)
                tt.optimize();
            else if (t == 2)
                tt.column().first.set_index();
            else if (t == 3)
                tt.column().second.set_index();



            v = tt.where().first.equal("A").second.equal("A").third.equal(1).find_all(start, end, limit);
            CHECK_EQUAL(res1, v.size());

            v = tt.where().second.equal("A").first.equal("A").third.equal(1).find_all(start, end, limit);
            CHECK_EQUAL(res1, v.size());

            v = tt.where().third.equal(1).second.equal("A").first.equal("A").find_all(start, end, limit);
            CHECK_EQUAL(res1, v.size());

            v = tt.where().group().first.equal("A").Or().second.equal("A").end_group().third.equal(1).find_all(start, end, limit);
            CHECK_EQUAL(res2, v.size());

            v = tt.where().first.equal("A").group().second.equal("A").Or().third.equal(1).end_group().find_all(start, end, limit);
            CHECK_EQUAL(res3, v.size());

            TripleTable::Query q = tt.where().group().first.equal("A").Or().third.equal(1).end_group().second.equal("A");
            v = q.find_all(start, end, limit);
            CHECK_EQUAL(res4, v.size());

            v = tt.where().group().first.equal("A").Or().third.equal(1).end_group().second.equal("A").find_all(start, end, limit);
            CHECK_EQUAL(res4, v.size());

            v = tt.where().first.equal("A").Or().second.equal("A").Or().third.equal(1).find_all(start, end, limit);
            CHECK_EQUAL(res5, v.size());

            v = tt.where().first.not_equal("A").second.equal("A").third.equal(1).find_all(start, end, limit);
            CHECK_EQUAL(res6, v.size());

            v = tt.where().first.not_equal("longlonglonglonglonglonglong A").second.equal("A").third.equal(1).find_all(start, end, limit);
            CHECK_EQUAL(res7, v.size());

            v = tt.where().first.not_equal("longlonglonglonglonglonglong A").second.equal("A").third.equal(2).find_all(start, end, limit);
            CHECK_EQUAL(res8, v.size());
        }
    }
}


TEST(TestQueryStrIndex3)
{
    // Create two columns where query match-density varies alot throughout the rows. This forces the query engine to
    // jump back and forth between the two conditions and test edge cases in these transitions. Tests combinations of
    // linear scan, enum and index

#ifdef TIGHTDB_DEBUG
    for (int N = 0; N < 4; N++) {
#else
    for (int N = 0; N < 20; N++) {
#endif
        TupleTableType ttt;

        vector<size_t> vec;
        size_t row = 0;

        size_t n = 0;
#ifdef TIGHTDB_DEBUG
        for (int i = 0; i < 4; i++) {
#else
        for (int i = 0; i < 20; i++) {
#endif
            // 1/500 match probability because we want possibility for a 1000 sized leaf to contain 0 matches (important
            // edge case)
            int f1 = rand() % 500 + 1;
            int f2 = rand() % 500 + 1;
            bool longstrings = (rand() % 5 == 1);

            // 2200 entries with that probability to fill out two concecutive 1000 sized leafs with above probability,
            // plus a remainder (edge case)
            for (int j = 0; j < 2200; j++) {
                if (rand() % f1 == 0)
                    if (rand() % f2 == 0) {
                        ttt.add(0, longstrings ? "AAAAAAAAAAAAAAAAAAAAAAAA" : "AA");
                        if (!longstrings) {
                            n++;
                            vec.push_back(row);
                        }
                    }
                    else
                        ttt.add(0, "BB");
                else
                    if (rand() % f2 == 0)
                        ttt.add(1, "AA");
                    else
                        ttt.add(1, "BB");

                row++;
            }
        }

        TupleTableType::View v;

        // Both linear scans
        v = ttt.where().second.equal("AA").first.equal(0).find_all();
        CHECK_EQUAL(vec.size(), v.size());
        for (size_t t = 0; t < vec.size(); t++)
            CHECK_EQUAL(vec[t], v.get_source_ndx(t));
        v.clear();
        vec.clear();

        v = ttt.where().first.equal(0).second.equal("AA").find_all();
        CHECK_EQUAL(vec.size(), v.size());
        for (size_t t = 0; t < vec.size(); t++)
            CHECK_EQUAL(vec[t], v.get_source_ndx(t));
        v.clear();
        vec.clear();

        ttt.optimize();

        // Linear scan over enum, plus linear integer column scan
        v = ttt.where().second.equal("AA").first.equal(0).find_all();
        CHECK_EQUAL(vec.size(), v.size());
        for (size_t t = 0; t < vec.size(); t++)
            CHECK_EQUAL(vec[t], v.get_source_ndx(t));
        v.clear();
        vec.clear();

        v = ttt.where().first.equal(0).second.equal("AA").find_all();
        CHECK_EQUAL(vec.size(), v.size());
        for (size_t t = 0; t < vec.size(); t++)
            CHECK_EQUAL(vec[t], v.get_source_ndx(t));
        v.clear();
        vec.clear();

        ttt.column().second.set_index();

        // Index lookup, plus linear integer column scan
        v = ttt.where().second.equal("AA").first.equal(0).find_all();
        CHECK_EQUAL(vec.size(), v.size());
        for (size_t t = 0; t < vec.size(); t++)
            CHECK_EQUAL(vec[t], v.get_source_ndx(t));
        v.clear();
        vec.clear();

        v = ttt.where().first.equal(0).second.equal("AA").find_all();
        CHECK_EQUAL(vec.size(), v.size());
        for (size_t t = 0; t < vec.size(); t++)
            CHECK_EQUAL(vec[t], v.get_source_ndx(t));
        v.clear();
        vec.clear();
    }
}


TEST(TestQueryStrIndex2)
{
    TupleTableType ttt;

    int64_t s;

    for (int i = 0; i < 100; ++i) {
        ttt.add(1, "AA");
    }
    ttt.add(1, "BB");
    ttt.column().second.set_index();

    s = ttt.where().second.equal("AA").count();
    CHECK_EQUAL(100, s);

    s = ttt.where().second.equal("BB").count();
    CHECK_EQUAL(1, s);

    s = ttt.where().second.equal("CC").count();
    CHECK_EQUAL(0, s);
}


TEST(TestQueryStrEnum)
{
    TupleTableType ttt;

    int aa;
    int64_t s;

    for (int i = 0; i < 100; ++i) {
        ttt.clear();
        aa = 0;
        for (size_t t = 0; t < 2000; ++t) {
            if (rand() % 3 == 0) {
                ttt.add(1, "AA");
                ++aa;
            }
            else {
                ttt.add(1, "BB");
            }
        }
        ttt.optimize();
        s = ttt.where().second.equal("AA").count();
        CHECK_EQUAL(aa, s);
    }
}


TEST(TestQueryStrIndex)
{
#ifdef TIGHTDB_DEBUG
    size_t itera = 4;
    size_t iterb = 100;
#else
    size_t itera = 100;
    size_t iterb = 2000;
#endif

    int aa;
    int64_t s;

    for (size_t i = 0; i < itera; i++) {
        TupleTableType ttt;
        aa = 0;
        for (size_t t = 0; t < iterb; t++) {
            if (rand() % 3 == 0) {
                ttt.add(1, "AA");
                aa++;
            }
            else {
                ttt.add(1, "BB");
            }
        }

        s = ttt.where().second.equal("AA").count();
        CHECK_EQUAL(aa, s);

        ttt.optimize();
        s = ttt.where().second.equal("AA").count();
        CHECK_EQUAL(aa, s);

        ttt.column().second.set_index();
        s = ttt.where().second.equal("AA").count();
        CHECK_EQUAL(aa, s);
    }

}


TEST(Group_GameAnalytics)
{
    {
        Group g;
        GATable::Ref t = g.get_table<GATable>("firstevents");

        for (size_t i = 0; i < 1000; ++i) {
            const int64_t r1 = rand() % 1000;
            const int64_t r2 = rand() % 1000;

            t->add("10", "US", "1.0", r1, r2);
        }
        t->optimize();
        File::try_remove("ga_test.tightdb");
        g.write("ga_test.tightdb");
    }

    Group g("ga_test.tightdb");
    GATable::Ref t = g.get_table<GATable>("firstevents");

    GATable::Query q = t->where().country.equal("US");

    size_t c1 = 0;
    for (size_t i = 0; i < 100; ++i) {
        c1 += t->column().country.count("US");
    }

    size_t c2 = 0;
    for (size_t i = 0; i < 100; ++i) {
        c2 += q.count();
    }

    CHECK_EQUAL(c1, t->size() * 100);
    CHECK_EQUAL(c1, c2);
}


TEST(TestQueryFloat3)
{
    FloatTable3 t;

    t.add(float(1.1), double(2.1), 1);
    t.add(float(1.2), double(2.2), 2);
    t.add(float(1.3), double(2.3), 3);
    t.add(float(1.4), double(2.4), 4); // match
    t.add(float(1.5), double(2.5), 5); // match
    t.add(float(1.6), double(2.6), 6); // match
    t.add(float(1.7), double(2.7), 7);
    t.add(float(1.8), double(2.8), 8);
    t.add(float(1.9), double(2.9), 9);

    FloatTable3::Query q1 = t.where().col_float.greater(1.35f).col_double.less(2.65);
    int64_t a1 = q1.col_int.sum();
    CHECK_EQUAL(15, a1);

    FloatTable3::Query q2 = t.where().col_double.less(2.65).col_float.greater(1.35f);
    int64_t a2 = q2.col_int.sum();
    CHECK_EQUAL(15, a2);

    FloatTable3::Query q3 = t.where().col_double.less(2.65).col_float.greater(1.35f);
    double a3 = q3.col_float.sum();
    double sum3 = double(1.4f) + double(1.5f) + double(1.6f);
    CHECK_EQUAL(sum3, a3);

    FloatTable3::Query q4 = t.where().col_float.greater(1.35f).col_double.less(2.65);
    double a4 = q4.col_float.sum();
    CHECK_EQUAL(sum3, a4);

    FloatTable3::Query q5 = t.where().col_int.greater_equal(4).col_double.less(2.65);
    double a5 = q5.col_float.sum();
    CHECK_EQUAL(sum3, a5);

    FloatTable3::Query q6 = t.where().col_double.less(2.65).col_int.greater_equal(4);
    double a6 = q6.col_float.sum();
    CHECK_EQUAL(sum3, a6);

    FloatTable3::Query q7 = t.where().col_int.greater(3).col_int.less(7);
    int64_t a7 = q7.col_int.sum();
    CHECK_EQUAL(15, a7);
    FloatTable3::Query q8 = t.where().col_int.greater(3).col_int.less(7);
    int64_t a8 = q8.col_int.sum();
    CHECK_EQUAL(15, a8);
}

TEST(TestTableViewSum)
{
    TableViewSum ttt;

    ttt.add(1.0, 1.0, 1);
    ttt.add(2.0, 2.0, 2);
    ttt.add(3.0, 3.0, 3);
    ttt.add(4.0, 4.0, 4);
    ttt.add(5.0, 5.0, 5);
    ttt.add(6.0, 6.0, 6);
    ttt.add(7.0, 7.0, 7);
    ttt.add(8.0, 8.0, 8);
    ttt.add(9.0, 9.0, 9);
    ttt.add(10.0, 10.0, 10);

    TableViewSum::Query q1 = ttt.where().col_int.between(5, 9);
    TableViewSum::View tv1 = q1.find_all();
    int64_t s = tv1.column().col_int.sum();
    CHECK_EQUAL(5 + 6 + 7 + 8 + 9, s);
}


TEST(TestQueryJavaMinimumCrash)
{
    // Test that triggers a bug that was discovered through Java intnerface and has been fixed
    PHPMinimumCrash ttt;

    ttt.add("Joe", "John", 1);
    ttt.add("Jane", "Doe", 2);
    ttt.add("Bob", "Hanson", 3);

    PHPMinimumCrash::Query q1 = ttt.where().firstname.equal("Joe").Or().firstname.equal("Bob");
    int64_t m = q1.salary.minimum();
    CHECK_EQUAL(1, m);
}




TEST(TestQueryFloat4)
{
    FloatTable3 t;

    t.add(numeric_limits<float>::max(), numeric_limits<double>::max(), 11111);
    t.add(numeric_limits<float>::infinity(), numeric_limits<double>::infinity(), 11111);
    t.add(12345.0, 12345.0, 11111);

    FloatTable3::Query q1 = t.where();
    float a1 = q1.col_float.maximum();
    double a2 = q1.col_double.maximum();
    CHECK_EQUAL(numeric_limits<float>::infinity(), a1);
    CHECK_EQUAL(numeric_limits<double>::infinity(), a2);


    FloatTable3::Query q2 = t.where();
    float a3 = q1.col_float.minimum();
    double a4 = q1.col_double.minimum();
    CHECK_EQUAL(12345.0, a3);
    CHECK_EQUAL(12345.0, a4);
}

TEST(TestQueryFloat)
{
    FloatTable t;

    t.add(1.10f, 2.20);
    t.add(1.13f, 2.21);
    t.add(1.13f, 2.22);
    t.add(1.10f, 2.20);
    t.add(1.20f, 3.20);

    // Test find_all()
    FloatTable::View v = t.where().col_float.equal(1.13f).find_all();
    CHECK_EQUAL(2, v.size());
    CHECK_EQUAL(1.13f, v[0].col_float.get());
    CHECK_EQUAL(1.13f, v[1].col_float.get());

    FloatTable::View v2 = t.where().col_double.equal(3.2).find_all();
    CHECK_EQUAL(1, v2.size());
    CHECK_EQUAL(3.2, v2[0].col_double.get());

    // Test operators (and count)
    CHECK_EQUAL(2, t.where().col_float.equal(1.13f).count());
    CHECK_EQUAL(3, t.where().col_float.not_equal(1.13f).count());
    CHECK_EQUAL(3, t.where().col_float.greater(1.1f).count());
    CHECK_EQUAL(3, t.where().col_float.greater_equal(1.13f).count());
    CHECK_EQUAL(4, t.where().col_float.less_equal(1.13f).count());
    CHECK_EQUAL(2, t.where().col_float.less(1.13f).count());
    CHECK_EQUAL(3, t.where().col_float.between(1.13f, 1.2f).count());

    CHECK_EQUAL(2, t.where().col_double.equal(2.20).count());
    CHECK_EQUAL(3, t.where().col_double.not_equal(2.20).count());
    CHECK_EQUAL(2, t.where().col_double.greater(2.21).count());
    CHECK_EQUAL(3, t.where().col_double.greater_equal(2.21).count());
    CHECK_EQUAL(4, t.where().col_double.less_equal(2.22).count());
    CHECK_EQUAL(3, t.where().col_double.less(2.22).count());
    CHECK_EQUAL(4, t.where().col_double.between(2.20, 2.22).count());

    // ------ Test sum()
    // ... NO conditions
    double sum1_d = 2.20 + 2.21 + 2.22 + 2.20 + 3.20;
    CHECK_EQUAL(sum1_d, t.where().col_double.sum());

    // Note: sum of float is calculated by having a double aggregate to where each float is added
    // (thereby getting casted to double).
    double sum1_f = double(1.10f) + double(1.13f) + double(1.13f) + double(1.10f) + double(1.20f);
    double res = t.where().col_float.sum();
    CHECK_EQUAL(sum1_f, res);

    // ... with conditions
    double sum2_f = double(1.13f) + double(1.20f);
    double sum2_d = 2.21 + 3.20;
    FloatTable::Query q2 = t.where().col_float.between(1.13f, 1.20f).col_double.not_equal(2.22);
    CHECK_EQUAL(sum2_d, q2.col_double.sum());
    CHECK_EQUAL(sum2_f, q2.col_float.sum());

    // ------ Test average()

    // ... NO conditions
    CHECK_EQUAL(sum1_f/5, t.where().col_float.average());
    CHECK_EQUAL(sum1_d/5, t.where().col_double.average());
    // ... with conditions
    CHECK_EQUAL(sum2_f/2, q2.col_float.average());
    CHECK_EQUAL(sum2_d/2, q2.col_double.average());

    // -------- Test minimum(), maximum()

    // ... NO conditions
    CHECK_EQUAL(1.20f, t.where().col_float.maximum());
    CHECK_EQUAL(1.10f, t.where().col_float.minimum());
    CHECK_EQUAL(3.20, t.where().col_double.maximum());
    CHECK_EQUAL(2.20, t.where().col_double.minimum());

    // ... with conditions
    CHECK_EQUAL(1.20f, q2.col_float.maximum());
    CHECK_EQUAL(1.13f, q2.col_float.minimum());
    CHECK_EQUAL(3.20, q2.col_double.maximum());
    CHECK_EQUAL(2.21, q2.col_double.minimum());

    size_t count = 0;
    // ... NO conditions
    CHECK_EQUAL(1.20f, t.where().col_float.maximum(&count));
    CHECK_EQUAL(5, count);
    CHECK_EQUAL(1.10f, t.where().col_float.minimum(&count));
    CHECK_EQUAL(5, count);
    CHECK_EQUAL(3.20, t.where().col_double.maximum(&count));
    CHECK_EQUAL(5, count);
    CHECK_EQUAL(2.20, t.where().col_double.minimum(&count));
    CHECK_EQUAL(5, count);

    // ... with conditions
    CHECK_EQUAL(1.20f, q2.col_float.maximum(&count));
    CHECK_EQUAL(2, count);
    CHECK_EQUAL(1.13f, q2.col_float.minimum(&count));
    CHECK_EQUAL(2, count);
    CHECK_EQUAL(3.20, q2.col_double.maximum(&count));
    CHECK_EQUAL(2, count);
    CHECK_EQUAL(2.21, q2.col_double.minimum(&count));
    CHECK_EQUAL(2, count);

}


TEST(TestDateQuery)
{
    PeopleTable table;

    table.add("Mary",  28, false, tightdb::DateTime(2012,  1, 24), tightdb::BinaryData("bin \0\n data 1", 13));
    table.add("Frank", 56, true,  tightdb::DateTime(2008,  4, 15), tightdb::BinaryData("bin \0\n data 2", 13));
    table.add("Bob",   24, true,  tightdb::DateTime(2010, 12,  1), tightdb::BinaryData("bin \0\n data 3", 13));

    // Find people where hired year == 2012 (hour:minute:second is default initialized to 00:00:00)
    PeopleTable::View view5 = table.where().hired.greater_equal(tightdb::DateTime(2012, 1, 1).get_datetime())
                                           .hired.less(         tightdb::DateTime(2013, 1, 1).get_datetime()).find_all();
    CHECK_EQUAL(1, view5.size());
    CHECK_EQUAL("Mary", view5[0].name);
}


TEST(TestQueryStrIndexed_enum)
{
    TupleTableType ttt;

    for (size_t t = 0; t < 10; t++) {
        ttt.add(1, "a");
        ttt.add(4, "b");
        ttt.add(7, "c");
        ttt.add(10, "a");
        ttt.add(1, "b");
        ttt.add(4, "c");
    }

    ttt.optimize();

    ttt.column().second.set_index();

    int64_t s = ttt.where().second.equal("a").first.sum();
    CHECK_EQUAL(10*11, s);

    s = ttt.where().second.equal("a").first.equal(10).first.sum();
    CHECK_EQUAL(100, s);

    s = ttt.where().first.equal(10).second.equal("a").first.sum();
    CHECK_EQUAL(100, s);

    TupleTableType::View tv = ttt.where().second.equal("a").find_all();
    CHECK_EQUAL(10*2, tv.size());
}


TEST(TestQueryStrIndexed_non_enum)
{
    TupleTableType ttt;

    for (size_t t = 0; t < 10; t++) {
        ttt.add(1, "a");
        ttt.add(4, "b");
        ttt.add(7, "c");
        ttt.add(10, "a");
        ttt.add(1, "b");
        ttt.add(4, "c");
    }

    ttt.column().second.set_index();

    int64_t s = ttt.where().second.equal("a").first.sum();
    CHECK_EQUAL(10*11, s);

    s = ttt.where().second.equal("a").first.equal(10).first.sum();
    CHECK_EQUAL(100, s);

    s = ttt.where().first.equal(10).second.equal("a").first.sum();
    CHECK_EQUAL(100, s);

    TupleTableType::View tv = ttt.where().second.equal("a").find_all();
    CHECK_EQUAL(10*2, tv.size());
}

TEST(TestQueryFindAll_Contains2_2)
{
    TupleTableType ttt;

    ttt.add(0, "foo");
    ttt.add(1, "foobar");
    ttt.add(2, "hellofoobar");
    ttt.add(3, "foO");
    ttt.add(4, "foObar");
    ttt.add(5, "hellofoObar");
    ttt.add(6, "hellofo");
    ttt.add(7, "fobar");
    ttt.add(8, "oobar");

// FIXME: UTF-8 case handling is only implemented on msw for now
    TupleTableType::Query q1 = ttt.where().second.contains("foO", false);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(6, tv1.size());
    CHECK_EQUAL(0, tv1.get_source_ndx(0));
    CHECK_EQUAL(1, tv1.get_source_ndx(1));
    CHECK_EQUAL(2, tv1.get_source_ndx(2));
    CHECK_EQUAL(3, tv1.get_source_ndx(3));
    CHECK_EQUAL(4, tv1.get_source_ndx(4));
    CHECK_EQUAL(5, tv1.get_source_ndx(5));
    TupleTableType::Query q2 = ttt.where().second.contains("foO", true);
    TupleTableType::View tv2 = q2.find_all();
    CHECK_EQUAL(3, tv2.size());
    CHECK_EQUAL(3, tv2.get_source_ndx(0));
    CHECK_EQUAL(4, tv2.get_source_ndx(1));
    CHECK_EQUAL(5, tv2.get_source_ndx(2));
}
/*
TEST(TestQuery_sum_new_aggregates)
{
    // test the new ACTION_FIND_PATTERN() method in array

    OneIntTable t;
    for (size_t i = 0; i < 1000; i++) {
        t.add(1);
        t.add(2);
        t.add(4);
        t.add(6);
    }
    size_t c = t.where().first.equal(2).count();
    CHECK_EQUAL(1000, c);

    c = t.where().first.greater(2).count();
    CHECK_EQUAL(2000, c);

}
*/

TEST(TestQuery_sum_min_max_avg_foreign_col)
{
    TwoIntTable t;
    t.add(1, 10);
    t.add(2, 20);
    t.add(2, 30);
    t.add(3, 40);

    CHECK_EQUAL(50, t.where().first.equal(2).second.sum());
}


TEST(TestAggregateSingleCond)
{
    OneIntTable ttt;

    ttt.add(1);
    ttt.add(2);
    ttt.add(2);
    ttt.add(3);
    ttt.add(3);
    ttt.add(4);

    int64_t s = ttt.where().first.equal(2).first.sum();
    CHECK_EQUAL(4, s);

    s = ttt.where().first.greater(2).first.sum();
    CHECK_EQUAL(10, s);

    s = ttt.where().first.less(3).first.sum();
    CHECK_EQUAL(5, s);

    s = ttt.where().first.not_equal(3).first.sum();
    CHECK_EQUAL(9, s);
}

TEST(TestQueryFindAll_range1)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(4, "a");
    ttt.add(7, "a");
    ttt.add(10, "a");
    ttt.add(1, "a");
    ttt.add(4, "a");
    ttt.add(7, "a");
    ttt.add(10, "a");
    ttt.add(1, "a");
    ttt.add(4, "a");
    ttt.add(7, "a");
    ttt.add(10, "a");

    TupleTableType::Query q1 = ttt.where().second.equal("a");
    TupleTableType::View tv1 = q1.find_all(4, 10);
    CHECK_EQUAL(6, tv1.size());
}


TEST(TestQueryFindAll_range_or_monkey2)
{
    const size_t ROWS = 20;
    const size_t ITER = 100;

    for (size_t u = 0; u < ITER; u++)
    {
        TwoIntTable tit;
        Array a;
        size_t start = rand() % (ROWS + 1);
        size_t end = start + rand() % (ROWS + 1);

        if (end > ROWS)
            end = ROWS;

        for (size_t t = 0; t < ROWS; t++) {
            int64_t r1 = rand() % 10;
            int64_t r2 = rand() % 10;
            tit.add(r1, r2);
        }

        TwoIntTable::Query q1 = tit.where().group().first.equal(3).Or().first.equal(7).end_group().second.greater(5);
        TwoIntTable::View tv1 = q1.find_all(start, end);

        for (size_t t = start; t < end; t++) {
            if ((tit[t].first == 3 || tit[t].first == 7) && tit[t].second > 5) {
                a.add(t);
            }
        }
        size_t s1 = a.size();
        size_t s2 = tv1.size();

        CHECK_EQUAL(s1, s2);
        for (size_t t = 0; t < a.size(); t++) {
            size_t i1 = to_size_t(a.get(t));
            size_t i2 = tv1.get_source_ndx(t);
            CHECK_EQUAL(i1, i2);
        }
        a.destroy();
    }

}



TEST(TestQueryFindAll_range_or)
{
    TupleTableType ttt;

    ttt.add(1, "b");
    ttt.add(2, "a"); //// match
    ttt.add(3, "b"); //
    ttt.add(1, "a"); //// match
    ttt.add(2, "b"); //// match
    ttt.add(3, "a");
    ttt.add(1, "b");
    ttt.add(2, "a"); //// match
    ttt.add(3, "b"); //

    TupleTableType::Query q1 = ttt.where().group().first.greater(1).Or().second.equal("a").end_group().first.less(3);
    TupleTableType::View tv1 = q1.find_all(1, 8);
    CHECK_EQUAL(4, tv1.size());

    TupleTableType::View tv2 = q1.find_all(2, 8);
    CHECK_EQUAL(3, tv2.size());

    TupleTableType::View tv3 = q1.find_all(1, 7);
    CHECK_EQUAL(3, tv3.size());
}


TEST(TestQuerySimpleStr)
{
    TupleTableType ttt;

    ttt.add(1, "X");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "X");
    ttt.add(6, "X");
    TupleTableType::Query q = ttt.where().second.equal("X");
    size_t c = q.count();

    CHECK_EQUAL(4, c);
}

TEST(TestQueryDelete)
{
    TupleTableType ttt;

    ttt.add(1, "X");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "X");
    ttt.add(6, "X");

    TupleTableType::Query q = ttt.where().second.equal("X");
    size_t r = q.remove();

    CHECK_EQUAL(4, r);
    CHECK_EQUAL(2, ttt.size());
    CHECK_EQUAL(2, ttt[0].first);
    CHECK_EQUAL(4, ttt[1].first);

    // test remove of all
    ttt.clear();
    ttt.add(1, "X");
    ttt.add(2, "X");
    ttt.add(3, "X");
    TupleTableType::Query q2 = ttt.where().second.equal("X");
    r = q2.remove();
    CHECK_EQUAL(3, r);
    CHECK_EQUAL(0, ttt.size());
}

TEST(TestQueryDeleteRange)
{
    TupleTableType ttt;

    ttt.add(0, "X");
    ttt.add(1, "X");
    ttt.add(2, "X");
    ttt.add(3, "X");
    ttt.add(4, "X");
    ttt.add(5, "X");

    TupleTableType::Query q = ttt.where().second.equal("X");
    size_t r = q.remove(1, 4);

    CHECK_EQUAL(3, r);
    CHECK_EQUAL(3, ttt.size());
    CHECK_EQUAL(0, ttt[0].first);
    CHECK_EQUAL(4, ttt[1].first);
    CHECK_EQUAL(5, ttt[2].first);
}

TEST(TestQueryDeleteLimit)
{
    TupleTableType ttt;

    ttt.add(0, "X");
    ttt.add(1, "X");
    ttt.add(2, "X");
    ttt.add(3, "X");
    ttt.add(4, "X");
    ttt.add(5, "X");

    TupleTableType::Query q = ttt.where().second.equal("X");
    size_t r = q.remove(1, 4, 2);

    CHECK_EQUAL(2, r);
    CHECK_EQUAL(4, ttt.size());
    CHECK_EQUAL(0, ttt[0].first);
    CHECK_EQUAL(3, ttt[1].first);
    CHECK_EQUAL(4, ttt[2].first);
    CHECK_EQUAL(5, ttt[3].first);
}



TEST(TestQuerySimple)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");

    TupleTableType::Query q1 = ttt.where().first.equal(2);

    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(1, tv1.size());
    CHECK_EQUAL(1, tv1.get_source_ndx(0));
}

TEST(TestQuerySimpleBUGdetect)
{
    TupleTableType ttt;
    ttt.add(1, "a");
    ttt.add(2, "a");

    TupleTableType::Query q1 = ttt.where();

    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(2, tv1.size());
    CHECK_EQUAL(0, tv1.get_source_ndx(0));

    TupleTableType::View resView = tv1.column().second.find_all("Foo");

    // This previously crashed:
    // TableView resView = TableView(tv1);
    // tv1.find_all(resView, 1, "Foo");
}


TEST(TestQuerySubtable)
{
    Group group;
    TableRef table = group.get_table("test");

    // Create specification with sub-table
    Spec& s = table->get_spec();
    s.add_column(type_Int,    "first");
    s.add_column(type_String, "second");
    Spec sub = s.add_subtable_column("third");
        sub.add_column(type_Int,    "sub_first");
        sub.add_column(type_String, "sub_second");
    table->update_from_spec();

    CHECK_EQUAL(3, table->get_column_count());

    // Main table
    table->insert_int(0, 0, 111);
    table->insert_string(1, 0, "this");
    table->insert_subtable(2, 0);
    table->insert_done();

    table->insert_int(0, 1, 222);
    table->insert_string(1, 1, "is");
    table->insert_subtable(2, 1);
    table->insert_done();

    table->insert_int(0, 2, 333);
    table->insert_string(1, 2, "a test");
    table->insert_subtable(2, 2);
    table->insert_done();

    table->insert_int(0, 3, 444);
    table->insert_string(1, 3, "of queries");
    table->insert_subtable(2, 3);
    table->insert_done();


    // Sub tables
    TableRef subtable = table->get_subtable(2, 0);
    subtable->insert_int(0, 0, 11);
    subtable->insert_string(1, 0, "a");
    subtable->insert_done();

    subtable = table->get_subtable(2, 1);
    subtable->insert_int(0, 0, 22);
    subtable->insert_string(1, 0, "b");
    subtable->insert_done();
    subtable->insert_int(0, 1, 33);
    subtable->insert_string(1, 1, "c");
    subtable->insert_done();

    subtable = table->get_subtable(2, 2);
    subtable->insert_int(0, 0, 44);
    subtable->insert_string(1, 0, "d");
    subtable->insert_done();

    subtable = table->get_subtable(2, 3);
    subtable->insert_int(0, 0, 55);
    subtable->insert_string(1, 0, "e");
    subtable->insert_done();


    int64_t val50 = 50;
    int64_t val200 = 200;
    int64_t val20 = 20;
    int64_t val300 = 300;

    Query q1 = table->where();
    q1.greater(0, val200);
    q1.subtable(2);
    q1.less(0, val50);
    q1.end_subtable();
    TableView t1 = q1.find_all(0, size_t(-1));
    CHECK_EQUAL(2, t1.size());
    CHECK_EQUAL(1, t1.get_source_ndx(0));
    CHECK_EQUAL(2, t1.get_source_ndx(1));


    Query q2 = table->where();
    q2.subtable(2);
    q2.greater(0, val50);
    q2.Or();
    q2.less(0, val20);
    q2.end_subtable();
    TableView t2 = q2.find_all(0, size_t(-1));
    CHECK_EQUAL(2, t2.size());
    CHECK_EQUAL(0, t2.get_source_ndx(0));
    CHECK_EQUAL(3, t2.get_source_ndx(1));


    Query q3 = table->where();
    q3.subtable(2);
    q3.greater(0, val50);
    q3.Or();
    q3.less(0, val20);
    q3.end_subtable();
    q3.less(0, val300);
    TableView t3 = q3.find_all(0, size_t(-1));
    CHECK_EQUAL(1, t3.size());
    CHECK_EQUAL(0, t3.get_source_ndx(0));


    Query q4 = table->where();
    q4.equal(0, (int64_t)333);
    q4.Or();
    q4.subtable(2);
    q4.greater(0, val50);
    q4.Or();
    q4.less(0, val20);
    q4.end_subtable();
    TableView t4 = q4.find_all(0, size_t(-1));
    CHECK_EQUAL(3, t4.size());
    CHECK_EQUAL(0, t4.get_source_ndx(0));
    CHECK_EQUAL(2, t4.get_source_ndx(1));
    CHECK_EQUAL(3, t4.get_source_ndx(2));
}




TEST(TestQuerySort1)
{
    TupleTableType ttt;

    ttt.add(1, "a"); // 0
    ttt.add(2, "a"); // 1
    ttt.add(3, "X"); // 2
    ttt.add(1, "a"); // 3
    ttt.add(2, "a"); // 4
    ttt.add(3, "X"); // 5
    ttt.add(9, "a"); // 6
    ttt.add(8, "a"); // 7
    ttt.add(7, "X"); // 8

    // tv.get_source_ndx()  = 0, 2, 3, 5, 6, 7, 8
    // Vals         = 1, 3, 1, 3, 9, 8, 7
    // result       = 3, 0, 5, 2, 8, 7, 6

    TupleTableType::Query q = ttt.where().first.not_equal(2);
    TupleTableType::View tv = q.find_all();
    tv.column().first.sort();

    CHECK(tv.size() == 7);
    CHECK(tv[0].first == 1);
    CHECK(tv[1].first == 1);
    CHECK(tv[2].first == 3);
    CHECK(tv[3].first == 3);
    CHECK(tv[4].first == 7);
    CHECK(tv[5].first == 8);
    CHECK(tv[6].first == 9);
}



TEST(TestQuerySort_QuickSort)
{
    // Triggers QuickSort because range > len
    TupleTableType ttt;

    for (size_t t = 0; t < 1000; t++)
        ttt.add(rand() % 1100, "a"); // 0

    TupleTableType::Query q = ttt.where();
    TupleTableType::View tv = q.find_all();
    tv.column().first.sort();

    CHECK(tv.size() == 1000);
    for (size_t t = 1; t < tv.size(); t++) {
        CHECK(tv[t].first >= tv[t-1].first);
    }
}

TEST(TestQuerySort_CountSort)
{
    // Triggers CountSort because range <= len
    TupleTableType ttt;

    for (size_t t = 0; t < 1000; t++)
        ttt.add(rand() % 900, "a"); // 0

    TupleTableType::Query q = ttt.where();
    TupleTableType::View tv = q.find_all();
    tv.column().first.sort();

    CHECK(tv.size() == 1000);
    for (size_t t = 1; t < tv.size(); t++) {
        CHECK(tv[t].first >= tv[t-1].first);
    }
}


TEST(TestQuerySort_Descending)
{
    TupleTableType ttt;

    for (size_t t = 0; t < 1000; t++)
        ttt.add(rand() % 1100, "a"); // 0

    TupleTableType::Query q = ttt.where();
    TupleTableType::View tv = q.find_all();
    tv.column().first.sort(false);

    CHECK(tv.size() == 1000);
    for (size_t t = 1; t < tv.size(); t++) {
        CHECK(tv[t].first <= tv[t-1].first);
    }
}


TEST(TestQuerySort_Dates)
{
    Table table;
    table.add_column(type_DateTime, "first");

    table.insert_datetime(0, 0, 1000);
    table.insert_done();
    table.insert_datetime(0, 1, 3000);
    table.insert_done();
    table.insert_datetime(0, 2, 2000);
    table.insert_done();

    TableView tv = table.where().find_all();
    CHECK(tv.size() == 3);
    CHECK(tv.get_source_ndx(0) == 0);
    CHECK(tv.get_source_ndx(1) == 1);
    CHECK(tv.get_source_ndx(2) == 2);

    tv.sort(0);

    CHECK(tv.size() == 3);
    CHECK(tv.get_datetime(0, 0) == 1000);
    CHECK(tv.get_datetime(0, 1) == 2000);
    CHECK(tv.get_datetime(0, 2) == 3000);
}


TEST(TestQuerySort_Bools)
{
    Table table;
    table.add_column(type_Bool, "first");

    table.insert_bool(0, 0, true);
    table.insert_done();
    table.insert_bool(0, 0, false);
    table.insert_done();
    table.insert_bool(0, 0, true);
    table.insert_done();

    TableView tv = table.where().find_all();
    tv.sort(0);

    CHECK(tv.size() == 3);
    CHECK(tv.get_bool(0, 0) == false);
    CHECK(tv.get_bool(0, 1) == true);
    CHECK(tv.get_bool(0, 2) == true);
}

TEST(TestQueryThreads)
{
    TupleTableType ttt;

    // Spread query search hits in an odd way to test more edge cases
    // (thread job size is THREAD_CHUNK_SIZE = 10)
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 10; j++) {
            ttt.add(5, "a");
            ttt.add(j, "b");
            ttt.add(6, "c");
            ttt.add(6, "a");
            ttt.add(6, "b");
            ttt.add(6, "c");
            ttt.add(6, "a");
        }
    }
    TupleTableType::Query q1 = ttt.where().first.equal(2).second.equal("b");

    // Note, set THREAD_CHUNK_SIZE to 1.000.000 or more for performance
    //q1.set_threads(5);
    TupleTableType::View tv = q1.find_all();

    CHECK_EQUAL(100, tv.size());
    for (int i = 0; i < 100; i++) {
        const size_t expected = i*7*10 + 14 + 1;
        const size_t actual   = tv.get_source_ndx(i);
        CHECK_EQUAL(expected, actual);
    }
}


TEST(TestQueryLongString)
{
    TupleTableType ttt;

    // Spread query search hits in an odd way to test more edge cases
    // (thread job size is THREAD_CHUNK_SIZE = 10)
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 10; j++) {
            ttt.add(5, "aaaaaaaaaaaaaaaaaa");
            ttt.add(j, "bbbbbbbbbbbbbbbbbb");
            ttt.add(6, "cccccccccccccccccc");
            ttt.add(6, "aaaaaaaaaaaaaaaaaa");
            ttt.add(6, "bbbbbbbbbbbbbbbbbb");
            ttt.add(6, "cccccccccccccccccc");
            ttt.add(6, "aaaaaaaaaaaaaaaaaa");
        }
    }
    TupleTableType::Query q1 = ttt.where().first.equal(2).second.equal("bbbbbbbbbbbbbbbbbb");

    // Note, set THREAD_CHUNK_SIZE to 1.000.000 or more for performance
    //q1.set_threads(5);
    TupleTableType::View tv = q1.find_all();

    CHECK_EQUAL(100, tv.size());
    for (int i = 0; i < 100; i++) {
        const size_t expected = i*7*10 + 14 + 1;
        const size_t actual   = tv.get_source_ndx(i);
        CHECK_EQUAL(expected, actual);
    }
}


TEST(TestQueryLongEnum)
{
    TupleTableType ttt;

    // Spread query search hits in an odd way to test more edge cases
    // (thread job size is THREAD_CHUNK_SIZE = 10)
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 10; j++) {
            ttt.add(5, "aaaaaaaaaaaaaaaaaa");
            ttt.add(j, "bbbbbbbbbbbbbbbbbb");
            ttt.add(6, "cccccccccccccccccc");
            ttt.add(6, "aaaaaaaaaaaaaaaaaa");
            ttt.add(6, "bbbbbbbbbbbbbbbbbb");
            ttt.add(6, "cccccccccccccccccc");
            ttt.add(6, "aaaaaaaaaaaaaaaaaa");
        }
    }
    ttt.optimize();
    TupleTableType::Query q1 = ttt.where().first.equal(2).second.not_equal("aaaaaaaaaaaaaaaaaa");

    // Note, set THREAD_CHUNK_SIZE to 1.000.000 or more for performance
    //q1.set_threads(5);
    TupleTableType::View tv = q1.find_all();

    CHECK_EQUAL(100, tv.size());
    for (int i = 0; i < 100; i++) {
        const size_t expected = i*7*10 + 14 + 1;
        const size_t actual   = tv.get_source_ndx(i);
        CHECK_EQUAL(expected, actual);
    }
}

TEST(TestQueryBigString)
{
    TupleTableType ttt;
    ttt.add(1, "a");
    size_t res1 = ttt.where().second.equal("a").find_next();
    CHECK_EQUAL(0, res1);

    ttt.add(2, "40 chars  40 chars  40 chars  40 chars  ");
    size_t res2 = ttt.where().second.equal("40 chars  40 chars  40 chars  40 chars  ").find_next();
    CHECK_EQUAL(1, res2);

    ttt.add(1, "70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ");
    size_t res3 = ttt.where().second.equal("70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  70 chars  ").find_next();
    CHECK_EQUAL(2, res3);
}

TEST(TestQuerySimple2)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");

    TupleTableType::Query q1 = ttt.where().first.equal(2);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(3, tv1.size());
    CHECK_EQUAL(1, tv1.get_source_ndx(0));
    CHECK_EQUAL(4, tv1.get_source_ndx(1));
    CHECK_EQUAL(7, tv1.get_source_ndx(2));
}

/*
TEST(TestQueryLimit)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a"); //
    ttt.add(3, "X");
    ttt.add(1, "a");
    ttt.add(2, "a"); //
    ttt.add(3, "X");
    ttt.add(1, "a");
    ttt.add(2, "a"); //
    ttt.add(3, "X");
    ttt.add(1, "a");
    ttt.add(2, "a"); //
    ttt.add(3, "X");
    ttt.add(1, "a");
    ttt.add(2, "a"); //
    ttt.add(3, "X");

    TupleTableType::Query q1 = ttt.where().first.equal(2);

    TupleTableType::View tv1 = q1.find_all(0, size_t(-1), 2);
    CHECK_EQUAL(2, tv1.size());
    CHECK_EQUAL(1, tv1.get_source_ndx(0));
    CHECK_EQUAL(4, tv1.get_source_ndx(1));

    TupleTableType::View tv2 = q1.find_all(tv1.get_source_ndx(tv1.size() - 1) + 1, size_t(-1), 2);
    CHECK_EQUAL(2, tv2.size());
    CHECK_EQUAL(7, tv2.get_source_ndx(0));
    CHECK_EQUAL(10, tv2.get_source_ndx(1));

    TupleTableType::View tv3 = q1.find_all(tv2.get_source_ndx(tv2.size() - 1) + 1, size_t(-1), 2);
    CHECK_EQUAL(1, tv3.size());
    CHECK_EQUAL(13, tv3.get_source_ndx(0));


    TupleTableType::Query q2 = ttt.where();
    TupleTableType::View tv4 = q2.find_all(0, 5, 3);
    CHECK_EQUAL(3, tv4.size());

    TupleTableType::Query q3 = ttt.where();
    TupleTableType::View tv5 = q3.find_all(0, 3, 5);
    CHECK_EQUAL(3, tv5.size());
}
*/

TEST(TestQueryFindNext)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(6, "X");
    ttt.add(7, "X");

    TupleTableType::Query q1 = ttt.where().second.equal("X").first.greater(4);

    const size_t res1 = q1.find_next();
    const size_t res2 = q1.find_next(res1 + 1);
    const size_t res3 = q1.find_next(res2 + 1);

    CHECK_EQUAL(5, res1);
    CHECK_EQUAL(6, res2);
    CHECK_EQUAL(not_found, res3); // no more matches

    // Do same searches with new query every time
    const size_t res4 = ttt.where().second.equal("X").first.greater(4).find_next();
    const size_t res5 = ttt.where().second.equal("X").first.greater(4).find_next(res1 + 1);
    const size_t res6 = ttt.where().second.equal("X").first.greater(4).find_next(res2 + 1);

    CHECK_EQUAL(5, res4);
    CHECK_EQUAL(6, res5);
    CHECK_EQUAL(not_found, res6); // no more matches
}

TEST(TestQueryFindNext2)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(6, "X");
    ttt.add(7, "X"); // match

    TupleTableType::Query q1 = ttt.where().second.equal("X").first.greater(4);

    const size_t res1 = q1.find_next(6);
    CHECK_EQUAL(6, res1);
}

TEST(TestQueryFindAll1)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(6, "X");
    ttt.add(7, "X");

    TupleTableType::Query q1 = ttt.where().second.equal("a").first.greater(2).first.not_equal(4);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(4, tv1.get_source_ndx(0));

    TupleTableType::Query q2 = ttt.where().second.equal("X").first.greater(4);
    TupleTableType::View tv2 = q2.find_all();
    CHECK_EQUAL(5, tv2.get_source_ndx(0));
    CHECK_EQUAL(6, tv2.get_source_ndx(1));

}

TEST(TestQueryFindAll2)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(11, "X");
    ttt.add(0, "X");

    TupleTableType::Query q2 = ttt.where().second.not_equal("a").first.less(3);
    TupleTableType::View tv2 = q2.find_all();
    CHECK_EQUAL(6, tv2.get_source_ndx(0));
}

TEST(TestQueryFindAllBetween)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(11, "X");
    ttt.add(3, "X");

    TupleTableType::Query q2 = ttt.where().first.between(3, 5);
    TupleTableType::View tv2 = q2.find_all();
    CHECK_EQUAL(2, tv2.get_source_ndx(0));
    CHECK_EQUAL(3, tv2.get_source_ndx(1));
    CHECK_EQUAL(4, tv2.get_source_ndx(2));
    CHECK_EQUAL(6, tv2.get_source_ndx(3));
}


TEST(TestQueryFindAll_Range)
{
    TupleTableType ttt;

    ttt.add(5, "a");
    ttt.add(5, "a");
    ttt.add(5, "a");

    TupleTableType::Query q1 = ttt.where().second.equal("a").first.greater(2).first.not_equal(4);
    TupleTableType::View tv1 = q1.find_all(1, 2);
    CHECK_EQUAL(1, tv1.get_source_ndx(0));
}


TEST(TestQueryFindAll_Or)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(6, "a");
    ttt.add(7, "X");

    // first == 5 || second == X
    TupleTableType::Query q1 = ttt.where().first.equal(5).Or().second.equal("X");
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(3, tv1.size());
    CHECK_EQUAL(2, tv1.get_source_ndx(0));
    CHECK_EQUAL(4, tv1.get_source_ndx(1));
    CHECK_EQUAL(6, tv1.get_source_ndx(2));
}


TEST(TestQueryFindAll_Parans1)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(11, "X");

    // first > 3 && (second == X)
    TupleTableType::Query q1 = ttt.where().first.greater(3).group().second.equal("X").end_group();
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(1, tv1.size());
    CHECK_EQUAL(6, tv1.get_source_ndx(0));
}


TEST(TestQueryFindAll_OrParan)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X"); //
    ttt.add(4, "a");
    ttt.add(5, "a"); //
    ttt.add(6, "a");
    ttt.add(7, "X"); //
    ttt.add(2, "X");

    // (first == 5 || second == X && first > 2)
    TupleTableType::Query q1 = ttt.where().group().first.equal(5).Or().second.equal("X").first.greater(2).end_group();
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(3, tv1.size());
    CHECK_EQUAL(2, tv1.get_source_ndx(0));
    CHECK_EQUAL(4, tv1.get_source_ndx(1));
    CHECK_EQUAL(6, tv1.get_source_ndx(2));
}


TEST(TestQueryFindAll_OrNested0)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(11, "X");
    ttt.add(8, "Y");

    // first > 3 && (first == 5 || second == X)
    TupleTableType::Query q1 = ttt.where().first.greater(3).group().first.equal(5).Or().second.equal("X").end_group();
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(2, tv1.size());
    CHECK_EQUAL(5, tv1.get_source_ndx(0));
    CHECK_EQUAL(6, tv1.get_source_ndx(1));
}

TEST(TestQueryFindAll_OrNested)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(11, "X");
    ttt.add(8, "Y");

    // first > 3 && (first == 5 || (second == X || second == Y))
    TupleTableType::Query q1 = ttt.where().first.greater(3).group().first.equal(5).Or().group().second.equal("X").Or().second.equal("Y").end_group().end_group();
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(5, tv1.get_source_ndx(0));
    CHECK_EQUAL(6, tv1.get_source_ndx(1));
    CHECK_EQUAL(7, tv1.get_source_ndx(2));
}

TEST(TestQueryFindAll_OrPHP)
{
    TupleTableType ttt;

    ttt.add(1, "Joe");
    ttt.add(2, "Sara");
    ttt.add(3, "Jim");

    // (second == Jim || second == Joe) && first = 1
    TupleTableType::Query q1 = ttt.where().group().second.equal("Jim").Or().second.equal("Joe").end_group().first.equal(1);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(0, tv1.get_source_ndx(0));
}

TEST(TestQueryFindAllOr)
{
    TupleTableType ttt;

    ttt.add(1, "Joe");
    ttt.add(2, "Sara");
    ttt.add(3, "Jim");

    // (second == Jim || second == Joe) && first = 1
    TupleTableType::Query q1 = ttt.where().group().second.equal("Jim").Or().second.equal("Joe").end_group().first.equal(3);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(2, tv1.get_source_ndx(0));
}





TEST(TestQueryFindAll_Parans2)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(11, "X");

    // ()((first > 3()) && (()))
    TupleTableType::Query q1 = ttt.where().group().end_group().group().group().first.greater(3).group().end_group().end_group().group().group().end_group().end_group().end_group();
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(3, tv1.size());
    CHECK_EQUAL(4, tv1.get_source_ndx(0));
    CHECK_EQUAL(5, tv1.get_source_ndx(1));
    CHECK_EQUAL(6, tv1.get_source_ndx(2));
}

TEST(TestQueryFindAll_Parans4)
{
    TupleTableType ttt;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");
    ttt.add(3, "X");
    ttt.add(4, "a");
    ttt.add(5, "a");
    ttt.add(11, "X");

    // ()
    TupleTableType::Query q1 = ttt.where().group().end_group();
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(7, tv1.size());
}


TEST(TestQueryFindAll_Bool)
{
    BoolTupleTable btt;

    btt.add(1, true);
    btt.add(2, false);
    btt.add(3, true);
    btt.add(3, false);

    BoolTupleTable::Query q1 = btt.where().second.equal(true);
    BoolTupleTable::View tv1 = q1.find_all();
    CHECK_EQUAL(0, tv1.get_source_ndx(0));
    CHECK_EQUAL(2, tv1.get_source_ndx(1));

    BoolTupleTable::Query q2 = btt.where().second.equal(false);
    BoolTupleTable::View tv2 = q2.find_all();
    CHECK_EQUAL(1, tv2.get_source_ndx(0));
    CHECK_EQUAL(3, tv2.get_source_ndx(1));
}

TEST(TestQueryFindAll_Begins)
{
    TupleTableType ttt;

    ttt.add(0, "fo");
    ttt.add(0, "foo");
    ttt.add(0, "foobar");

    TupleTableType::Query q1 = ttt.where().second.begins_with("foo");
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(2, tv1.size());
    CHECK_EQUAL(1, tv1.get_source_ndx(0));
    CHECK_EQUAL(2, tv1.get_source_ndx(1));
}

TEST(TestQueryFindAll_Ends)
{
    TupleTableType ttt;

    ttt.add(0, "barfo");
    ttt.add(0, "barfoo");
    ttt.add(0, "barfoobar");

    TupleTableType::Query q1 = ttt.where().second.ends_with("foo");
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(1, tv1.size());
    CHECK_EQUAL(1, tv1.get_source_ndx(0));
}


TEST(TestQueryFindAll_Contains)
{
    TupleTableType ttt;

    ttt.add(0, "foo");
    ttt.add(0, "foobar");
    ttt.add(0, "barfoo");
    ttt.add(0, "barfoobaz");
    ttt.add(0, "fo");
    ttt.add(0, "fobar");
    ttt.add(0, "barfo");

    TupleTableType::Query q1 = ttt.where().second.contains("foo");
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(4, tv1.size());
    CHECK_EQUAL(0, tv1.get_source_ndx(0));
    CHECK_EQUAL(1, tv1.get_source_ndx(1));
    CHECK_EQUAL(2, tv1.get_source_ndx(2));
    CHECK_EQUAL(3, tv1.get_source_ndx(3));
}

TEST(TestQuery_Binary)
{
    TupleTableTypeBin t;

    const char bin[64] = {
        6, 3, 9, 5, 9, 7, 6, 3, 2, 6, 0, 0, 5, 4, 2, 4,
        5, 7, 9, 5, 7, 1, 1, 2, 0, 8, 3, 8, 0, 9, 6, 8,
        4, 7, 3, 4, 9, 5, 2, 3, 6, 2, 7, 4, 0, 3, 7, 6,
        2, 3, 5, 9, 3, 1, 2, 1, 0, 5, 5, 2, 9, 4, 5, 9
    };

    const char bin_2[4] = { 6, 6, 6, 6 }; // Not occuring above

    t.add(0, BinaryData(bin +  0, 16));
    t.add(0, BinaryData(bin +  0, 32));
    t.add(0, BinaryData(bin +  0, 48));
    t.add(0, BinaryData(bin +  0, 64));
    t.add(0, BinaryData(bin + 16, 48));
    t.add(0, BinaryData(bin + 32, 32));
    t.add(0, BinaryData(bin + 48, 16));
    t.add(0, BinaryData(bin + 24, 16)); // The "odd ball"
    t.add(0, BinaryData(bin +  0, 32)); // Repeat an entry

    CHECK_EQUAL(0, t.where().second.equal(BinaryData(bin + 16, 16)).count());
    CHECK_EQUAL(1, t.where().second.equal(BinaryData(bin +  0, 16)).count());
    CHECK_EQUAL(1, t.where().second.equal(BinaryData(bin + 48, 16)).count());
    CHECK_EQUAL(2, t.where().second.equal(BinaryData(bin +  0, 32)).count());

    CHECK_EQUAL(9, t.where().second.not_equal(BinaryData(bin + 16, 16)).count());
    CHECK_EQUAL(8, t.where().second.not_equal(BinaryData(bin +  0, 16)).count());

    CHECK_EQUAL(0, t.where().second.begins_with(BinaryData(bin +  8, 16)).count());
    CHECK_EQUAL(1, t.where().second.begins_with(BinaryData(bin + 16, 16)).count());
    CHECK_EQUAL(4, t.where().second.begins_with(BinaryData(bin +  0, 32)).count());
    CHECK_EQUAL(5, t.where().second.begins_with(BinaryData(bin +  0, 16)).count());
    CHECK_EQUAL(1, t.where().second.begins_with(BinaryData(bin + 48, 16)).count());
    CHECK_EQUAL(9, t.where().second.begins_with(BinaryData(bin + 0,   0)).count());

    CHECK_EQUAL(0, t.where().second.ends_with(BinaryData(bin + 40, 16)).count());
    CHECK_EQUAL(1, t.where().second.ends_with(BinaryData(bin + 32, 16)).count());
    CHECK_EQUAL(3, t.where().second.ends_with(BinaryData(bin + 32, 32)).count());
    CHECK_EQUAL(4, t.where().second.ends_with(BinaryData(bin + 48, 16)).count());
    CHECK_EQUAL(1, t.where().second.ends_with(BinaryData(bin +  0, 16)).count());
    CHECK_EQUAL(9, t.where().second.ends_with(BinaryData(bin + 64,  0)).count());

    CHECK_EQUAL(0, t.where().second.contains(BinaryData(bin_2)).count());
    CHECK_EQUAL(5, t.where().second.contains(BinaryData(bin +  0, 16)).count());
    CHECK_EQUAL(5, t.where().second.contains(BinaryData(bin + 16, 16)).count());
    CHECK_EQUAL(4, t.where().second.contains(BinaryData(bin + 24, 16)).count());
    CHECK_EQUAL(4, t.where().second.contains(BinaryData(bin + 32, 16)).count());
    CHECK_EQUAL(9, t.where().second.contains(BinaryData(bin +  0,  0)).count());

    {
        TupleTableTypeBin::View tv = t.where().second.equal(BinaryData(bin + 0, 32)).find_all();
        if (tv.size() == 2) {
            CHECK_EQUAL(1, tv.get_source_ndx(0));
            CHECK_EQUAL(8, tv.get_source_ndx(1));
        }
        else CHECK(false);
    }

    {
        TupleTableTypeBin::View tv = t.where().second.contains(BinaryData(bin + 24, 16)).find_all();
        if (tv.size() == 4) {
            CHECK_EQUAL(2, tv.get_source_ndx(0));
            CHECK_EQUAL(3, tv.get_source_ndx(1));
            CHECK_EQUAL(4, tv.get_source_ndx(2));
            CHECK_EQUAL(7, tv.get_source_ndx(3));
        }
        else CHECK(false);
    }
}


TEST(TestQueryEnums)
{
    TupleTableType table;

    for (size_t i = 0; i < 5; ++i) {
        table.add(1, "abd");
        table.add(2, "eftg");
        table.add(5, "hijkl");
        table.add(8, "mnopqr");
        table.add(9, "stuvxyz");
    }

    table.optimize();

    TupleTableType::Query q1 = table.where().second.equal("eftg");
    TupleTableType::View tv1 = q1.find_all();

    CHECK_EQUAL(5, tv1.size());
    CHECK_EQUAL(1, tv1.get_source_ndx(0));
    CHECK_EQUAL(6, tv1.get_source_ndx(1));
    CHECK_EQUAL(11, tv1.get_source_ndx(2));
    CHECK_EQUAL(16, tv1.get_source_ndx(3));
    CHECK_EQUAL(21, tv1.get_source_ndx(4));
}


#define uY  "\x0CE\x0AB"              // greek capital letter upsilon with dialytika (U+03AB)
#define uYd "\x0CE\x0A5\x0CC\x088"    // decomposed form (Y followed by two dots)
#define uy  "\x0CF\x08B"              // greek small letter upsilon with dialytika (U+03AB)
#define uyd "\x0cf\x085\x0CC\x088"    // decomposed form (Y followed by two dots)

#define uA  "\x0c3\x085"         // danish capital A with ring above (as in BLAABAERGROED)
#define uAd "\x041\x0cc\x08a"    // decomposed form (A (41) followed by ring)
#define ua  "\x0c3\x0a5"         // danish lower case a with ring above (as in blaabaergroed)
#define uad "\x061\x0cc\x08a"    // decomposed form (a (41) followed by ring)

TEST(TestQueryCaseSensitivity)
{
    TupleTableType ttt;

    ttt.add(1, "BLAAbaergroed");
    ttt.add(1, "BLAAbaergroedandMORE");
    ttt.add(1, "BLAAbaergroed2");

    TupleTableType::Query q1 = ttt.where().second.equal("blaabaerGROED", false);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(1, tv1.size());
    CHECK_EQUAL(0, tv1.get_source_ndx(0));
}

#if (defined(_WIN32) || defined(__WIN32__) || defined(_WIN64))

TEST(TestQueryUnicode2)
{
    TupleTableType ttt;

    ttt.add(1, uY);
    ttt.add(1, uYd);
    ttt.add(1, uy);
    ttt.add(1, uyd);

    TupleTableType::Query q1 = ttt.where().second.equal(uY, false);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(2, tv1.size());
    CHECK_EQUAL(0, tv1.get_source_ndx(0));
    CHECK_EQUAL(2, tv1.get_source_ndx(1));

    TupleTableType::Query q2 = ttt.where().second.equal(uYd, false);
    TupleTableType::View tv2 = q2.find_all();
    CHECK_EQUAL(2, tv2.size());
    CHECK_EQUAL(1, tv2.get_source_ndx(0));
    CHECK_EQUAL(3, tv2.get_source_ndx(1));

    TupleTableType::Query q3 = ttt.where().second.equal(uYd, true);
    TupleTableType::View tv3 = q3.find_all();
    CHECK_EQUAL(1, tv3.size());
    CHECK_EQUAL(1, tv3.get_source_ndx(0));
}

TEST(TestQueryUnicode3)
{
    TupleTableType ttt;

    ttt.add(1, uA);
    ttt.add(1, uAd);
    ttt.add(1, ua);
    ttt.add(1, uad);

    TupleTableType::Query q1 = ttt.where().second.equal(uA, false);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(2, tv1.size());
    CHECK_EQUAL(0, tv1.get_source_ndx(0));
    CHECK_EQUAL(2, tv1.get_source_ndx(1));

    TupleTableType::Query q2 = ttt.where().second.equal(ua, false);
    TupleTableType::View tv2 = q2.find_all();
    CHECK_EQUAL(2, tv2.size());
    CHECK_EQUAL(0, tv2.get_source_ndx(0));
    CHECK_EQUAL(2, tv2.get_source_ndx(1));


    TupleTableType::Query q3 = ttt.where().second.equal(uad, false);
    TupleTableType::View tv3 = q3.find_all();
    CHECK_EQUAL(2, tv3.size());
    CHECK_EQUAL(1, tv3.get_source_ndx(0));
    CHECK_EQUAL(3, tv3.get_source_ndx(1));

    TupleTableType::Query q4 = ttt.where().second.equal(uad, true);
    TupleTableType::View tv4 = q4.find_all();
    CHECK_EQUAL(1, tv4.size());
    CHECK_EQUAL(3, tv4.get_source_ndx(0));
}

#endif

TEST(TestQueryFindAll_BeginsUNICODE)
{
    TupleTableType ttt;

    ttt.add(0, uad "fo");
    ttt.add(0, uad "foo");
    ttt.add(0, uad "foobar");

    TupleTableType::Query q1 = ttt.where().second.begins_with(uad "foo");
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(2, tv1.size());
    CHECK_EQUAL(1, tv1.get_source_ndx(0));
    CHECK_EQUAL(2, tv1.get_source_ndx(1));
}


TEST(TestQueryFindAll_EndsUNICODE)
{
    TupleTableType ttt;

    ttt.add(0, "barfo");
    ttt.add(0, "barfoo" uad);
    ttt.add(0, "barfoobar");

    TupleTableType::Query q1 = ttt.where().second.ends_with("foo" uad);
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(1, tv1.size());
    CHECK_EQUAL(1, tv1.get_source_ndx(0));

    TupleTableType::Query q2 = ttt.where().second.ends_with("foo" uAd, false);
    TupleTableType::View tv2 = q2.find_all();
    CHECK_EQUAL(1, tv2.size());
    CHECK_EQUAL(1, tv2.get_source_ndx(0));
}


TEST(TestQueryFindAll_ContainsUNICODE)
{
    TupleTableType ttt;

    ttt.add(0, uad "foo");
    ttt.add(0, uad "foobar");
    ttt.add(0, "bar" uad "foo");
    ttt.add(0, uad "bar" uad "foobaz");
    ttt.add(0, uad "fo");
    ttt.add(0, uad "fobar");
    ttt.add(0, uad "barfo");

    TupleTableType::Query q1 = ttt.where().second.contains(uad "foo");
    TupleTableType::View tv1 = q1.find_all();
    CHECK_EQUAL(4, tv1.size());
    CHECK_EQUAL(0, tv1.get_source_ndx(0));
    CHECK_EQUAL(1, tv1.get_source_ndx(1));
    CHECK_EQUAL(2, tv1.get_source_ndx(2));
    CHECK_EQUAL(3, tv1.get_source_ndx(3));

    TupleTableType::Query q2 = ttt.where().second.contains(uAd "foo", false);
    TupleTableType::View tv2 = q1.find_all();
    CHECK_EQUAL(4, tv2.size());
    CHECK_EQUAL(0, tv2.get_source_ndx(0));
    CHECK_EQUAL(1, tv2.get_source_ndx(1));
    CHECK_EQUAL(2, tv2.get_source_ndx(2));
    CHECK_EQUAL(3, tv2.get_source_ndx(3));
}

TEST(TestQuerySyntaxCheck)
{
    TupleTableType ttt;
    string s;

    ttt.add(1, "a");
    ttt.add(2, "a");
    ttt.add(3, "X");

    TupleTableType::Query q1 = ttt.where().first.equal(2).end_group();
#ifdef TIGHTDB_DEBUG
    s = q1.Verify();
    CHECK(s != "");
#endif

    TupleTableType::Query q2 = ttt.where().group().group().first.equal(2).end_group();
#ifdef TIGHTDB_DEBUG
    s = q2.Verify();
    CHECK(s != "");
#endif

    TupleTableType::Query q3 = ttt.where().first.equal(2).Or();
#ifdef TIGHTDB_DEBUG
    s = q3.Verify();
    CHECK(s != "");
#endif

    TupleTableType::Query q4 = ttt.where().Or().first.equal(2);
#ifdef TIGHTDB_DEBUG
    s = q4.Verify();
    CHECK(s != "");
#endif

    TupleTableType::Query q5 = ttt.where().first.equal(2);
#ifdef TIGHTDB_DEBUG
    s = q5.Verify();
    CHECK(s == "");
#endif

    TupleTableType::Query q6 = ttt.where().group().first.equal(2);
#ifdef TIGHTDB_DEBUG
    s = q6.Verify();
    CHECK(s != "");
#endif

// FIXME: Work is currently underway to fully support locale
// indenepdent case folding as defined by Unicode. Reenable this test
// when is becomes available.
/*
    TupleTableType::Query q7 = ttt.where().second.equal("\xa0", false);
#ifdef TIGHTDB_DEBUG
    s = q7.Verify();
    CHECK(s != "");
#endif
*/
}

TEST(TestTV)
{
    TupleTableType t;
    t.add(1, "a");
    t.add(2, "a");
    t.add(3, "c");

    TupleTableType::View v = t.where().first.greater(1).find_all();

    TupleTableType::Query q1 = t.where().tableview(v);
    CHECK_EQUAL(2, q1.count());

    TupleTableType::Query q3 = t.where().tableview(v).second.equal("a");
    CHECK_EQUAL(1, q3.count());

    TupleTableType::Query q4 = t.where().tableview(v).first.between(3,6);
    CHECK_EQUAL(1, q4.count());
}

TEST(TestQuery_sum_min_max_avg)
{
    TupleTableType t;
    t.add(1, "a");
    t.add(2, "b");
    t.add(3, "c");

    CHECK_EQUAL(6, t.where().first.sum());
    CHECK_EQUAL(1, t.where().first.minimum());
    CHECK_EQUAL(3, t.where().first.maximum());
    CHECK_EQUAL(2, t.where().first.average());

    size_t cnt;
    CHECK_EQUAL(0, t.where().first.sum(&cnt, 0, 0));
    CHECK_EQUAL(0, cnt);
    CHECK_EQUAL(0, t.where().first.sum(&cnt, 1, 1));
    CHECK_EQUAL(0, cnt);
    CHECK_EQUAL(0, t.where().first.sum(&cnt, 2, 2));
    CHECK_EQUAL(0, cnt);

    CHECK_EQUAL(1, t.where().first.sum(&cnt, 0, 1));
    CHECK_EQUAL(1, cnt);
    CHECK_EQUAL(2, t.where().first.sum(&cnt, 1, 2));
    CHECK_EQUAL(1, cnt);
    CHECK_EQUAL(3, t.where().first.sum(&cnt, 2, 3));
    CHECK_EQUAL(1, cnt);

    CHECK_EQUAL(3, t.where().first.sum(&cnt, 0, 2));
    CHECK_EQUAL(2, cnt);
    CHECK_EQUAL(5, t.where().first.sum(&cnt, 1, 3));
    CHECK_EQUAL(2, cnt);

    CHECK_EQUAL(6, t.where().first.sum(&cnt, 0, 3));
    CHECK_EQUAL(3, cnt);
    CHECK_EQUAL(6, t.where().first.sum(&cnt, 0, size_t(-1)));
    CHECK_EQUAL(3, cnt);
}

TEST(TestQuery_avg)
{
    TupleTableType t;
    size_t cnt;
    t.add(10, "a");
    CHECK_EQUAL(10, t.where().first.average());
    t.add(30, "b");
    CHECK_EQUAL(20, t.where().first.average());

    CHECK_EQUAL(0, t.where().first.average(NULL, 0, 0));     // none
    CHECK_EQUAL(0, t.where().first.average(NULL, 1, 1));     // none
    CHECK_EQUAL(20,t.where().first.average(NULL, 0, 2));     // both
    CHECK_EQUAL(20,t.where().first.average(NULL, 0, -1));     // both

    CHECK_EQUAL(10,t.where().first.average(&cnt, 0, 1));     // first

    CHECK_EQUAL(30,t.where().first.sum(NULL, 1, 2));     // second
    CHECK_EQUAL(30,t.where().first.average(NULL, 1, 2));     // second
}

TEST(TestQuery_avg2)
{
    TupleTableType t;
    size_t cnt;

    t.add(10, "a");
    t.add(100, "b");
    t.add(20, "a");
    t.add(100, "b");
    t.add(100, "b");
    t.add(30, "a");
    TupleTableType::Query q = t.where().second.equal("a");
    CHECK_EQUAL(3, q.count());
    q.first.sum();

    CHECK_EQUAL(60, t.where().second.equal("a").first.sum());

    CHECK_EQUAL(0, t.where().second.equal("a").first.average(&cnt, 0, 0));
    CHECK_EQUAL(0, t.where().second.equal("a").first.average(&cnt, 1, 1));
    CHECK_EQUAL(0, t.where().second.equal("a").first.average(&cnt, 2, 2));
    CHECK_EQUAL(0, cnt);

    CHECK_EQUAL(10, t.where().second.equal("a").first.average(&cnt, 0, 1));
    CHECK_EQUAL(20, t.where().second.equal("a").first.average(&cnt, 1, 5));
    CHECK_EQUAL(30, t.where().second.equal("a").first.average(&cnt, 5, 6));
    CHECK_EQUAL(1, cnt);

    CHECK_EQUAL(15, t.where().second.equal("a").first.average(&cnt, 0, 3));
    CHECK_EQUAL(20, t.where().second.equal("a").first.average(&cnt, 2, 5));
    CHECK_EQUAL(1, cnt);

    CHECK_EQUAL(20, t.where().second.equal("a").first.average(&cnt));
    CHECK_EQUAL(3, cnt);
    CHECK_EQUAL(15, t.where().second.equal("a").first.average(&cnt, 0, 3));
    CHECK_EQUAL(2, cnt);
    CHECK_EQUAL(20, t.where().second.equal("a").first.average(&cnt, 0, size_t(-1)));
    CHECK_EQUAL(3, cnt);
}


TEST(TestQuery_OfByOne)
{
    TupleTableType t;
    for (size_t i = 0; i < TIGHTDB_MAX_LIST_SIZE * 2; ++i) {
        t.add(1, "a");
    }

    // Top
    t[0].first = 0;
    size_t res = t.where().first.equal(0).find_next();
    CHECK_EQUAL(0, res);
    t[0].first = 1; // reset

    // Before split
    t[TIGHTDB_MAX_LIST_SIZE-1].first = 0;
    res = t.where().first.equal(0).find_next();
    CHECK_EQUAL(TIGHTDB_MAX_LIST_SIZE-1, res);
    t[TIGHTDB_MAX_LIST_SIZE-1].first = 1; // reset

    // After split
    t[TIGHTDB_MAX_LIST_SIZE].first = 0;
    res = t.where().first.equal(0).find_next();
    CHECK_EQUAL(TIGHTDB_MAX_LIST_SIZE, res);
    t[TIGHTDB_MAX_LIST_SIZE].first = 1; // reset

    // Before end
    const size_t last_pos = (TIGHTDB_MAX_LIST_SIZE*2)-1;
    t[last_pos].first = 0;
    res = t.where().first.equal(0).find_next();
    CHECK_EQUAL(last_pos, res);
}

TEST(TestQuery_Const)
{
    TupleTableType t;
    t.add(10, "a");
    t.add(100, "b");
    t.add(20, "a");

    const TupleTableType& const_table = t;

    const size_t count = const_table.where().second.equal("a").count();
    CHECK_EQUAL(2, count);

    //TODO: Should not be possible
    const_table.where().second.equal("a").remove();
}

namespace {

TIGHTDB_TABLE_2(PhoneTable,
                type,   String,
                number, String)

TIGHTDB_TABLE_4(EmployeeTable,
                name,   String,
                age,    Int,
                hired,  Bool,
                phones, Subtable<PhoneTable>)

} // anonymous namespace

TEST(TestQuery_Subtables_Typed)
{
    // Create table
    EmployeeTable employees;

    // Add initial rows
    employees.add("joe", 42, false, NULL);
    employees[0].phones->add("home", "324-323-3214");
    employees[0].phones->add("work", "321-564-8678");

    employees.add("jessica", 22, true, NULL);
    employees[1].phones->add("mobile", "434-426-4646");
    employees[1].phones->add("school", "345-543-5345");

    // Do a query
    EmployeeTable::Query q = employees.where().hired.equal(true);
    EmployeeTable::View view = q.find_all();

    // Verify result
    CHECK(view.size() == 1 && view[0].name == "jessica");
}


TEST(TestQuery_AllTypes_DynamicallyTyped)
{
    Table table;
    {
        Spec& spec = table.get_spec();
        spec.add_column(type_Bool,     "boo");
        spec.add_column(type_Int,      "int");
        spec.add_column(type_Float,    "flt");
        spec.add_column(type_Double,   "dbl");
        spec.add_column(type_String,   "str");
        spec.add_column(type_Binary,   "bin");
        spec.add_column(type_DateTime, "dat");
        {
            Spec subspec = spec.add_subtable_column("tab");
            subspec.add_column(type_Int, "sub_int");
        }
        spec.add_column(type_Mixed,  "mix");
    }
    table.update_from_spec();

    const char bin[4] = { 0, 1, 2, 3 };
    BinaryData bin1(bin, sizeof bin / 2);
    BinaryData bin2(bin, sizeof bin);
    time_t time_now = time(0);
    Mixed mix_int(int64_t(1));
    Mixed mix_subtab((Mixed::subtable_tag()));

    table.add_empty_row();
    table.set_bool    (0, 0, false);
    table.set_int     (1, 0, 54);
    table.set_float   (2, 0, 0.7f);
    table.set_double  (3, 0, 0.8);
    table.set_string  (4, 0, "foo");
    table.set_binary  (5, 0, bin1);
    table.set_datetime(6, 0, 0);
    table.set_mixed   (8, 0, mix_int);

    table.add_empty_row();
    table.set_bool    (0, 1, true);
    table.set_int     (1, 1, 506);
    table.set_float   (2, 1, 7.7f);
    table.set_double  (3, 1, 8.8);
    table.set_string  (4, 1, "banach");
    table.set_binary  (5, 1, bin2);
    table.set_datetime(6, 1, time_now);
    TableRef subtab = table.get_subtable(7, 1);
    subtab->add_empty_row();
    subtab->set_int(0, 0, 100);
    table.set_mixed  (8, 1, mix_subtab);

    CHECK_EQUAL(1, table.where().equal(0, false).count());
    CHECK_EQUAL(1, table.where().equal(1, int64_t(54)).count());
    CHECK_EQUAL(1, table.where().equal(2, 0.7f).count());
    CHECK_EQUAL(1, table.where().equal(3, 0.8).count());
    CHECK_EQUAL(1, table.where().equal(4, "foo").count());
    CHECK_EQUAL(1, table.where().equal(5, bin1).count());
    CHECK_EQUAL(1, table.where().equal_datetime(6, 0).count());
//    CHECK_EQUAL(1, table.where().equal(7, subtab).count());
//    CHECK_EQUAL(1, table.where().equal(8, mix_int).count());

    Query query = table.where().equal(0, false);

    CHECK_EQUAL(54, query.minimum_int(1));
    CHECK_EQUAL(54, query.maximum_int(1));
    CHECK_EQUAL(54, query.sum_int(1));
    CHECK_EQUAL(54, query.average_int(1));

    CHECK_EQUAL(0.7f, query.minimum_float(2));
    CHECK_EQUAL(0.7f, query.maximum_float(2));
    CHECK_EQUAL(0.7f, query.sum_float(2));
    CHECK_EQUAL(0.7f, query.average_float(2));

    CHECK_EQUAL(0.8, query.minimum_double(3));
    CHECK_EQUAL(0.8, query.maximum_double(3));
    CHECK_EQUAL(0.8, query.sum_double(3));
    CHECK_EQUAL(0.8, query.average_double(3));
}


namespace {
TIGHTDB_TABLE_1(TestQuerySub,
                age,  Int)

TIGHTDB_TABLE_9(TestQueryAllTypes,
                bool_col,   Bool,
                int_col,    Int,
                float_col,  Float,
                double_col, Double,
                string_col, String,
                binary_col, Binary,
                date_col,   DateTime,
                table_col,  Subtable<TestQuerySub>,
                mixed_col,  Mixed)
}

TEST(TestQuery_AllTypes_StaticallyTyped)
{
    TestQueryAllTypes table;

    const char bin[4] = { 0, 1, 2, 3 };
    BinaryData bin1(bin, sizeof bin / 2);
    BinaryData bin2(bin, sizeof bin);
    time_t time_now = time(0);
    TestQuerySub subtab;
    subtab.add(100);
    Mixed mix_int(int64_t(1));
    Mixed mix_subtab((Mixed::subtable_tag()));

    table.add(false,  54, 0.7f, 0.8, "foo",    bin1, 0,        0,       mix_int);
    table.add(true,  506, 7.7f, 8.8, "banach", bin2, time_now, &subtab, mix_subtab);

    CHECK_EQUAL(1, table.where().bool_col.equal(false).count());
    CHECK_EQUAL(1, table.where().int_col.equal(54).count());
    CHECK_EQUAL(1, table.where().float_col.equal(0.7f).count());
    CHECK_EQUAL(1, table.where().double_col.equal(0.8).count());
    CHECK_EQUAL(1, table.where().string_col.equal("foo").count());
    CHECK_EQUAL(1, table.where().binary_col.equal(bin1).count());
    CHECK_EQUAL(1, table.where().date_col.equal(0).count());
//    CHECK_EQUAL(1, table.where().table_col.equal(subtab).count());
//    CHECK_EQUAL(1, table.where().mixed_col.equal(mix_int).count());

    TestQueryAllTypes::Query query = table.where().bool_col.equal(false);

    CHECK_EQUAL(54, query.int_col.minimum());
    CHECK_EQUAL(54, query.int_col.maximum());
    CHECK_EQUAL(54, query.int_col.sum());
    CHECK_EQUAL(54, query.int_col.average());

    CHECK_EQUAL(0.7f, query.float_col.minimum());
    CHECK_EQUAL(0.7f, query.float_col.maximum());
    CHECK_EQUAL(0.7f, query.float_col.sum());
    CHECK_EQUAL(0.7f, query.float_col.average());

    CHECK_EQUAL(0.8, query.double_col.minimum());
    CHECK_EQUAL(0.8, query.double_col.maximum());
    CHECK_EQUAL(0.8, query.double_col.sum());
    CHECK_EQUAL(0.8, query.double_col.average());
}


TEST(Query_ref_counting)
{
    Table* t = LangBindHelper::new_table();
    t->add_column(type_Int, "myint");
    t->insert_int(0, 0, 12);
    t->insert_done();

    Query q = t->where();

    LangBindHelper::unbind_table_ref(t);

    // Now try to access Query and see that the Table is still alive
    TableView tv = q.find_all();
    CHECK_EQUAL(1, tv.size());
}

#endif // TEST_QUERY
