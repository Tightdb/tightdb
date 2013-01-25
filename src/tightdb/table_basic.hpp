/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of TightDB Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to TightDB Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from TightDB Incorporated.
 *
 **************************************************************************/
#ifndef TIGHTDB_TABLE_BASIC_HPP
#define TIGHTDB_TABLE_BASIC_HPP

#ifdef _MSC_VER
#include <win32/stdint.h>
#else
#include <stdint.h> // unint8_t etc
#endif

#include <cstddef>
#include <cstring> // strcmp()
#include <ctime>
#include <utility>

#include <tightdb/meta.hpp>
#include <tightdb/tuple.hpp>
#include <tightdb/table.hpp>
#include <tightdb/column.hpp>
#include <tightdb/query.hpp>
#include <tightdb/table_accessors.hpp>
#include <tightdb/table_view_basic.hpp>

namespace tightdb {


namespace _impl {
    template<class Type, int col_idx> struct AddCol;
    template<class Type, int col_idx> struct DiffColType;
    template<class Type, int col_idx> struct InsertIntoCol;
    template<class Type, int col_idx> struct AssignIntoCol;
}



/// This class is non-polymorphic, that is, it has no virtual
/// functions. Further more, it has no destructor, and it adds no new
/// data-members. These properties are important, because it ensures
/// that there is no run-time distinction between a Table instance and
/// an instance of any variation of this class, and therefore it is
/// valid to cast a pointer from Table to BasicTable<Spec> even when
/// the instance is constructed as a Table. Of couse, this also
/// assumes that Table is non-polymorphic. Further more, accessing the
/// Table via a poiter or reference to a BasicTable is not in
/// violation of the strict aliasing rule.
template<class Spec> class BasicTable: private Table, public Spec::ConvenienceMethods {
public:
    typedef Spec spec_type;
    typedef typename Spec::Columns Columns;

    typedef BasicTableRef<BasicTable> Ref;
    typedef BasicTableRef<const BasicTable> ConstRef;

    typedef BasicTableView<BasicTable> View;
    typedef BasicTableView<const BasicTable> ConstView;

    using Table::is_valid;
    using Table::has_shared_spec;
    using Table::is_empty;
    using Table::size;
    using Table::clear;
    using Table::remove;
    using Table::remove_last;
    using Table::optimize;
    using Table::lookup;
    using Table::add_empty_row;
    using Table::insert_empty_row;

    BasicTable(Allocator& alloc = GetDefaultAllocator()): Table(alloc) { set_dynamic_spec(); }

    static int get_column_count() { return TypeCount<typename Spec::Columns>::value; }

    BasicTableRef<BasicTable> get_table_ref()
    {
        return BasicTableRef<BasicTable>(this);
    }

    BasicTableRef<const BasicTable> get_table_ref() const
    {
        return BasicTableRef<const BasicTable>(this);
    }

private:
    template<int col_idx> struct Col {
        typedef typename TypeAt<typename Spec::Columns, col_idx>::type value_type;
        typedef _impl::ColumnAccessor<BasicTable, col_idx, value_type> type;
    };
    typedef typename Spec::template ColNames<Col, BasicTable*> ColsAccessor;

    template<int col_idx> struct ConstCol {
        typedef typename TypeAt<typename Spec::Columns, col_idx>::type value_type;
        typedef _impl::ColumnAccessor<const BasicTable, col_idx, value_type> type;
    };
    typedef typename Spec::template ColNames<ConstCol, const BasicTable*> ConstColsAccessor;

public:
    ColsAccessor column() { return ColsAccessor(this); }
    ConstColsAccessor column() const { return ConstColsAccessor(this); }

private:
    template<int col_idx> struct Field {
        typedef typename TypeAt<typename Spec::Columns, col_idx>::type value_type;
        typedef _impl::FieldAccessor<BasicTable, col_idx, value_type, false> type;
    };
    typedef std::pair<BasicTable*, std::size_t> FieldInit;
    typedef typename Spec::template ColNames<Field, FieldInit> RowAccessor;

    template<int col_idx> struct ConstField {
        typedef typename TypeAt<typename Spec::Columns, col_idx>::type value_type;
        typedef _impl::FieldAccessor<const BasicTable, col_idx, value_type, true> type;
    };
    typedef std::pair<const BasicTable*, std::size_t> ConstFieldInit;
    typedef typename Spec::template ColNames<ConstField, ConstFieldInit> ConstRowAccessor;

public:
    RowAccessor operator[](std::size_t row_idx)
    {
        return RowAccessor(std::make_pair(this, row_idx));
    }

    ConstRowAccessor operator[](std::size_t row_idx) const
    {
        return ConstRowAccessor(std::make_pair(this, row_idx));
    }

    RowAccessor front() { return RowAccessor(std::make_pair(this, 0)); }
    ConstRowAccessor front() const { return ConstRowAccessor(std::make_pair(this, 0)); }

    /// Access the last row, or one of its predecessors.
    ///
    /// \param rel_idx An optional index of the row specified relative
    /// to the end. Thus, <tt>table.back(rel_idx)</tt> is the same as
    /// <tt>table[table.size() + rel_idx]</tt>.
    ///
    RowAccessor back(int rel_idx = -1)
    {
        return RowAccessor(std::make_pair(this, m_size+rel_idx));
    }

    ConstRowAccessor back(int rel_idx = -1) const
    {
        return ConstRowAccessor(std::make_pair(this, m_size+rel_idx));
    }

    RowAccessor add() { return RowAccessor(std::make_pair(this, add_empty_row())); }

    template<class L> void add(const Tuple<L>& tuple)
    {
        TIGHTDB_STATIC_ASSERT(TypeCount<L>::value == TypeCount<Columns>::value,
                              "Wrong number of tuple elements");
        ForEachType<Columns, _impl::InsertIntoCol>::exec(static_cast<Table*>(this), size(), tuple);
        insert_done();
    }

    void insert(std::size_t i) { insert_empty_row(i); }

    template<class L> void insert(std::size_t i, const Tuple<L>& tuple)
    {
        TIGHTDB_STATIC_ASSERT(TypeCount<L>::value == TypeCount<Columns>::value,
                              "Wrong number of tuple elements");
        ForEachType<Columns, _impl::InsertIntoCol>::exec(static_cast<Table*>(this), i, tuple);
        insert_done();
    }

    template<class L> void set(std::size_t i, const Tuple<L>& tuple)
    {
        TIGHTDB_STATIC_ASSERT(TypeCount<L>::value == TypeCount<Columns>::value,
                              "Wrong number of tuple elements");
        ForEachType<Columns, _impl::AssignIntoCol>::exec(static_cast<Table*>(this), i, tuple);
    }

    using Spec::ConvenienceMethods::add; // FIXME: This probably fails if Spec::ConvenienceMethods has no add().
    using Spec::ConvenienceMethods::insert; // FIXME: This probably fails if Spec::ConvenienceMethods has no insert().
    using Spec::ConvenienceMethods::set; // FIXME: This probably fails if Spec::ConvenienceMethods has no set().

    typedef RowAccessor Cursor; // FIXME: A cursor must be a distinct class that can be constructed from a RowAccessor
    typedef ConstRowAccessor ConstCursor;


    class Query;
    Query       where() {return Query(*this);}
    const Query where() const {return Query(*this);}

    /// Compare two tables for equality. Two tables are equal if, and
    /// only if, they contain the same rows in the same order, that
    /// is, for each value V at column index C and row index R in one
    /// of the tables, there is a value at column index C and row
    /// index R in the other table that is equal to V.
    bool operator==(const BasicTable& t) const { return compare_rows(t); }

    /// Compare two tables for inequality. See operator==().
    bool operator!=(const BasicTable& t) const { return !compare_rows(t); }

    /// Checks whether the dynamic type of the specified table matches
    /// the statically specified table type. The two types (or specs)
    /// must have the same columns, and in the same order. Two columns
    /// are considered equal if, and only if they have the same name
    /// and the same type. The type is understood as the value encoded
    /// by the ColumnType enumeration. This check proceeds recursively
    /// for subtable columns.
    ///
    /// \tparam T The static table type. It makes no difference
    /// whether it is const-qualified or not.
    ///
    /// FIXME: Consider dropping the requirement that column names
    /// must be equal. There does not seem to be any value for the
    /// user in that requirement. Further more, there may be cases
    /// where it is desirable to be able to cast to a table type with
    /// different column names. Similar changes are needed in the Java
    /// and Objective-C language bindings.
    template<class T> friend bool is_a(const Table&);

    //@{
    /// These functions return null if the specified table is not
    /// compatible with the specified table type.
    template<class T> friend BasicTableRef<T> checked_cast(TableRef);
    template<class T> friend BasicTableRef<const T> checked_cast(ConstTableRef);
    //@}

#ifdef TIGHTDB_DEBUG
    using Table::Verify;
    using Table::print;
#endif

private:
    template<int col_idx> struct QueryCol {
        typedef typename TypeAt<typename Spec::Columns, col_idx>::type value_type;
        typedef _impl::QueryColumn<BasicTable, col_idx, value_type> type;
    };

    // These are intende to be used only by accessor classes
    Table* get_impl() { return this; }
    const Table* get_impl() const { return this; }

    template<class Subtab> Subtab* get_subtable_ptr(size_t col_idx, std::size_t row_idx)
    {
        return static_cast<Subtab*>(Table::get_subtable_ptr(col_idx, row_idx));
    }

    template<class Subtab> const Subtab* get_subtable_ptr(size_t col_idx, std::size_t row_idx) const
    {
        return static_cast<const Subtab*>(Table::get_subtable_ptr(col_idx, row_idx));
    }

    void set_dynamic_spec()
    {
        tightdb::Spec& spec = get_spec();
        ForEachType<typename Spec::Columns, _impl::AddCol>::exec(&spec, Spec::dyn_col_names());
        update_from_spec();
    }

    static bool matches_dynamic_spec(const tightdb::Spec* spec)
    {
        return !HasType<typename Spec::Columns,
                        _impl::DiffColType>::exec(spec, Spec::dyn_col_names());
    }

    // This one allows a BasicTable to know that BasicTables with
    // other Specs are also derived from Table.
    template<class> friend class BasicTable;

    // These allow bind_ptr to know that this class is derived from
    // Table.
    friend class bind_ptr<BasicTable>;
    friend class bind_ptr<const BasicTable>;

    // These allow BasicTableRef to refer to RowAccessor and
    // ConstRowAccessor.
    friend class BasicTableRef<BasicTable>;
    friend class BasicTableRef<const BasicTable>;

    // These allow BasicTableView to call get_subtable_ptr().
    friend class BasicTableView<BasicTable>;
    friend class BasicTableView<const BasicTable>;

    template<class, int> friend struct _impl::DiffColType;
    template<class, int, class, bool> friend class _impl::FieldAccessor;
    template<class, int, class> friend class _impl::MixedFieldAccessorBase;
    template<class, int, class> friend class _impl::ColumnAccessorBase;
    template<class, int, class> friend class _impl::ColumnAccessor;
    template<class, int, class> friend class _impl::QueryColumn;
    friend class Group;
};


#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4355)
#endif

template<class Spec> class BasicTable<Spec>::Query:
    public Spec::template ColNames<QueryCol, Query*> {
public:
    template<class, int, class> friend class _impl::QueryColumnBase;
    template<class, int, class> friend class _impl::QueryColumn;

    Query(const Query&q): Spec::template ColNames<QueryCol, Query*>(this), m_impl(q.m_impl) {}

    Query& tableview(const Array& arr) { m_impl.tableview(arr); return *this; }

// Query& Query::tableview(const TableView& tv)
// Query& Query::tableview(const Array &arr)

    Query& tableview(const typename BasicTable<Spec>::View& v) { 
        m_impl.tableview(*v.get_impl());
        return *this; 
    }


    Query& group() { m_impl.group(); return *this; }

    Query& end_group() { m_impl.end_group(); return *this; }

    Query& end_subtable() { m_impl.end_subtable(); return *this; }

    Query& Or() { m_impl.Or(); return *this; }

    std::size_t find_next(std::size_t lastmatch=std::size_t(-1))
    {
        return m_impl.find_next(lastmatch);
    }

    typename BasicTable<Spec>::View find_all(std::size_t start=0,
                                             std::size_t end=std::size_t(-1),
                                             std::size_t limit=std::size_t(-1))
    {
        return m_impl.find_all(start, end, limit);
    }

    typename BasicTable<Spec>::ConstView find_all(std::size_t start=0,
                                                  std::size_t end=std::size_t(-1),
                                                  std::size_t limit=std::size_t(-1)) const
    {
        return m_impl.find_all(start, end, limit);
    }

    std::size_t count(std::size_t start=0,
                      std::size_t end=std::size_t(-1), std::size_t limit=std::size_t(-1)) const
    {
        return m_impl.count(start, end, limit);
    }

    std::size_t remove(std::size_t start = 0,
                       std::size_t end = std::size_t(-1),
                       std::size_t limit = std::size_t(-1))
    {
        return m_impl.remove(start, end, limit);
    }

#ifdef TIGHTDB_DEBUG
    std::string Verify() { return m_impl.Verify(); }
#endif

protected:
    friend class BasicTable;

    Query(const BasicTable<Spec>& table): Spec::template ColNames<QueryCol, Query*>(this), m_impl((Table&)table) {}

private:
    tightdb::Query m_impl;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif




// Implementation:

namespace _impl
{
    template<class T> struct GetColumnTypeId;

    template<> struct GetColumnTypeId<int64_t> {
        static const ColumnType id = COLUMN_TYPE_INT;
    };
    template<> struct GetColumnTypeId<bool> {
        static const ColumnType id = COLUMN_TYPE_BOOL;
    };
    template<> struct GetColumnTypeId<float> {
        static const ColumnType id = COLUMN_TYPE_FLOAT;
    };
    template<> struct GetColumnTypeId<double> {
        static const ColumnType id = COLUMN_TYPE_DOUBLE;
    };
    template<> struct GetColumnTypeId<const char*> {
        static const ColumnType id = COLUMN_TYPE_STRING;
    };
    template<class E> struct GetColumnTypeId<SpecBase::Enum<E> > {
        static const ColumnType id = COLUMN_TYPE_INT;
    };
    template<> struct GetColumnTypeId<Date> {
        static const ColumnType id = COLUMN_TYPE_DATE;
    };
    template<> struct GetColumnTypeId<BinaryData> {
        static const ColumnType id = COLUMN_TYPE_BINARY;
    };
    template<> struct GetColumnTypeId<Mixed> {
        static const ColumnType id = COLUMN_TYPE_MIXED;
    };


    template<class Type, int col_idx> struct AddCol {
        static void exec(Spec* spec, const char* const* col_names)
        {
            TIGHTDB_ASSERT(col_idx == spec->get_column_count());
            spec->add_column(GetColumnTypeId<Type>::id, col_names[col_idx]);
        }
    };

    // AddCol specialization for subtables
    template<class Subtab, int col_idx> struct AddCol<SpecBase::Subtable<Subtab>, col_idx> {
        static void exec(Spec* spec, const char* const* col_names)
        {
            TIGHTDB_ASSERT(col_idx == spec->get_column_count());
            typedef typename Subtab::Columns Subcolumns;
            Spec subspec = spec->add_subtable_column(col_names[col_idx]);
            const char* const* const subcol_names = Subtab::spec_type::dyn_col_names();
            ForEachType<Subcolumns, _impl::AddCol>::exec(&subspec, subcol_names);
        }
    };



    template<class Type, int col_idx> struct DiffColType {
        static bool exec(const Spec* spec, const char* const* col_names)
        {
            return GetColumnTypeId<Type>::id != spec->get_column_type(col_idx) ||
                std::strcmp(col_names[col_idx], spec->get_column_name(col_idx)) != 0;
        }
    };

    // DiffColType specialization for subtables
    template<class Subtab, int col_idx> struct DiffColType<SpecBase::Subtable<Subtab>, col_idx> {
        static bool exec(const Spec* spec, const char* const* col_names)
        {
            if (spec->get_column_type(col_idx) != COLUMN_TYPE_TABLE ||
                std::strcmp(col_names[col_idx], spec->get_column_name(col_idx)) != 0) return true;
            Spec subspec = spec->get_subtable_spec(col_idx);
            return !Subtab::matches_dynamic_spec(&subspec);
        }
    };



    // InsertIntoCol specialization for integers
    template<int col_idx> struct InsertIntoCol<int64_t, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->insert_int(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // InsertIntoCol specialization for float
    template<int col_idx> struct InsertIntoCol<float, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->insert_float(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // InsertIntoCol specialization for double
    template<int col_idx> struct InsertIntoCol<double, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->insert_double(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // InsertIntoCol specialization for booleans
    template<int col_idx> struct InsertIntoCol<bool, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->insert_bool(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // InsertIntoCol specialization for strings
    template<int col_idx> struct InsertIntoCol<const char*, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->insert_string(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // InsertIntoCol specialization for enumerations
    template<class E, int col_idx> struct InsertIntoCol<SpecBase::Enum<E>, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->insert_enum(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // InsertIntoCol specialization for dates
    template<int col_idx> struct InsertIntoCol<Date, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            const Date d(at<col_idx>(tuple));
            t->insert_date(col_idx, row_idx, d.get_date());
        }
    };

    // InsertIntoCol specialization for binary data
    template<int col_idx> struct InsertIntoCol<BinaryData, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            const BinaryData b(at<col_idx>(tuple));
            t->insert_binary(col_idx, row_idx, b.pointer, b.len);
        }
    };

    // InsertIntoCol specialization for subtables
    template<class T, int col_idx> struct InsertIntoCol<SpecBase::Subtable<T>, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->insert_subtable(col_idx, row_idx);
            TIGHTDB_ASSERT(!static_cast<const T*>(at<col_idx>(tuple))); // FIXME: Implement table copy when specified!
            static_cast<void>(tuple);
        }
    };

    // InsertIntoCol specialization for mixed type
    template<int col_idx> struct InsertIntoCol<Mixed, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->insert_mixed(col_idx, row_idx, at<col_idx>(tuple));
        }
    };



