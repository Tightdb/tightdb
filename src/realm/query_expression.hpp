/*************************************************************************
 *
 * REALM CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] Realm Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of Realm Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to Realm Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from Realm Incorporated.
 *
 **************************************************************************/

/*
This file lets you write queries in C++ syntax like: Expression* e = (first + 1 / second >= third + 12.3);

Type conversion/promotion semantics is the same as in the C++ expressions, e.g float + int > double == float +
(float)int > double.


Grammar:
-----------------------------------------------------------------------------------------------------------------------
    Expression:         Subexpr2<T>  Compare<Cond, T>  Subexpr2<T>
                        operator! Expression

    Subexpr2<T>:        Value<T>
                        Columns<T>
                        Subexpr2<T>  Operator<Oper<T>  Subexpr2<T>
                        power(Subexpr2<T>) // power(x) = x * x, as example of unary operator

    Value<T>:           T

    Operator<Oper<T>>:  +, -, *, /

    Compare<Cond, T>:   ==, !=, >=, <=, >, <

    T:                  bool, int, int64_t, float, double, StringData


Class diagram
-----------------------------------------------------------------------------------------------------------------------
Subexpr2
    void evaluate(size_t i, ValueBase* destination)

Compare: public Subexpr2
    size_t find_first(size_t start, size_t end)     // main method that executes query

    bool m_auto_delete
    Subexpr2& m_left;                               // left expression subtree
    Subexpr2& m_right;                              // right expression subtree

Operator: public Subexpr2
    void evaluate(size_t i, ValueBase* destination)
    bool m_auto_delete
    Subexpr2& m_left;                               // left expression subtree
    Subexpr2& m_right;                              // right expression subtree

Value<T>: public Subexpr2
    void evaluate(size_t i, ValueBase* destination)
    T m_v[8];

Columns<T>: public Subexpr2
    void evaluate(size_t i, ValueBase* destination)
    SequentialGetter<T> sg;                         // class bound to a column, lets you read values in a fast way
    Table* m_table;

class ColumnAccessor<>: public Columns<double>


Call diagram:
-----------------------------------------------------------------------------------------------------------------------
Example of 'table.first > 34.6 + table.second':

size_t Compare<Greater>::find_first()-------------+
         |                                        |
         |                                        |
         |                                        |
         +--> Columns<float>::evaluate()          +--------> Operator<Plus>::evaluate()
                                                                |               |
                                               Value<float>::evaluate()    Columns<float>::evaluate()

Operator, Value and Columns have an evaluate(size_t i, ValueBase* destination) method which returns a Value<T>
containing 8 values representing table rows i...i + 7.

So Value<T> contains 8 concecutive values and all operations are based on these chunks. This is
to save overhead by virtual calls needed for evaluating a query that has been dynamically constructed at runtime.


Memory allocation:
-----------------------------------------------------------------------------------------------------------------------
Operator and Compare contain a 'bool m_auto_delete' which tell if their subtrees were created by the query system or by the
end-user. If created by query system, they are deleted upon destructed of Operator and Compare.

Value and Columns given to Operator or Compare constructors are cloned with 'new' and hence deleted unconditionally
by query system.


Caveats, notes and todos
-----------------------------------------------------------------------------------------------------------------------
    * Perhaps disallow columns from two different tables in same expression
    * The name Columns (with s) an be confusing because we also have Column (without s)
    * Memory allocation: Maybe clone Compare and Operator to get rid of m_auto_delete. However, this might become
      bloated, with non-trivial copy constructors instead of defaults
    * Hack: In compare operator overloads (==, !=, >, etc), Compare class is returned as Query class, resulting in object
      slicing. Just be aware.
    * clone() some times new's, sometimes it just returns *this. Can be confusing. Rename method or copy always.
    * We have Columns::m_table, Query::m_table and ColumnAccessorBase::m_table that point at the same thing, even with
      ColumnAccessor<> extending Columns. So m_table is redundant, but this is in order to keep class dependencies and
      entanglement low so that the design is flexible (if you perhaps later want a Columns class that is not dependent
      on ColumnAccessor)

Nulls
-----------------------------------------------------------------------------------------------------------------------
First note that at array level, nulls are distinguished between non-null in different ways:
String:
    m_data == 0 && m_size == 0

Integer, Bool, DateTime stored in ArrayIntNull:
    value == get(0) (entry 0 determins a magic value that represents nulls)

Float/double:
    null::is_null(value) which tests if value bit-matches one specific bit pattern reserved for null

The Columns class encapsulates all this into a simple class that, for any type T has
    evaluate(size_t index) that reads values from a column, taking nulls in count
    get(index)
    set(index)
    is_null(index)
    set_null(index)
*/

#ifndef REALM_QUERY_EXPRESSION_HPP
#define REALM_QUERY_EXPRESSION_HPP

#include <realm/column_type_traits.hpp>

// Normally, if a next-generation-syntax condition is supported by the old query_engine.hpp, a query_engine node is
// created because it's faster (by a factor of 5 - 10). Because many of our existing next-generation-syntax unit
// unit tests are indeed simple enough to fallback to old query_engine, query_expression gets low test coverage. Undef
// flag to get higher query_expression test coverage. This is a good idea to try out each time you develop on/modify
// query_expression.

#define REALM_OLDQUERY_FALLBACK

namespace realm {

template <class T> T minimum(T a, T b)
{
    return a < b ? a : b;
}

// FIXME, this needs to exist elsewhere
typedef int64_t             Int;
typedef bool                Bool;
typedef realm::DateTime   DateTime;
typedef float               Float;
typedef double              Double;
typedef realm::StringData String;
typedef realm::BinaryData Binary;


// Return StringData if either T or U is StringData, else return T. See description of usage in export2().
template<class T, class U> struct EitherIsString
{
    typedef T type;
};

template<class T> struct EitherIsString<T, StringData>
{
    typedef StringData type;
};

// Hack to avoid template instantiation errors. See create(). Todo, see if we can simplify only_numeric and
// EitherIsString somehow
namespace {
template<class T, class U> T only_numeric(U in)
{
    return static_cast<T>(in);
}

template<class T> int only_numeric(const StringData&)
{
    REALM_ASSERT(false);
    return 0;
}

template<class T> StringData only_string(T in)
{
    REALM_ASSERT(false);
    static_cast<void>(in);
    return StringData();
}

StringData only_string(StringData in)
{
    return in;
}

// Modify in to refer to a deep clone of the data it points to, if applicable,
// and return a pointer which must be deleted if non-NULL
template<class T> char* in_place_deep_clone(T* in)
{
    static_cast<void>(in);
    return 0;
}

char* in_place_deep_clone(StringData* in)
{
    if (in->is_null())
        return nullptr;

    char* payload = new char[in->size()];
    memcpy(payload, in->data(), in->size());
    *in = StringData(payload, in->size());
    return payload;
}

} // anonymous namespace

template<class T>struct Plus {
    T operator()(T v1, T v2) const { return v1 + v2; }
    typedef T type;
};

template<class T>struct Minus {
    T operator()(T v1, T v2) const { return v1 - v2; }
    typedef T type;
};

template<class T>struct Div {
    T operator()(T v1, T v2) const { return v1 / v2; }
    typedef T type;
};

template<class T>struct Mul {
    T operator()(T v1, T v2) const { return v1 * v2; }
    typedef T type;
};

// Unary operator
template<class T>struct Pow {
    T operator()(T v) const { return v * v; }
    typedef T type;
};

// Finds a common type for T1 and T2 according to C++ conversion/promotion in arithmetic (float + int => float, etc)
template<class T1, class T2,
    bool T1_is_int = std::numeric_limits<T1>::is_integer || std::is_same<T1, null>::value,
    bool T2_is_int = std::numeric_limits<T2>::is_integer || std::is_same<T2, null>::value,
    bool T1_is_widest = (sizeof(T1) > sizeof(T2)   ||     std::is_same<T2, null>::value    ) > struct Common;
template<class T1, class T2, bool b> struct Common<T1, T2, b, b, true > {
    typedef T1 type;
};
template<class T1, class T2, bool b> struct Common<T1, T2, b, b, false> {
    typedef T2 type;
};
template<class T1, class T2, bool b> struct Common<T1, T2, false, true , b> {
    typedef T1 type;
};
template<class T1, class T2, bool b> struct Common<T1, T2, true, false, b> {
    typedef T2 type;
};




struct ValueBase
{
    static const size_t default_size = 8;
    virtual void export_bool(ValueBase& destination) const = 0;
    virtual void export_int(ValueBase& destination) const = 0;
    virtual void export_float(ValueBase& destination) const = 0;
    virtual void export_int64_t(ValueBase& destination) const = 0;
    virtual void export_double(ValueBase& destination) const = 0;
    virtual void export_StringData(ValueBase& destination) const = 0;
    virtual void export_null(ValueBase& destination) const = 0;
    virtual void import(const ValueBase& destination) = 0;