    // AssignIntoCol specialization for integers
    template<int col_idx> struct AssignIntoCol<int64_t, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->set_int(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // AssignIntoCol specialization for floats
    template<int col_idx> struct AssignIntoCol<float, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->set_float(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // AssignIntoCol specialization for doubles
    template<int col_idx> struct AssignIntoCol<double, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->set_double(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // AssignIntoCol specialization for booleans
    template<int col_idx> struct AssignIntoCol<bool, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->set_bool(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // AssignIntoCol specialization for strings
    template<int col_idx> struct AssignIntoCol<const char*, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->set_string(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // AssignIntoCol specialization for enumerations
    template<class E, int col_idx> struct AssignIntoCol<SpecBase::Enum<E>, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->set_enum(col_idx, row_idx, at<col_idx>(tuple));
        }
    };

    // AssignIntoCol specialization for dates
    template<int col_idx> struct AssignIntoCol<Date, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            const Date d(at<col_idx>(tuple));
            t->set_date(col_idx, row_idx, d.get_date());
        }
    };

    // AssignIntoCol specialization for binary data
    template<int col_idx> struct AssignIntoCol<BinaryData, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            const BinaryData b(at<col_idx>(tuple));
            t->set_binary(col_idx, row_idx, b.pointer, b.len);
        }
    };

    // AssignIntoCol specialization for subtables
    template<class T, int col_idx> struct AssignIntoCol<SpecBase::Subtable<T>, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->clear_subtable(col_idx, row_idx);
            TIGHTDB_ASSERT(!static_cast<const T*>(at<col_idx>(tuple))); // FIXME: Implement table copy when specified!
            static_cast<void>(tuple);
        }
    };

    // AssignIntoCol specialization for mixed type
    template<int col_idx> struct AssignIntoCol<Mixed, col_idx> {
        template<class L> static void exec(Table* t, std::size_t row_idx, Tuple<L> tuple)
        {
            t->set_mixed(col_idx, row_idx, at<col_idx>(tuple));
        }
    };
}


template<class T> inline bool is_a(const Table& t)
{
    return T::matches_dynamic_spec(&t.get_spec());
}

template<class T> inline BasicTableRef<T> checked_cast(TableRef t)
{
    if (!is_a<T>(*t)) return BasicTableRef<T>(); // Null
    return unchecked_cast<T>(t);
}

template<class T> inline BasicTableRef<const T> checked_cast(ConstTableRef t)
{
    if (!is_a<T>(*t)) return BasicTableRef<const T>(); // Null
    return unchecked_cast<T>(t);
}


} // namespace tightdb

#endif // TIGHTDB_TABLE_BASIC_HPP