    // If true, all values in the class come from a link of a single field in the parent table (m_table). If
    // false, then values come from successive rows of m_table (query operations are operated on in bulks for speed)
    bool from_link;

    // Number of values stored in the class.
    size_t m_values;
};

class Expression : public Query
{
public:
    Expression() { }

    virtual size_t find_first(size_t start, size_t end) const = 0;
    virtual void set_table() = 0;
    virtual const Table* get_table() = 0;
    virtual ~Expression() {}
};

class Subexpr
{
public:
    virtual ~Subexpr() {}

    // todo, think about renaming, or actualy doing deep copy
    virtual Subexpr& clone()
    {
        return *this;
    }

    // Recursively set table pointers for all Columns object in the expression tree. Used for late binding of table
    virtual void set_table() {}

    // Recursively fetch tables of columns in expression tree. Used when user first builds a stand-alone expression and
    // binds it to a Query at a later time
    virtual const Table* get_table()
    {
        return nullptr;
    }

    virtual void evaluate(size_t index, ValueBase& destination) = 0;
};

class ColumnsBase {};

template <class T> class Columns;
template <class T> class Value;
template <class T> class Subexpr2;
template <class oper, class TLeft = Subexpr, class TRight = Subexpr> class Operator;
template <class oper, class TLeft = Subexpr> class UnaryOperator;
template <class TCond, class T, class TLeft = Subexpr, class TRight = Subexpr> class Compare;
class UnaryLinkCompare;
class ColumnAccessorBase;


// Handle cases where left side is a constant (int, float, int64_t, double, StringData)
template <class L, class Cond, class R> Query create(L left, const Subexpr2<R>& right)
{
    // Purpose of below code is to intercept the creation of a condition and test if it's supported by the old
    // query_engine.hpp which is faster. If it's supported, create a query_engine.hpp node, otherwise create a
    // query_expression.hpp node.
    //
    // This method intercepts only Value <cond> Subexpr2. Interception of Subexpr2 <cond> Subexpr is elsewhere.

#ifdef REALM_OLDQUERY_FALLBACK // if not defined, then never fallback to query_engine.hpp; always use query_expression
    const Columns<R>* column = dynamic_cast<const Columns<R>*>(&right);

    if (column &&
        ((std::numeric_limits<L>::is_integer && std::numeric_limits<L>::is_integer) ||
        (std::is_same<L, double>::value && std::is_same<R, double>::value) ||
        (std::is_same<L, float>::value && std::is_same<R, float>::value) ||
        (std::is_same<L, StringData>::value && std::is_same<R, StringData>::value))
        &&
        column->m_link_map.m_tables.size() == 0) {
        const Table* t = (const_cast<Columns<R>*>(column))->get_table();
        Query q = Query(*t);

        if (std::is_same<Cond, Less>::value)
            q.greater(column->m_column, only_numeric<R>(left));
        else if (std::is_same<Cond, Greater>::value)
            q.less(column->m_column, only_numeric<R>(left));
        else if (std::is_same<Cond, Equal>::value)
            q.equal(column->m_column, left);
        else if (std::is_same<Cond, NotEqual>::value)
            q.not_equal(column->m_column, left);
        else if (std::is_same<Cond, LessEqual>::value)
            q.greater_equal(column->m_column, only_numeric<R>(left));
        else if (std::is_same<Cond, GreaterEqual>::value)
            q.less_equal(column->m_column, only_numeric<R>(left));
        else if (std::is_same<Cond, EqualIns>::value)
            q.equal(column->m_column, only_string(left), false);
        else if (std::is_same<Cond, NotEqualIns>::value)
            q.not_equal(column->m_column, only_string(left), false);
        else if (std::is_same<Cond, BeginsWith>::value)
            q.begins_with(column->m_column, only_string(left));
        else if (std::is_same<Cond, BeginsWithIns>::value)
            q.begins_with(column->m_column, only_string(left), false);
        else if (std::is_same<Cond, EndsWith>::value)
            q.ends_with(column->m_column, only_string(left));
        else if (std::is_same<Cond, EndsWithIns>::value)
            q.ends_with(column->m_column, only_string(left), false);
        else if (std::is_same<Cond, Contains>::value)
            q.contains(column->m_column, only_string(left));
        else if (std::is_same<Cond, ContainsIns>::value)
            q.contains(column->m_column, only_string(left), false);
        else {
            // query_engine.hpp does not support this Cond. Please either add support for it in query_engine.hpp or
            // fallback to using use 'return *new Compare<>' instead.
            REALM_ASSERT(false);
        }
        // Return query_engine.hpp node
        return q;
    }
    else
#endif
    {
        // If we're searching for a string, create a deep copy of the search string
        // which will be deleted by the Compare instance.
        char* compare_string = in_place_deep_clone(&left);

        // Return query_expression.hpp node
        return *new Compare<Cond, typename Common<L, R>::type>(*new Value<L>(left),
            const_cast<Subexpr2<R>&>(right).clone(), true, compare_string);
    }
}


// All overloads where left-hand-side is Subexpr2<L>:
//
// left-hand-side       operator                              right-hand-side
// Subexpr2<L>          +, -, *, /, <, >, ==, !=, <=, >=      R, Subexpr2<R>
//
// For L = R = {int, int64_t, float, double, StringData}:
template <class L, class R> class Overloads
{
    typedef typename Common<L, R>::type CommonType;
public:

    // Arithmetic, right side constant
    Operator<Plus<CommonType>>& operator + (R right)
    {
       return *new Operator<Plus<CommonType>>(static_cast<Subexpr2<L>&>(*this).clone(), *new Value<R>(right), true);
    }
    Operator<Minus<CommonType>>& operator - (R right)
    {
       return *new Operator<Minus<CommonType>> (static_cast<Subexpr2<L>&>(*this).clone(), *new Value<R>(right), true);
    }
    Operator<Mul<CommonType>>& operator * (R right)
    {
       return *new Operator<Mul<CommonType>> (static_cast<Subexpr2<L>&>(*this).clone(), *new Value<R>(right), true);
    }
    Operator<Div<CommonType>>& operator / (R right)
    {
        return *new Operator<Div<CommonType>> (static_cast<Subexpr2<L>&>(*this).clone(), *new Value<R>(right), true);
    }

    // Arithmetic, right side subexpression
    Operator<Plus<CommonType>>& operator + (const Subexpr2<R>& right)
    {
        return *new Operator<Plus<CommonType>> (static_cast<Subexpr2<L>&>(*this).clone(), const_cast<Subexpr2<R>&>(right).clone(), true);
    }
    Operator<Minus<CommonType>>& operator - (const Subexpr2<R>& right)
    {
        return *new Operator<Minus<CommonType>> (static_cast<Subexpr2<L>&>(*this).clone(), const_cast<Subexpr2<R>&>(right).clone(), true);
    }
    Operator<Mul<CommonType>>& operator * (const Subexpr2<R>& right)
    {
        return *new Operator<Mul<CommonType>> (static_cast<Subexpr2<L>&>(*this).clone(), const_cast<Subexpr2<R>&>(right).clone(), true);
    }
    Operator<Div<CommonType>>& operator / (const Subexpr2<R>& right)
    {
        return *new Operator<Div<CommonType>> (static_cast<Subexpr2<L>&>(*this).clone(), const_cast<Subexpr2<R>&>(right).clone(), true);
    }

    // Compare, right side constant
    Query operator > (R right)
    {
        return create<R, Less, L>(right, static_cast<Subexpr2<L>&>(*this));
    }
    Query operator < (R right)
    {
        return create<R, Greater, L>(right, static_cast<Subexpr2<L>&>(*this));
    }
    Query operator >= (R right)
    {
        return create<R, LessEqual, L>(right, static_cast<Subexpr2<L>&>(*this));
    }
    Query operator <= (R right)
    {
        return create<R, GreaterEqual, L>(right, static_cast<Subexpr2<L>&>(*this));
    }
    Query operator == (R right)
    {
        return create<R, Equal, L>(right, static_cast<Subexpr2<L>&>(*this));
    }
    Query operator != (R right)
    {
        return create<R, NotEqual, L>(right, static_cast<Subexpr2<L>&>(*this));
    }

    // Purpose of this method is to intercept the creation of a condition and test if it's supported by the old
    // query_engine.hpp which is faster. If it's supported, create a query_engine.hpp node, otherwise create a
    // query_expression.hpp node.
    //
    // This method intercepts Subexpr2 <cond> Subexpr2 only. Value <cond> Subexpr2 is intercepted elsewhere.
    template <class Cond> Query create2 (const Subexpr2<R>& right)
    {
#ifdef REALM_OLDQUERY_FALLBACK // if not defined, never fallback query_engine; always use query_expression
        // Test if expressions are of type Columns. Other possibilities are Value and Operator.
        const Columns<R>* left_col = dynamic_cast<const Columns<R>*>(static_cast<Subexpr2<L>*>(this));
        const Columns<R>* right_col = dynamic_cast<const Columns<R>*>(&right);

        // query_engine supports 'T-column <op> <T-column>' for T = {int64_t, float, double}, op = {<, >, ==, !=, <=, >=},
        // but only if both columns are non-nullable
        if (left_col && right_col && std::is_same<L, R>::value && !left_col->m_nullable && !right_col->m_nullable) {
            const Table* t = (const_cast<Columns<R>*>(left_col))->get_table();
            Query q = Query(*t);

            if (std::numeric_limits<L>::is_integer || std::is_same<L, DateTime>::value) {
                if (std::is_same<Cond, Less>::value)
                    q.less_int(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, Greater>::value)
                    q.greater_int(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, Equal>::value)
                    q.equal_int(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, NotEqual>::value)
                    q.not_equal_int(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, LessEqual>::value)
                    q.less_equal_int(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, GreaterEqual>::value)
                    q.greater_equal_int(left_col->m_column, right_col->m_column);
                else {
                    REALM_ASSERT(false);
                }
            }
            else if (std::is_same<L, float>::value) {
                if (std::is_same<Cond, Less>::value)
                    q.less_float(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, Greater>::value)
                    q.greater_float(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, Equal>::value)
                    q.equal_float(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, NotEqual>::value)
                    q.not_equal_float(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, LessEqual>::value)
                    q.less_equal_float(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, GreaterEqual>::value)
                    q.greater_equal_float(left_col->m_column, right_col->m_column);
                else {
                    REALM_ASSERT(false);
                }
            }
            else if (std::is_same<L, double>::value) {
                if (std::is_same<Cond, Less>::value)
                    q.less_double(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, Greater>::value)
                    q.greater_double(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, Equal>::value)
                    q.equal_double(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, NotEqual>::value)
                    q.not_equal_double(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, LessEqual>::value)
                    q.less_equal_double(left_col->m_column, right_col->m_column);
                else if (std::is_same<Cond, GreaterEqual>::value)
                    q.greater_equal_double(left_col->m_column, right_col->m_column);
                else {
                    REALM_ASSERT(false);
                }
            }
            else {
                REALM_ASSERT(false);
            }
            // Return query_engine.hpp node
            return q;
        }
        else
#endif
        {
            // Return query_expression.hpp node
            return *new Compare<Cond, typename Common<R, float>::type>
                        (static_cast<Subexpr2<L>&>(*this).clone(), const_cast<Subexpr2<R>&>(right).clone(), true);
        }
    }

    // Compare, right side subexpression
    Query operator == (const Subexpr2<R>& right)
    {
        return create2<Equal>(right);
    }
    Query operator != (const Subexpr2<R>& right)
    {
        return create2<NotEqual>(right);
    }
    Query operator > (const Subexpr2<R>& right)
    {
        return create2<Greater>(right);
    }
    Query operator < (const Subexpr2<R>& right)
    {
        return create2<Less>(right);
    }
    Query operator >= (const Subexpr2<R>& right)
    {
        return create2<GreaterEqual>(right);
    }
    Query operator <= (const Subexpr2<R>& right)
    {
        return create2<LessEqual>(right);
    }
};

// With this wrapper class we can define just 20 overloads inside Overloads<L, R> instead of 5 * 20 = 100. Todo: We can
// consider if it's simpler/better to remove this class completely and just list all 100 overloads manually anyway.
template <class T> class Subexpr2 : public Subexpr, public Overloads<T, const char*>, public Overloads<T, int>, public
Overloads<T, float>, public Overloads<T, double>, public Overloads<T, int64_t>, public Overloads<T, StringData>,
public Overloads<T, bool>, public Overloads<T, DateTime>, public Overloads<T, null>
{
public:
    virtual ~Subexpr2() {};

#define RLM_U2(t, o) using Overloads<T, t>::operator o;
#define RLM_U(o) RLM_U2(int, o) RLM_U2(float, o) RLM_U2(double, o) RLM_U2(int64_t, o) RLM_U2(StringData, o) RLM_U2(bool, o) RLM_U2(DateTime, o) RLM_U2(null, o)
    RLM_U(+) RLM_U(-) RLM_U(*) RLM_U(/ ) RLM_U(> ) RLM_U(< ) RLM_U(== ) RLM_U(!= ) RLM_U(>= ) RLM_U(<= )
};


/* 
This class is used to store N values of type T = {int64_t, bool, DateTime or StringData}, and allows an entry
to be null too. It's used by the Value class for internal storage.

To indicate nulls, we could have chosen a separate bool vector or some other bitmask construction. But for
performance, we customize indication of nulls to match the same indication that is used in the persisted database
file

Queries in query_expression.hpp execute by processing chunks of 8 rows at a time. Assume you have a column:

    price (int) = {1, 2, 3, null, 1, 6, 6, 9, 5, 2, null}

And perform a query:

    Query q = (price + 2 == 5);

query_expression.hpp will then create a NullableVector<int> = {5, 5, 5, 5, 5, 5, 5, 5} and then read values
NullableVector<int> = {1, 2, 3, null, 1, 6, 6, 9} from the column, and then perform `+` and `==` on these chunks.

See the top of this file for more information on all this.

Assume the user specifies the null constant in a query:

Query q = (price == null)

The query system will then construct a NullableVector of type `null` (NullableVector<null>). This allows compile
time optimizations for these cases.
*/

template <class T, size_t prealloc = 8> struct NullableVector
{
    typedef typename std::conditional<std::is_same<T, bool>::value 
        || std::is_same<T, int>::value, int64_t, T>::type t_storage;

    NullableVector() {};

    NullableVector& operator= (const NullableVector& other)
    {
        init(other.m_size);
        for (size_t t = 0; t < other.m_size; t++) {
            set(t, other[t]);
        }
        return *this;
    }

    NullableVector(const NullableVector<T, prealloc>& other)
    {
        other.init(m_size);
        for (size_t t = 0; t < m_size; t++) {
            other.set((*this)[t]);
        }
    }

    ~NullableVector()
    {
        dealloc();
    }

    T operator[](size_t index) const
    {
        REALM_ASSERT_3(index, <, m_size);
        return m_first[index];
    }

    inline bool is_null(size_t index) const
    {
        REALM_ASSERT((std::is_same<t_storage, int64_t>::value));
        return m_first[index] == m_null;
    }

    inline void set_null(size_t index)
    {
        REALM_ASSERT((std::is_same<t_storage, int64_t>::value));
        m_first[index] = m_null;
    }

    inline void set(size_t index, t_storage value)
    {
        REALM_ASSERT((std::is_same<t_storage, int64_t>::value));

        // If value collides with magic null value, then switch to a new unique representation for null
        if (REALM_UNLIKELY(value == m_null)) {
            // adding a prime will generate 2^64 unique values. Todo: Only works on 2's complement architecture
            uint64_t candidate = static_cast<uint64_t>(m_null) + 0xfffffffbULL;
            while (std::find(m_first, m_first + m_size, static_cast<int64_t>(candidate)) != m_first + m_size)
                candidate += 0xfffffffbULL;
            std::replace(m_first, m_first + m_size, m_null, static_cast<int64_t>(candidate));
        }
        m_first[index] = value;
    }

    void fill(T value) 
    {
        for (size_t t = 0; t < m_size; t++) {
            if (std::is_same<T, null>::value)
                set_null(t);
            else
                set(t, value);
        }
    }

    void init(size_t size)
    {
        if (size == m_size)
            return;

        dealloc();
        m_size = size;
        if (m_size > 0) {
            if (m_size > prealloc)
                m_first = reinterpret_cast<t_storage*>(new T[m_size]);
            else
                m_first = m_cache;
        }
    }

    void init(size_t size, T values)
    {
        init(size);
        fill(values);
    }

    void dealloc()
    {
        if (m_first) {
            if (m_size > prealloc)
                delete[] m_first;
            m_first = nullptr;
        }
    }

    t_storage m_cache[prealloc];
    t_storage* m_first = &m_cache[0];
    size_t m_size = 0;

    int64_t m_null = reinterpret_cast<int64_t>(&m_null); // choose magic value to represent nulls
};

// Double
template<> inline void NullableVector<double>::set(size_t index, double value)
{
    m_first[index] = value;
}

template<> inline bool NullableVector<double>::is_null(size_t index) const
{
    return null::is_null_float(m_first[index]);
}

template<> inline void NullableVector<double>::set_null(size_t index)
{
    m_first[index] = null::get_null_float<double>();
} 

// Float
template<> inline bool NullableVector<float>::is_null(size_t index) const
{
    return null::is_null_float(m_first[index]);
}

template<> inline void NullableVector<float>::set_null(size_t index)
{
    m_first[index] = null::get_null_float<float>();
}

template<> inline void NullableVector<float>::set(size_t index, float value)
{
    m_first[index] = value;
}

// Null
template<> inline void NullableVector<null>::set_null(size_t)
{
    return;
}
template<> inline bool NullableVector<null>::is_null(size_t) const
{
    return true;
}
template<> inline void NullableVector<null>::set(size_t, null)
{
}

// DateTime
template<> inline bool NullableVector<DateTime>::is_null(size_t index) const
{
    return m_first[index].get_datetime() == m_null;
}

template<> inline void NullableVector<DateTime>::set(size_t index, DateTime value)
{
    m_first[index] = value;
}

template<> inline void NullableVector<DateTime>::set_null(size_t index)
{
    m_first[index] = m_null;
}

// StringData
template<> inline void NullableVector<StringData>::set(size_t index, StringData value)
{
    m_first[index] = value;
}
template<> inline bool NullableVector<StringData>::is_null(size_t index) const
{
    return m_first[index].is_null();
}

template<> inline void NullableVector<StringData>::set_null(size_t index)
{
    m_first[index] = StringData();
}


// Stores N values of type T. Can also exchange data with other ValueBase of different types
template<class T> class Value : public ValueBase, public Subexpr2<T>
{
public:
    Value()
    {
        init(false, ValueBase::default_size, 0);
    }
    Value(T v)
    {
        init(false, ValueBase::default_size, v);
    }
    Value(bool link, size_t values)
    {
        init(link, values, 0);
    }

    Value(bool link, size_t values, T v)
    {
        init(link, values, v);
    }

    void init(bool link, size_t values, T v) {
        m_storage.init(values, v);
        ValueBase::from_link = link;
        ValueBase::m_values = values;
    }

    void init(bool link, size_t values) {
        m_storage.init(values);
        ValueBase::from_link = link;
        ValueBase::m_values = values;
    }

    void evaluate(size_t, ValueBase& destination)
    {
        destination.import(*this);
    }


    template <class TOperator> REALM_FORCEINLINE void fun(const Value* left, const Value* right)
    {
        TOperator o;
        size_t vals = minimum(left->m_values, right->m_values);

        for (size_t t = 0; t < vals; t++) {
            if (std::is_same<T, int64_t>::value && (left->m_storage.is_null(t) || right->m_storage.is_null(t))) 
                m_storage.set_null(t);
            else
                m_storage.set(t, o(left->m_storage[t], right->m_storage[t]));
            
        }
    }

    template <class TOperator> REALM_FORCEINLINE void fun(const Value* value)
    {
        TOperator o;
        for (size_t t = 0; t < value->m_values; t++) {
            if (std::is_same<T, int64_t>::value && value->m_storage.is_null(t))
                m_storage.set_null(t);
            else
                m_storage.set(t, o(value->m_storage[t]));
        }
    }


    // Below import and export methods are for type conversion between float, double, int64_t, etc.
    template<class D> REALM_FORCEINLINE void export2(ValueBase& destination) const
    {
        // export2 is also instantiated for impossible conversions like T = StringData, D = int64_t. These are never
        // performed at runtime but still result in compiler errors. We therefore introduce EitherIsString which turns
        // both T and D into StringData if just one of them are
        typedef typename EitherIsString <D, T>::type dst_t;
        typedef typename EitherIsString <T, D>::type src_t;

        Value<dst_t>& d = static_cast<Value<dst_t>&>(destination);
        d.init(ValueBase::from_link, ValueBase::m_values, 0);
        for (size_t t = 0; t < ValueBase::m_values; t++) {

            // Values from a NullableVector<bool> must be read through an int64_t*, hence this type translation stuff
            typedef typename NullableVector<src_t>::t_storage t_storage;
            t_storage* source = reinterpret_cast<t_storage*>(m_storage.m_first);

            if (m_storage.is_null(t))
                d.m_storage.set_null(t);
            else
                d.m_storage.set(t, static_cast<dst_t>(source[t]));
        }
    }

    REALM_FORCEINLINE void export_bool(ValueBase& destination) const
    {
        export2<bool>(destination);
    }

    REALM_FORCEINLINE void export_int64_t(ValueBase& destination) const
    {
        export2<int64_t>(destination);
    }

    REALM_FORCEINLINE void export_float(ValueBase& destination) const
    {
        export2<float>(destination);
    }

    REALM_FORCEINLINE void export_int(ValueBase& destination) const
    {
        export2<int>(destination);
    }

    REALM_FORCEINLINE void export_double(ValueBase& destination) const
    {
        export2<double>(destination);
    }
    REALM_FORCEINLINE void export_StringData(ValueBase& destination) const
    {
        export2<StringData>(destination);
    }
    REALM_FORCEINLINE void export_null(ValueBase& destination) const
    {
        export2<null>(destination);
    }

    REALM_FORCEINLINE void import(const ValueBase& source)
    {
        if (std::is_same<T, int>::value)
            source.export_int(*this);
        else if (std::is_same<T, bool>::value)
            source.export_bool(*this);
        else if (std::is_same<T, float>::value)
            source.export_float(*this);
        else if (std::is_same<T, double>::value)
            source.export_double(*this);
        else if (std::is_same<T, int64_t>::value || std::is_same<T, bool>::value ||  std::is_same<T, DateTime>::value)
            source.export_int64_t(*this);
        else if (std::is_same<T, StringData>::value)
            source.export_StringData(*this);
        else if (std::is_same<T, null>::value)
            source.export_null(*this);
        else
            REALM_ASSERT_DEBUG(false);
    }

    // Given a TCond (==, !=, >, <, >=, <=) and two Value<T>, return index of first match
    template <class TCond> REALM_FORCEINLINE static size_t compare(Value<T>* left, Value<T>* right)
    {
        TCond c;

        if (!left->from_link && !right->from_link) {
            // Compare values one-by-one (one value is one row; no links)
            size_t min = minimum(left->ValueBase::m_values, right->ValueBase::m_values);
            for (size_t m = 0; m < min; m++) {

                if (c(left->m_storage[m], right->m_storage[m], left->m_storage.is_null(m), right->m_storage.is_null(m)))
                    return m;
            }
        }
        else if (left->from_link && right->from_link) {
            // Many-to-many links not supported yet. Need to specify behaviour
            REALM_ASSERT_DEBUG(false);
        }
        else if (!left->from_link && right->from_link) {
            // Right values come from link. Left must come from single row. Semantics: Match if at least 1 
            // linked-to-value fulfills the condition
            REALM_ASSERT_DEBUG(left->m_values == 0 || left->m_values == ValueBase::default_size);
            for (size_t r = 0; r < right->ValueBase::m_values; r++) {
                if (c(left->m_storage[0], right->m_storage[r], left->m_storage.is_null(0), right->m_storage.is_null(r)))
                    return 0;
            }
        }
        else if (left->from_link && !right->from_link) {
            // Same as above, right left values coming from links
            REALM_ASSERT_DEBUG(right->m_values == 0 || right->m_values == ValueBase::default_size);
            for (size_t l = 0; l < left->ValueBase::m_values; l++) {
                if (c(left->m_storage[l], right->m_storage[0], left->m_storage.is_null(l), right->m_storage.is_null(0)))
                    return 0;
            }
        }

        return not_found; // no match
    }

    virtual Subexpr& clone()
    {
        Value<T>& n = *new Value<T>();
        n.m_storage = m_storage;
        return n;
    }

    NullableVector<T> m_storage;
};


// All overloads where left-hand-side is L:
//
// left-hand-side       operator                              right-hand-side
// L                    +, -, *, /, <, >, ==, !=, <=, >=      Subexpr2<R>
//
// For L = R = {int, int64_t, float, double}:
// Compare numeric values
template <class R> Query operator > (double left, const Subexpr2<R>& right) {
    return create<double, Greater, R>(left, right);
}
template <class R> Query operator > (float left, const Subexpr2<R>& right) {
    return create<float, Greater, R>(left, right);
}
template <class R> Query operator > (int left, const Subexpr2<R>& right) {
    return create<int, Greater, R>(left, right);
}
template <class R> Query operator > (int64_t left, const Subexpr2<R>& right) {
    return create<int64_t, Greater, R>(left, right);
}
template <class R> Query operator < (double left, const Subexpr2<R>& right) {
    return create<float, Less, R>(left, right);
}
template <class R> Query operator < (float left, const Subexpr2<R>& right) {
    return create<int, Less, R>(left, right);
}
template <class R> Query operator < (int left, const Subexpr2<R>& right) {
    return create<int, Less, R>(left, right);
}
template <class R> Query operator < (int64_t left, const Subexpr2<R>& right) {
    return create<int64_t, Less, R>(left, right);
}
template <class R> Query operator == (double left, const Subexpr2<R>& right) {
    return create<double, Equal, R>(left, right);
}
template <class R> Query operator == (float left, const Subexpr2<R>& right) {
    return create<float, Equal, R>(left, right);
}
template <class R> Query operator == (int left, const Subexpr2<R>& right) {
    return create<int, Equal, R>(left, right);
}
template <class R> Query operator == (int64_t left, const Subexpr2<R>& right) {
    return create<int64_t, Equal, R>(left, right);
}
template <class R> Query operator >= (double left, const Subexpr2<R>& right) {
    return create<double, GreaterEqual, R>(left, right);
}
template <class R> Query operator >= (float left, const Subexpr2<R>& right) {
    return create<float, GreaterEqual, R>(left, right);
}
template <class R> Query operator >= (int left, const Subexpr2<R>& right) {
    return create<int, GreaterEqual, R>(left, right);
}
template <class R> Query operator >= (int64_t left, const Subexpr2<R>& right) {
    return create<int64_t, GreaterEqual, R>(left, right);
}
template <class R> Query operator <= (double left, const Subexpr2<R>& right) {
    return create<double, LessEqual, R>(left, right);
}
template <class R> Query operator <= (float left, const Subexpr2<R>& right) {
    return create<float, LessEqual, R>(left, right);
}
template <class R> Query operator <= (int left, const Subexpr2<R>& right) {
    return create<int, LessEqual, R>(left, right);
}
template <class R> Query operator <= (int64_t left, const Subexpr2<R>& right) {
    return create<int64_t, LessEqual, R>(left, right);
}
template <class R> Query operator != (double left, const Subexpr2<R>& right) {
    return create<double, NotEqual, R>(left, right);
}
template <class R> Query operator != (float left, const Subexpr2<R>& right) {
    return create<float, NotEqual, R>(left, right);
}
template <class R> Query operator != (int left, const Subexpr2<R>& right) {
    return create<int, NotEqual, R>(left, right);
}
template <class R> Query operator != (int64_t left, const Subexpr2<R>& right) {
    return create<int64_t, NotEqual, R>(left, right);
}

// Arithmetic
template <class R> Operator<Plus<typename Common<R, double>::type>>& operator + (double left, const Subexpr2<R>& right) {
    return *new Operator<Plus<typename Common<R, double>::type>>(*new Value<double>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Plus<typename Common<R, float>::type>>& operator + (float left, const Subexpr2<R>& right) {
    return *new Operator<Plus<typename Common<R, float>::type>>(*new Value<float>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Plus<typename Common<R, int>::type>>& operator + (int left, const Subexpr2<R>& right) {
    return *new Operator<Plus<typename Common<R, int>::type>>(*new Value<int>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Plus<typename Common<R, int64_t>::type>>& operator + (int64_t left, const Subexpr2<R>& right) {
    return *new Operator<Plus<typename Common<R, int64_t>::type>>(*new Value<int64_t>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Minus<typename Common<R, double>::type>>& operator - (double left, const Subexpr2<R>& right) {
    return *new Operator<Minus<typename Common<R, double>::type>>(*new Value<double>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Minus<typename Common<R, float>::type>>& operator - (float left, const Subexpr2<R>& right) {
    return *new Operator<Minus<typename Common<R, float>::type>>(*new Value<float>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Minus<typename Common<R, int>::type>>& operator - (int left, const Subexpr2<R>& right) {
    return *new Operator<Minus<typename Common<R, int>::type>>(*new Value<int>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Minus<typename Common<R, int64_t>::type>>& operator - (int64_t left, const Subexpr2<R>& right) {
    return *new Operator<Minus<typename Common<R, int64_t>::type>>(*new Value<int64_t>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Mul<typename Common<R, double>::type>>& operator * (double left, const Subexpr2<R>& right) {
    return *new Operator<Mul<typename Common<R, double>::type>>(*new Value<double>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Mul<typename Common<R, float>::type>>& operator * (float left, const Subexpr2<R>& right) {
    return *new Operator<Mul<typename Common<R, float>::type>>(*new Value<float>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Mul<typename Common<R, int>::type>>& operator * (int left, const Subexpr2<R>& right) {
    return *new Operator<Mul<typename Common<R, int>::type>>(*new Value<int>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Mul<typename Common<R, int64_t>::type>>& operator * (int64_t left, const Subexpr2<R>& right) {
    return *new Operator<Mul<typename Common<R, int64_t>::type>>(*new Value<int64_t>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Div<typename Common<R, double>::type>>& operator / (double left, const Subexpr2<R>& right) {
    return *new Operator<Div<typename Common<R, double>::type>>(*new Value<double>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Div<typename Common<R, float>::type>>& operator / (float left, const Subexpr2<R>& right) {
    return *new Operator<Div<typename Common<R, float>::type>>*(new Value<float>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Div<typename Common<R, int>::type>>& operator / (int left, const Subexpr2<R>& right) {
    return *new Operator<Div<typename Common<R, int>::type>>(*new Value<int>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}
template <class R> Operator<Div<typename Common<R, int64_t>::type>>& operator / (int64_t left, const Subexpr2<R>& right) {
    return *new Operator<Div<typename Common<R, int64_t>::type>>(*new Value<int64_t>(left), const_cast<Subexpr2<R>&>(right).clone(), true);
}

// Unary operators
template <class T> UnaryOperator<Pow<T>>& power (Subexpr2<T>& left) {
    return *new UnaryOperator<Pow<T>>(left.clone(), true);
}



// Classes used for LinkMap (see below).
struct LinkMapFunction
{
    // Your consume() method is given row index of the linked-to table as argument, and you must return wether or 
    // not you want the LinkMapFunction to exit (return false) or continue (return true) harvesting the link tree
    // for the current main table row index (it will be a link tree if you have multiple type_LinkList columns
    // in a link()->link() query.
    virtual bool consume(size_t row_index) = 0;
};

struct FindNullLinks : public LinkMapFunction
{
    FindNullLinks() : m_has_link(false) {};

    virtual bool consume(size_t row_index) {
        static_cast<void>(row_index);
        m_has_link = true;
        return false; // we've found a row index, so this can't be a null-link, so exit link harvesting
    }

    bool m_has_link;
};

struct MakeLinkVector : public LinkMapFunction
{
    MakeLinkVector(std::vector<size_t>& result) : m_links(result) {}

    virtual bool consume(size_t row_index) {
        m_links.push_back(row_index);
        return true; // continue evaluation
    }
    std::vector<size_t> &m_links;
};

struct CountLinks : public LinkMapFunction
{
    bool consume(size_t) override
    {
        m_link_count++;
        return true;
    }

    size_t result() const { return m_link_count; }

    size_t m_link_count = 0;
};


/*
The LinkMap and LinkMapFunction classes are used for query conditions on links themselves (contrary to conditions on
the value payload they point at).

MapLink::map_links() takes a row index of the link column as argument and follows any link chain stated in the query
(through the link()->link() methods) until the final payload table is reached, and then applies LinkMapFunction on 
the linked-to row index(es). 

If all link columns are type_Link, then LinkMapFunction is only invoked for a single row index. If one or more 
columns are type_LinkList, then it may result in multiple row indexes.

The reason we use this map pattern is that we can exit the link-tree-traversal as early as possible, e.g. when we've
found the first link that points to row '5'. Other solutions could be a std::vector<size_t> harvest_all_links(), or an
iterator pattern. First solution can't exit, second solution requires internal state.
*/
class LinkMap
{
public:
    LinkMap() : m_table(nullptr) {};

    void init(Table* table, std::vector<size_t> columns)
    {
        for (size_t t = 0; t < columns.size(); t++) {
            // Link column can be either LinkList or single Link
            ColumnType type = table->get_real_column_type(columns[t]);
            if (type == col_type_LinkList) {
                LinkListColumn& cll = table->get_column_link_list(columns[t]);
                m_tables.push_back(table);
                m_link_columns.push_back(&(table->get_column_link_list(columns[t])));
                m_link_types.push_back(realm::type_LinkList);
                table = &cll.get_target_table();
            }
            else {
                LinkColumn& cl = table->get_column_link(columns[t]);
                m_tables.push_back(table);
                m_link_columns.push_back(&(table->get_column_link(columns[t])));
                m_link_types.push_back(realm::type_Link);
                table = &cl.get_target_table();
            }
        }
        m_table = table;
    }

    std::vector<size_t> get_links(size_t index)
    {
        std::vector<size_t> res;
        get_links(index, res);
        return res;
    }

    size_t count_links(size_t row)
    {
        CountLinks counter;
        map_links(row, counter);
        return counter.result();
    }

    void map_links(size_t row, LinkMapFunction& lm)
    {
        map_links(0, row, lm);
    }

    const Table* m_table;
    std::vector<LinkColumnBase*> m_link_columns;
    std::vector<Table*> m_tables;

private:
    void map_links(size_t column, size_t row, LinkMapFunction& lm)
    {
        bool last = (column + 1 == m_link_columns.size());
        if (m_link_types[column] == type_Link) {
            LinkColumn& cl = *static_cast<LinkColumn*>(m_link_columns[column]);
            size_t r = to_size_t(cl.get(row));
            if (r == 0)
                return;
            r--; // LinkColumn stores link to row N as N + 1
            if (last) {
                bool continue2 = lm.consume(r);
                if (!continue2)
                    return;
            }
            else
                map_links(column + 1, r, lm);
        }
        else {
            LinkListColumn& cll = *static_cast<LinkListColumn*>(m_link_columns[column]);
            LinkViewRef lvr = cll.get(row);
            for (size_t t = 0; t < lvr->size(); t++) {
                size_t r = lvr->get(t).get_index();
                if (last) {
                    bool continue2 = lm.consume(r);
                    if (!continue2)
                        return;
                }
                else
                    map_links(column + 1, r, lm);
            }
        }
    }


    void get_links(size_t row, std::vector<size_t>& result)
    {
        MakeLinkVector mlv = MakeLinkVector(result);
        map_links(row, mlv);
    }

    std::vector<realm::DataType> m_link_types;
};

template <class T, class S, class I> Query string_compare(const Columns<StringData>& left, T right, bool case_insensitive);
template <class S, class I> Query string_compare(const Columns<StringData>& left, const Columns<StringData>& right, bool case_insensitive);

// Handling of String columns. These support only == and != compare operators. No 'arithmetic' operators (+, etc).
template <> class Columns<StringData> : public Subexpr2<StringData>
{
public:
    Columns(size_t column, const Table* table, std::vector<size_t> links) : m_table_linked_from(nullptr),
                                                                            m_table(nullptr), 
                                                                            m_column(column)
    {
        m_link_map.init(const_cast<Table*>(table), links);
        m_table = table;
        REALM_ASSERT_3(m_link_map.m_table->get_column_type(column), ==, type_String);
    }

    Columns(size_t column, const Table* table) : m_table_linked_from(nullptr), m_table(nullptr), m_column(column)
    {
        m_table = table;
    }

    explicit Columns() : m_table_linked_from(nullptr), m_table(nullptr) { }


    explicit Columns(size_t column) : m_table_linked_from(nullptr), m_table(nullptr), m_column(column)
    {
    }

    virtual Subexpr& clone()
    {
        Columns<StringData>& n = *new Columns<StringData>();
        n = *this;
        return n;
    }

    virtual const Table* get_table()
    {
        return m_table;
    }

    virtual void evaluate(size_t index, ValueBase& destination)
    {
        Value<StringData>& d = static_cast<Value<StringData>&>(destination);

        if (m_link_map.m_link_columns.size() > 0) {
            std::vector<size_t> links = m_link_map.get_links(index);
            Value<StringData> v(true, links.size());
            for (size_t t = 0; t < links.size(); t++) {
                size_t link_to = links[t];
                v.m_storage.set(t, m_link_map.m_table->get_string(m_column, link_to));
            }
            destination.import(v);
        }
        else {
            // Not a link column
            for (size_t t = 0; t < destination.m_values && index + t < m_table->size(); t++) {
                d.m_storage.set(t, m_table->get_string(m_column, index + t));
            }
        }
    }

    Query equal(StringData sd, bool case_sensitive = true)
    {
        return string_compare<StringData, Equal, EqualIns>(*this, sd, case_sensitive);
    }

    Query equal(const Columns<StringData>& col, bool case_sensitive = true)
    {
        return string_compare<Equal, EqualIns>(*this, col, case_sensitive);
    }

    Query not_equal(StringData sd, bool case_sensitive = true)
    {
        return string_compare<StringData, NotEqual, NotEqualIns>(*this, sd, case_sensitive);
    }

    Query not_equal(const Columns<StringData>& col, bool case_sensitive = true)
    {
        return string_compare<NotEqual, NotEqualIns>(*this, col, case_sensitive);
    }
    
    Query begins_with(StringData sd, bool case_sensitive = true)
    {
        return string_compare<StringData, BeginsWith, BeginsWithIns>(*this, sd, case_sensitive);
    }

    Query begins_with(const Columns<StringData>& col, bool case_sensitive = true)
    {
        return string_compare<BeginsWith, BeginsWithIns>(*this, col, case_sensitive);
    }

    Query ends_with(StringData sd, bool case_sensitive = true)
    {
        return string_compare<StringData, EndsWith, EndsWithIns>(*this, sd, case_sensitive);
    }

    Query ends_with(const Columns<StringData>& col, bool case_sensitive = true)
    {
        return string_compare<EndsWith, EndsWithIns>(*this, col, case_sensitive);
    }
    
    Query contains(StringData sd, bool case_sensitive = true)
    {
        return string_compare<StringData, Contains, ContainsIns>(*this, sd, case_sensitive);
    }

    Query contains(const Columns<StringData>& col, bool case_sensitive = true)
    {
        return string_compare<Contains, ContainsIns>(*this, col, case_sensitive);
    }
    
    const Table* m_table_linked_from;

    // Pointer to payload table (which is the linked-to table if this is a link column) used for condition operator
    const Table* m_table;

    // Column index of payload column of m_table
    size_t m_column;

    LinkMap m_link_map;
};


template <class T, class S, class I> Query string_compare(const Columns<StringData>& left, T right, bool case_sensitive)
{
    StringData sd(right);
    if (case_sensitive)
        return create<StringData, S, StringData>(sd, left);
    else
        return create<StringData, I, StringData>(sd, left);
}

template <class S, class I> Query string_compare(const Columns<StringData>& left, const Columns<StringData>& right, bool case_sensitive)
{
    Subexpr& left_copy = const_cast<Columns<StringData>&>(left).clone();
    Subexpr& right_copy = const_cast<Columns<StringData>&>(right).clone();
    if (case_sensitive)
        return *new Compare<S, StringData>(right_copy, left_copy, /* auto_delete */ true);
    else
        return *new Compare<I, StringData>(right_copy, left_copy, /* auto_delete */ true);
}

// Columns<String> == Columns<String>
inline Query operator == (const Columns<StringData>& left, const Columns<StringData>& right) {
    return string_compare<Equal, EqualIns>(left, right, true);
}

// Columns<String> != Columns<String>
inline Query operator != (const Columns<StringData>& left, const Columns<StringData>& right) {
    return string_compare<NotEqual, NotEqualIns>(left, right, true);
}

// String == Columns<String>
template <class T> Query operator == (T left, const Columns<StringData>& right) {
    return operator==(right, left);
}

// String != Columns<String>
template <class T> Query operator != (T left, const Columns<StringData>& right) {
    return operator!=(right, left);
}

// Columns<String> == String
template <class T> Query operator == (const Columns<StringData>& left, T right) {
    return string_compare<T, Equal, EqualIns>(left, right, true);
}

// Columns<String> != String
template <class T> Query operator != (const Columns<StringData>& left, T right) {
    return string_compare<T, NotEqual, NotEqualIns>(left, right, true);
}

// This class is intended to perform queries on the *pointers* of links, contrary to performing queries on *payload* 
// in linked-to tables. Queries can be "find first link that points at row X" or "find first null-link". Currently
// only "find first null-link" is supported. More will be added later.
class UnaryLinkCompare : public Expression
{
public:
    UnaryLinkCompare(LinkMap lm) : m_link_map(lm)
    {
        Query::expression(this);
        Table* t = const_cast<Table*>(get_table());
        Query::set_table(t->get_table_ref());
    }

    void set_table()
    {
    }

    // Return main table of query (table on which table->where()... is invoked). Note that this is not the same as 
    // any linked-to payload tables
    virtual const Table* get_table()
    {
        return m_link_map.m_tables[0];
    }

    size_t find_first(size_t start, size_t end) const
    {
        for (; start < end;) {
            std::vector<size_t> l = m_link_map.get_links(start);
            // We have found a Link which is NULL, or LinkList with 0 entries. Return it as match.

            FindNullLinks fnl;
            m_link_map.map_links(start, fnl);
            if (!fnl.m_has_link)
                return start;
            
            start++;
        }

        return not_found;
    }

private:
    mutable LinkMap m_link_map;
};

class LinkCount : public Subexpr2<Int> {
public:
    LinkCount(LinkMap link_map) : m_link_map(link_map) { }

    Subexpr& clone() override
    {
        return *new LinkCount(*this);
    }

    const Table* get_table() override
    {
        return m_link_map.m_tables[0];
    }

    void set_table() override { }

    void evaluate(size_t index, ValueBase& destination) override
    {
        size_t count = m_link_map.count_links(index);
        destination.import(Value<Int>(false, 1, count));
    }

private:
    LinkMap m_link_map;
};

// This is for LinkList too because we have 'typedef List LinkList'
template <> class Columns<Link> : public Subexpr2<Link>
{
public:
    Query is_null() {
        if (m_link_map.m_link_columns.size() > 1)
            throw std::runtime_error("Cannot find null-links in a linked-to table (link()...is_null() not supported).");
        // Todo, it may be useful to support the above, but we would need to figure out an intuitive behaviour
        return *new UnaryLinkCompare(m_link_map);
    }

    LinkCount count() const
    {
        return LinkCount(m_link_map);
    }

private:
    Columns(size_t column, const Table* table, std::vector<size_t> links) :
        m_table(nullptr)
    {
        static_cast<void>(column);
        m_link_map.init(const_cast<Table*>(table), links);
        m_table = table;
    }

    Columns() : m_table(nullptr) { }

    explicit Columns(size_t column) : m_table(nullptr) { static_cast<void>(column); }

    Columns(size_t column, const Table* table) : m_table(nullptr)
    {
        static_cast<void>(column);
        m_table = table;
    }

    virtual Subexpr& clone()
    {
        return *this;
    }

    virtual const Table* get_table()
    {
        return m_table;
    }

    virtual void evaluate(size_t index, ValueBase& destination)
    {
        static_cast<void>(index);
        static_cast<void>(destination);
        REALM_ASSERT(false);
    }

    // m_table is redundant with ColumnAccessorBase<>::m_table, but is in order to decrease class dependency/entanglement
    const Table* m_table;

    // Column index of payload column of m_table
    size_t m_column;

    LinkMap m_link_map;
    bool auto_delete;

   friend class Table;
};


template <class T> class Columns : public Subexpr2<T>, public ColumnsBase
{
public:
    using ColType = typename ColumnTypeTraits<T, false>::column_type;
    using ColTypeN = typename ColumnTypeTraits<T, true>::column_type;

    Columns(size_t column, const Table* table, std::vector<size_t> links) : m_column(column)
    {
        m_link_map.init(const_cast<Table*>(table), links);
        m_table = table; 
        m_nullable = m_link_map.m_table->is_nullable(m_column);
    }

    Columns(size_t column, const Table* table) : m_column(column)
    {
        m_table = table;
        m_nullable = m_table->is_nullable(column);
    }


    Columns() { }

    explicit Columns(size_t column) : m_column(column) {}

    ~Columns()
    {
        delete m_sg;
    }

    template<class C> Subexpr& clone()
    {
        Columns<T>& n = *new Columns<T>();
        n = *this;
        SequentialGetter<C> *s = new SequentialGetter<C>();
        n.m_sg = s;
        n.m_nullable = m_nullable;
        return n;
    }

    virtual Subexpr& clone()
    {
        if (m_nullable)
            return clone<ColTypeN>();
        else
            return clone<ColType>();
    }

    // Recursively set table pointers for all Columns object in the expression tree. Used for late binding of table
    virtual void set_table()
    {
        const ColumnBase* c;
        if (m_link_map.m_link_columns.size() == 0) {
            m_nullable = m_table->is_nullable(m_column);
            c = &m_table->get_column_base(m_column);
        }
        else {
            m_nullable = m_link_map.m_table->is_nullable(m_column);
            c = &m_link_map.m_table->get_column_base(m_column);
        }

        if (m_sg == nullptr) {
            if (m_nullable)
                m_sg = new SequentialGetter<ColTypeN>();
            else
                m_sg = new SequentialGetter<ColType>();
        }

        if (m_nullable)
            static_cast<SequentialGetter<ColTypeN>*>(m_sg)->init(  (ColTypeN*) (c)); // todo, c cast
        else
            static_cast<SequentialGetter<ColType>*>(m_sg)->init( (ColType*)(c));
    }


    // Recursively fetch tables of columns in expression tree. Used when user first builds a stand-alone expression 
    // and binds it to a Query at a later time
    virtual const Table* get_table()
    {
        return m_table;
    }

    void evaluate(size_t index, ValueBase& destination) {
        if (m_nullable)
            evaluate<ColTypeN>(index, destination);
        else
            evaluate<ColType>(index, destination);
    }

    // Load values from Column into destination
    template<class C> void evaluate(size_t index, ValueBase& destination) {
        SequentialGetter<C>* sgc = static_cast<SequentialGetter<C>*>(m_sg);

        if (m_link_map.m_link_columns.size() > 0) {
            // LinkList with more than 0 values. Create Value with payload for all fields

            std::vector<size_t> links = m_link_map.get_links(index);
            Value<T> v(true, links.size());

            for (size_t t = 0; t < links.size(); t++) {
                size_t link_to = links[t];
                sgc->cache_next(link_to); // todo, needed?
                v.m_storage.set(t, sgc->get_next(link_to));
            }
            destination.import(v);
        }
        else {
            // Not a Link column
            // make sequential getter load the respective leaf to access data at column row 'index'
            sgc->cache_next(index); 
            size_t colsize = sgc->m_column->size();

            // Now load `ValueBase::default_size` rows from from the leaf into m_storage. If it's an integer 
            // leaf, then it contains the method get_chunk() which copies these values in a super fast way (first 
            // case of the `if` below. Otherwise, copy the values one by one in a for-loop (the `else` case).
            if (std::is_same<T, int64_t>::value && index + ValueBase::default_size <= sgc->m_leaf_end) {
                Value<T> v;

                // If you want to modify 'default_size' then update Array::get_chunk()
                REALM_ASSERT_3(ValueBase::default_size, ==, 8); 
                
                sgc->m_leaf_ptr->get_chunk(index - sgc->m_leaf_start, 
                    static_cast<Value<int64_t>*>(static_cast<ValueBase*>(&v))->m_storage.m_first);

                if (m_nullable)
                   v.m_storage.m_null = ((ArrayIntNull*)(sgc->m_leaf_ptr))->null_value();

                destination.import(v);
            }
            else          
            {
                // To make Valgrind happy we must initialize all default_size in v even if Column ends earlier. Todo,
                // benchmark if an unconditional zero out is faster
                size_t rows = colsize - index;
                if (rows > ValueBase::default_size)
                    rows = ValueBase::default_size;
                Value<T> v(false, rows);

                for (size_t t = 0; t < rows; t++)
                    v.m_storage.set(t, sgc->get_next(index + t));

                if (m_nullable && (std::is_same<T, int64_t>::value ||
                                   std::is_same<T, bool>::value ||
                                   std::is_same<T, realm::DateTime>::value)) {
                    v.m_storage.m_null = reinterpret_cast<const ArrayIntNull*>(sgc->m_leaf_ptr)->null_value();
                }

                destination.import(v);
            }
        }
    }

    const Table* m_table_linked_from = nullptr;

    // m_table is redundant with ColumnAccessorBase<>::m_table, but is in order to decrease class 
    // dependency/entanglement
    const Table* m_table = nullptr;

    // Fast (leaf caching) value getter for payload column (column in table on which query condition is executed)
    SequentialGetterBase* m_sg = nullptr;

    // Column index of payload column of m_table
    size_t m_column;

    LinkMap m_link_map;

    // set to false by default for stand-alone Columns declaration that are not yet associated with any table
    // or oclumn. Call init() to update it or use a constructor that takes table + column index as argument.
    bool m_nullable = false;
};


template <class oper, class TLeft> class UnaryOperator : public Subexpr2<typename oper::type>
{
public:
    UnaryOperator(TLeft& left, bool auto_delete = false) : m_auto_delete(auto_delete), m_left(left) {}

    ~UnaryOperator()
    {
        if (m_auto_delete)
            delete &m_left;
    }

    // Recursively set table pointers for all Columns object in the expression tree. Used for late binding of table
    void set_table()
    {
        m_left.set_table();
    }

    // Recursively fetch tables of columns in expression tree. Used when user first builds a stand-alone expression and
    // binds it to a Query at a later time
    virtual const Table* get_table()
    {
        const Table* l = m_left.get_table();
        return l;
    }

    // destination = operator(left)
    void evaluate(size_t index, ValueBase& destination)
    {
        Value<T> result;
        Value<T> left;
        m_left.evaluate(index, left);
        result.template fun<oper>(&left);
        destination.import(result);
    }

private:
    typedef typename oper::type T;
    bool m_auto_delete;
    TLeft& m_left;
};


template <class oper, class TLeft, class TRight> class Operator : public Subexpr2<typename oper::type>
{
public:

    Operator(TLeft& left, const TRight& right, bool auto_delete = false) : m_left(left), m_right(const_cast<TRight&>(right))
    {
        m_auto_delete = auto_delete;
    }

    ~Operator()
    {
        if (m_auto_delete) {
            delete &m_left;
            delete &m_right;
        }
    }

    // Recursively set table pointers for all Columns object in the expression tree. Used for late binding of table
    void set_table()
    {
        m_left.set_table();
        m_right.set_table();
    }

    // Recursively fetch tables of columns in expression tree. Used when user first builds a stand-alone expression and
    // binds it to a Query at a later time
    virtual const Table* get_table()
    {
        const Table* l = m_left.get_table();
        const Table* r = m_right.get_table();

        // Queries do not support multiple different tables; all tables must be the same.
        REALM_ASSERT(l == nullptr || r == nullptr || l == r);

        // nullptr pointer means expression which isn't yet associated with any table, or is a Value<T>
        return l ? l : r;
    }

    // destination = operator(left, right)
    void evaluate(size_t index, ValueBase& destination)
    {
        Value<T> result;
        Value<T> left;
        Value<T> right;
        m_left.evaluate(index, left);
        m_right.evaluate(index, right);
        result.template fun<oper>(&left, &right);
        destination.import(result);
    }

private:
    typedef typename oper::type T;
    bool m_auto_delete;
    TLeft& m_left;
    TRight& m_right;
};


template <class TCond, class T, class TLeft, class TRight> class Compare : public Expression
{
public:

    // Compare extends Expression which extends Query. This constructor for Compare initializes the Query part by
    // adding an ExpressionNode (see query_engine.hpp) and initializes Query::table so that it's ready to call
    // Query methods on, like find_first(), etc.
    Compare(TLeft& left, const TRight& right, bool auto_delete = false, const char* compare_string = nullptr) :
            m_left(left), m_right(const_cast<TRight&>(right)), m_compare_string(compare_string)
    {
        m_auto_delete = auto_delete;
        Query::expression(this);
        Table* t = const_cast<Table*>(get_table()); // todo, const
        if (t)
            Query::set_table(t->get_table_ref());
    }

    ~Compare()
    {
        if (m_auto_delete) {
            delete[] m_compare_string;
            delete &m_left;
            delete &m_right;
        }
    }

    // Recursively set table pointers for all Columns object in the expression tree. Used for late binding of table
    void set_table()
    {
        m_left.set_table();
        m_right.set_table();
    }

    // Recursively fetch tables of columns in expression tree. Used when user first builds a stand-alone expression and
    // binds it to a Query at a later time
    virtual const Table* get_table()
    {
        const Table* l = m_left.get_table();
        const Table* r = m_right.get_table();

        // All main tables in each subexpression of a query (table.columns() or table.link()) must be the same.
        REALM_ASSERT(l == nullptr || r == nullptr || l == r);

        // nullptr pointer means expression which isn't yet associated with any table, or is a Value<T>
        return l ? l : r;
    }

    size_t find_first(size_t start, size_t end) const
    {
        size_t match;
        Value<T> right;
        Value<T> left;

        for (; start < end;) {
            m_left.evaluate(start, left);
            m_right.evaluate(start, right);
            match = Value<T>::template compare<TCond>(&left, &right);

            if (match != not_found && match + start < end)
                return start + match;

            size_t rows = (left.from_link || right.from_link) ? 1 : minimum(right.m_values, left.m_values);
            start += rows;
        }

        return not_found; // no match
    }

private:
    bool m_auto_delete;
    TLeft& m_left;
    TRight& m_right;

    // Only used if T is StringData. It then points at the deep copied user given string (the "foo" in 
    // Query q = table2->link(col_link2).column<String>(1) == "foo") so that we can delete it when this 
    // Compare object is destructed and the copy is no longer needed. 
    const char* m_compare_string;
};

}
#endif // REALM_QUERY_EXPRESSION_HPP

