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
#ifndef TIGHTDB_TABLE_HPP
#define TIGHTDB_TABLE_HPP

#include <utility>

#include <tightdb/column_fwd.hpp>
#include <tightdb/table_ref.hpp>
#include <tightdb/spec.hpp>
#include <tightdb/mixed.hpp>
#include <tightdb/query.hpp>


#ifdef TIGHTDB_ENABLE_REPLICATION
#  include <tightdb/replication.hpp>
#endif

namespace tightdb {

using std::size_t;

class TableView;
class ConstTableView;
class StringIndex;


/// The Table class is non-polymorphic, that is, it has no virtual
/// functions. This is important because it ensures that there is no
/// run-time distinction between a Table instance and an instance of
/// any variation of BasicTable<T>, and this, in turn, makes it valid
/// to cast a pointer from Table to BasicTable<T> even when the
/// instance is constructed as a Table. Of couse, this also assumes
/// that BasicTable<> is non-polymorphic, has no destructor, and adds
/// no extra data members.
///
/// FIXME: Table copying (from any group to any group) could be made
/// aliasing safe as follows: Start by cloning source table into
/// target allocator. On success, assign, and then deallocate any
/// previous structure at the target.
///
/// FIXME: It might be desirable to have a 'table move' feature
/// between two places inside the same group (say from a subtable or a
/// mixed column to group level). This could be done in a very
/// efficient manner.
///
/// FIXME: When compiling in debug mode, all public table methods
/// should should TIGHTDB_ASSERT(is_valid()).
class Table {
public:
    /// Construct a new freestanding top-level table with static
    /// lifetime.
    ///
    /// This constructor should be used only when placing a table
    /// instance on the stack, and it is then the responsibility of
    /// the application that there are no objects of type TableRef or
    /// ConstTableRef that refer to it, or to any of its subtables,
    /// when it goes out of scope. To create a top-level table with
    /// dynamic lifetime, use Table::create() instead.
    Table(Allocator& = Allocator::get_default());

    /// Construct a copy of the specified table as a new freestanding
    /// top-level table with static lifetime.
    ///
    /// This constructor should be used only when placing a table
    /// instance on the stack, and it is then the responsibility of
    /// the application that there are no objects of type TableRef or
    /// ConstTableRef that refer to it, or to any of its subtables,
    /// when it goes out of scope. To create a top-level table with
    /// dynamic lifetime, use Table::copy() instead.
    Table(const Table&, Allocator& = Allocator::get_default());

    ~Table();

    /// Construct a new freestanding top-level table with dynamic
    /// lifetime.
    static TableRef create(Allocator& = Allocator::get_default());

    /// Construct a copy of the specified table as a new freestanding
    /// top-level table with dynamic lifetime.
    TableRef copy(Allocator& = Allocator::get_default()) const;

    /// An invalid table must not be accessed in any way except by
    /// calling is_valid(). A table that is obtained from a Group
    /// becomes invalid if its group is destroyed. This is also true
    /// for any subtable that is obtained indirectly from a group. A
    /// subtable will generally become invalid if its parent table is
    /// modified. Calling a const member function on a parent table,
    /// will never invalidate its subtables. A free standing table
    /// will never become invalid. A subtable of a freestanding table
    /// may become invalid.
    ///
    /// FIXME: High level language bindings will probably want to be
    /// able to explicitely invalidate a group and all tables of that
    /// group if any modifying operation fails (e.g. memory allocation
    /// failure) (and something similar for freestanding tables) since
    /// that leaves the group in state where any further access is
    /// disallowed. This way they will be able to reliably intercept
    /// any attempt at accessing such a failed group.
    ///
    /// FIXME: The C++ documentation must state that if any modifying
    /// operation on a group (incl. tables, subtables, and specs), or
    /// on a free standing table (incl. subtables and specs), then any
    /// further access to that group (except ~Group()) or freestanding
    /// table (except ~Table()) has undefined behaviour and is
    /// considered an error on behalf of the application. Note that
    /// even Table::is_valid() is disallowed in this case.
    bool is_valid() const TIGHTDB_NOEXCEPT { return m_columns.HasParent(); }

    /// A shared spec is a column specification that in general
    /// applies to many tables. A table is not allowed to directly
    /// modify its own spec if it is shared. A shared spec may only be
    /// modified via the closest ancestor table that has a nonshared
    /// spec. Such an ancestor will always exist.
    bool has_shared_spec() const;

    // Schema handling (see also <tightdb/spec.hpp>)
    Spec&       get_spec();
    const Spec& get_spec() const;
    void        update_from_spec(); // Must not be called for a table with shared spec
    size_t      add_column(DataType type, StringData name); // Add a column dynamically
    size_t      add_subcolumn(const std::vector<size_t>& column_path, DataType type, StringData name);
    void        remove_column(size_t column_ndx);
    void        remove_column(const std::vector<size_t>& column_path);
    void        rename_column(size_t column_ndx, StringData name);
    void        rename_column(const std::vector<size_t>& column_path, StringData name);

    // Table size and deletion
    bool        is_empty() const TIGHTDB_NOEXCEPT {return m_size == 0;}
    size_t      size() const TIGHTDB_NOEXCEPT {return m_size;}
    void        clear();

    // Column information
    size_t      get_column_count() const TIGHTDB_NOEXCEPT;
    StringData  get_column_name(size_t column_ndx) const TIGHTDB_NOEXCEPT;
    size_t      get_column_index(StringData name) const;
    DataType    get_column_type(size_t column_ndx) const TIGHTDB_NOEXCEPT;

    // Row handling
    size_t      add_empty_row(size_t num_rows = 1);
    void        insert_empty_row(size_t row_ndx, size_t num_rows = 1);
    void        remove(size_t row_ndx);
    void        remove_last() {if (!is_empty()) remove(m_size-1);}

    // Insert row
    // NOTE: You have to insert values in ALL columns followed by insert_done().
    void insert_int(size_t column_ndx, size_t row_ndx, int64_t value);
    void insert_bool(size_t column_ndx, size_t row_ndx, bool value);
    void insert_date(size_t column_ndx, size_t row_ndx, Date value);
    template<class E> void insert_enum(size_t column_ndx, size_t row_ndx, E value);
    void insert_float(size_t column_ndx, size_t row_ndx, float value);
    void insert_double(size_t column_ndx, size_t row_ndx, double value);
    void insert_string(size_t column_ndx, size_t row_ndx, StringData value);
    void insert_binary(size_t column_ndx, size_t row_ndx, BinaryData value);
    void insert_subtable(size_t column_ndx, size_t row_ndx); // Insert empty table
    void insert_mixed(size_t column_ndx, size_t row_ndx, Mixed value);
    void insert_done();

    // Get cell values
    int64_t     get_int(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;
    bool        get_bool(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;
    Date        get_date(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;
    float       get_float(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;
    double      get_double(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;
    StringData  get_string(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;
    BinaryData  get_binary(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;
    Mixed       get_mixed(size_t column_ndx, size_t row_ndx) const; // FIXME: Should be modified so it never throws
    DataType    get_mixed_type(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;

    // Set cell values
    void set_int(size_t column_ndx, size_t row_ndx, int64_t value);
    void set_bool(size_t column_ndx, size_t row_ndx, bool value);
    void set_date(size_t column_ndx, size_t row_ndx, Date value);
    template<class E> void set_enum(size_t column_ndx, size_t row_ndx, E value);
    void set_float(size_t column_ndx, size_t row_ndx, float value);
    void set_double(size_t column_ndx, size_t row_ndx, double value);
    void set_string(size_t column_ndx, size_t row_ndx, StringData value);
    void set_binary(size_t column_ndx, size_t row_ndx, BinaryData value);
    void set_mixed(size_t column_ndx, size_t row_ndx, Mixed value);
    void add_int(size_t column_ndx, int64_t value);

    // Sub-tables (works on columns whose type is either 'subtable' or
    // 'mixed', for a value in a mixed column that is not a subtable,
    // get_subtable() returns null, get_subtable_size() returns zero,
    // and clear_subtable() replaces the value with an empty table.)
    TableRef       get_subtable(size_t column_ndx, size_t row_ndx);
    ConstTableRef  get_subtable(size_t column_ndx, size_t row_ndx) const;
    size_t         get_subtable_size(size_t column_ndx, size_t row_ndx) const TIGHTDB_NOEXCEPT;
    void           clear_subtable(size_t column_ndx, size_t row_ndx);

    // Indexing
    bool has_index(size_t column_ndx) const;
    void set_index(size_t column_ndx) {set_index(column_ndx, true);}

    // Aggregate functions
    size_t  count_int(size_t column_ndx, int64_t value) const;
    size_t  count_string(size_t column_ndx, StringData value) const;
    size_t  count_float(size_t column_ndx, float value) const;
    size_t  count_double(size_t column_ndx, double value) const;

    int64_t sum(size_t column_ndx) const;
    double  sum_float(size_t column_ndx) const;
    double  sum_double(size_t column_ndx) const;
    // FIXME: What to return for below when table empty? 0?
    int64_t maximum(size_t column_ndx) const;
    float   maximum_float(size_t column_ndx) const;
    double  maximum_double(size_t column_ndx) const;
    int64_t minimum(size_t column_ndx) const;
    float   minimum_float(size_t column_ndx) const;
    double  minimum_double(size_t column_ndx) const;
    double  average(size_t column_ndx) const;
    double  average_float(size_t column_ndx) const;
    double  average_double(size_t column_ndx) const;

    // Searching
    size_t         lookup(StringData value) const;
    size_t         find_first_int(size_t column_ndx, int64_t value) const;
    size_t         find_first_bool(size_t column_ndx, bool value) const;
    size_t         find_first_date(size_t column_ndx, Date value) const;
    size_t         find_first_float(size_t column_ndx, float value) const;
    size_t         find_first_double(size_t column_ndx, double value) const;
    size_t         find_first_string(size_t column_ndx, StringData value) const;
    size_t         find_first_binary(size_t column_ndx, BinaryData value) const;

    TableView      find_all_int(size_t column_ndx, int64_t value);
    ConstTableView find_all_int(size_t column_ndx, int64_t value) const;
    TableView      find_all_bool(size_t column_ndx, bool value);
    ConstTableView find_all_bool(size_t column_ndx, bool value) const;
    TableView      find_all_date(size_t column_ndx, Date value);
    ConstTableView find_all_date(size_t column_ndx, Date value) const;
    TableView      find_all_float(size_t column_ndx, float value);
    ConstTableView find_all_float(size_t column_ndx, float value) const;
    TableView      find_all_double(size_t column_ndx, double value);
    ConstTableView find_all_double(size_t column_ndx, double value) const;
    TableView      find_all_string(size_t column_ndx, StringData value);
    ConstTableView find_all_string(size_t column_ndx, StringData value) const;
    TableView      find_all_binary(size_t column_ndx, BinaryData value);
    ConstTableView find_all_binary(size_t column_ndx, BinaryData value) const;

    TableView      distinct(size_t column_ndx);
    ConstTableView distinct(size_t column_ndx) const;

    TableView      get_sorted_view(size_t column_ndx, bool ascending=true);
    ConstTableView get_sorted_view(size_t column_ndx, bool ascending=true) const;

    // Queries
    Query       where() {return Query(*this);}
    const Query where() const {return Query(*this);} // FIXME: There is no point in returning a const Query. We need a ConstQuery class.

    // Optimizing
    void optimize();

    // Conversion
    void to_json(std::ostream& out);
    void to_string(std::ostream& out, size_t limit=500) const;
    void row_to_string(size_t row_ndx, std::ostream& out) const;

    // Get a reference to this table
    TableRef get_table_ref() { return TableRef(this); }
    ConstTableRef get_table_ref() const { return ConstTableRef(this); }

    /// Compare two tables for equality. Two tables are equal if, and
    /// only if, they contain the same columns and rows in the same
    /// order, that is, for each value V of type T at column index C
    /// and row index R in one of the tables, there is a value of type
    /// T at column index C and row index R in the other table that
    /// is equal to V.
    bool operator==(const Table&) const;

    /// Compare two tables for inequality. See operator==().
    bool operator!=(const Table& t) const;

    // Debug
#ifdef TIGHTDB_DEBUG
    void Verify() const; // Must be upper case to avoid conflict with macro in ObjC
    void to_dot(std::ostream& out, StringData title = StringData()) const;
    void print() const;
    MemStats stats() const;
#endif // TIGHTDB_DEBUG

    const ColumnBase& GetColumnBase(size_t column_ndx) const TIGHTDB_NOEXCEPT; // FIXME: Move this to private section next to the non-const version
    ColumnType get_real_column_type(size_t column_ndx) const TIGHTDB_NOEXCEPT; // FIXME: Used by various node types in <tightdb/query_engine.hpp>

    class Parent;

protected:
    size_t find_pos_int(size_t column_ndx, int64_t value) const TIGHTDB_NOEXCEPT;

    // FIXME: Most of the things that are protected here, could instead be private
    // Direct Column access
    template <class T, ColumnType col_type> T& GetColumn(size_t ndx);
    template <class T, ColumnType col_type> const T& GetColumn(size_t ndx) const TIGHTDB_NOEXCEPT;
    Column& GetColumn(size_t column_ndx);
    const Column& GetColumn(size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnFloat& GetColumnFloat(size_t column_ndx);
    const ColumnFloat& GetColumnFloat(size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnDouble& GetColumnDouble(size_t column_ndx);
    const ColumnDouble& GetColumnDouble(size_t column_ndx) const TIGHTDB_NOEXCEPT;
    AdaptiveStringColumn& GetColumnString(size_t column_ndx);
    const AdaptiveStringColumn& GetColumnString(size_t column_ndx) const TIGHTDB_NOEXCEPT;

    ColumnBinary& GetColumnBinary(size_t column_ndx);
    const ColumnBinary& GetColumnBinary(size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnStringEnum& GetColumnStringEnum(size_t column_ndx);
    const ColumnStringEnum& GetColumnStringEnum(size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnTable& GetColumnTable(size_t column_ndx);
    const ColumnTable& GetColumnTable(size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnMixed& GetColumnMixed(size_t column_ndx);
    const ColumnMixed& GetColumnMixed(size_t column_ndx) const TIGHTDB_NOEXCEPT;

    /// Used when the lifetime of a table is managed by reference
    /// counting. The lifetime of free-standing tables allocated on
    /// the stack by the application is not managed by reference
    /// counting, so that is a case where this tag must not be
    /// specified.
    class RefCountTag {};

    /// Construct a wrapper for a table with independent spec, and
    /// whose lifetime is managed by reference counting.
    Table(RefCountTag, Allocator& alloc, size_t top_ref,
          Parent* parent, size_t ndx_in_parent);

    /// Construct a wrapper for a table with shared spec, and whose
    /// lifetime is managed by reference counting.
    ///
    /// It is possible to construct a 'null' table by passing zero for
    /// \a columns_ref, in this case the columns will be created on
    /// demand.
    Table(RefCountTag, Allocator& alloc, size_t spec_ref, size_t columns_ref,
          Parent* parent, size_t ndx_in_parent);

    void init_from_ref(size_t top_ref, ArrayParent* parent, size_t ndx_in_parent);
    void init_from_ref(size_t spec_ref, size_t columns_ref,
                       ArrayParent* parent, size_t ndx_in_parent);
    void CreateColumns();
    void CacheColumns();
    void ClearCachedColumns();

    // Specification
    size_t GetColumnRefPos(size_t column_ndx) const;
    void   UpdateColumnRefs(size_t column_ndx, int diff);
    void   UpdateFromParent();
    void   do_remove_column(const std::vector<size_t>& column_ids, size_t pos);
    void   do_remove_column(size_t column_ndx);
    size_t do_add_column(DataType type);
    void   do_add_subcolumn(const std::vector<size_t>& column_path, size_t pos, DataType type);

    void   set_index(size_t column_ndx, bool update_spec);

    // Support function for conversions
    void to_json_row(size_t row_ndx, std::ostream& out);
    void to_string_header(std::ostream& out, std::vector<size_t>& widths) const;
    void to_string_row(size_t row_ndx, std::ostream& out, const std::vector<size_t>& widths) const;


#ifdef TIGHTDB_DEBUG
    void ToDotInternal(std::ostream& out) const;
#endif // TIGHTDB_DEBUG

    // Member variables
    size_t m_size;

    // On-disk format
    Array m_top;
    Array m_columns;
    Spec m_spec_set;

    // Cached columns
    Array m_cols;

    /// Get the subtable at the specified column and row index.
    ///
    /// The returned table pointer must always end up being wrapped in
    /// a TableRef.
    Table* get_subtable_ptr(size_t col_idx, size_t row_idx);

    /// Get the subtable at the specified column and row index.
    ///
    /// The returned table pointer must always end up being wrapped in
    /// a ConstTableRef.
    const Table* get_subtable_ptr(size_t col_idx, size_t row_idx) const;

    /// Compare the rows of two tables under the assumption that the
    /// two tables have the same spec, and therefore the same sequence
    /// of columns.
    bool compare_rows(const Table&) const;

    /// Assumes that the specified column is a subtable column (in
    /// particular, not a mixed column) and that the specified table
    /// has a spec that is compatible with that column, that is, the
    /// number of columns must be the same, and corresponding columns
    /// must have identical data types (as returned by
    /// get_column_type()).
    void insert_subtable(std::size_t col_ndx, std::size_t row_ndx, const Table*);

    void insert_mixed_subtable(std::size_t col_ndx, std::size_t row_ndx, const Table*);

    void set_mixed_subtable(std::size_t col_ndx, std::size_t row_ndx, const Table*);

    void insert_into(Table* parent, std::size_t col_ndx, std::size_t row_ndx) const;

    void set_into_mixed(Table* parent, std::size_t col_ndx, std::size_t row_ndx) const;

private:
    Table& operator=(const Table&); // Disable copying assignment

    /// Put this table wrapper into the invalid state, which detaches
    /// it from the underlying structure of arrays. Also do this
    /// recursively for subtables. When this function returns,
    /// is_valid() will return false.
    ///
    /// This function may be called for a table wrapper that is
    /// already in the invalid state (idempotency).
    ///
    /// It is also valid to call this function for a table wrapper
    /// that has not yet been marked as invalid, but whose underlying
    /// structure of arrays have changed in an unpredictable/unknown
    /// way. This generally happens when a modifying table operation
    /// fails, and also when one transaction is ended and a new one is
    /// started.
    void invalidate();

    mutable size_t m_ref_count;
    mutable const StringIndex* m_lookup_index;

    void bind_ref() const TIGHTDB_NOEXCEPT { ++m_ref_count; }
    void unbind_ref() const { if (--m_ref_count == 0) delete this; } // FIXME: Cannot be noexcept since ~Table() may throw

    struct UnbindGuard;

    const Array* get_column_root(std::size_t col_ndx) const TIGHTDB_NOEXCEPT;
    std::pair<const Array*, const Array*> get_string_column_roots(std::size_t col_ndx) const
        TIGHTDB_NOEXCEPT;

    ColumnBase& GetColumnBase(size_t column_ndx);
    void InstantiateBeforeChange();
    void validate_column_type(const ColumnBase& column, ColumnType expected_type, size_t ndx) const;

    /// Construct an empty table with independent spec and return just
    /// the reference to the underlying memory.
    static std::size_t create_empty_table(Allocator&);

    /// Construct a copy of the columns array of this table using the
    /// specified allocator and return just the ref to that array.
    ///
    /// In the clone, no string column will be of the enumeration
    /// type.
    std::size_t clone_columns(Allocator&) const;

    /// Construct a complete copy of this table (including its spec)
    /// using the specified allocator and return just the ref to that
    /// array.
    std::size_t clone(Allocator&) const;

    // Experimental
    TableView find_all_hamming(size_t column_ndx, uint64_t value, size_t max);
    ConstTableView find_all_hamming(size_t column_ndx, uint64_t value, size_t max) const;

#ifdef TIGHTDB_ENABLE_REPLICATION
    struct LocalTransactLog;
    LocalTransactLog transact_log() TIGHTDB_NOEXCEPT;
    // Condition: 1 <= end - begin
    size_t* record_subspec_path(const Spec*, size_t* begin, size_t* end) const TIGHTDB_NOEXCEPT;
    // Condition: 1 <= end - begin
    size_t* record_subtable_path(size_t* begin, size_t* end) const TIGHTDB_NOEXCEPT;
    friend class Replication;
#endif

    friend class Group;
    friend class Query;
    friend class ColumnMixed;
    template<class> friend class bind_ptr;
    friend class ColumnSubtableParent;
    friend class LangBindHelper;
    friend class TableViewBase;
};



class Table::Parent: public ArrayParent {
protected:
    friend class Table;

    // ColumnTable must override this method and return true.
    virtual bool subtables_have_shared_spec() { return false; }

    /// Must be called whenever a child Table is destroyed.
    virtual void child_destroyed(size_t child_ndx) = 0;

#ifdef TIGHTDB_ENABLE_REPLICATION
    virtual size_t* record_subtable_path(size_t* begin, size_t* end) TIGHTDB_NOEXCEPT;
#endif
};





// Implementation:

inline std::size_t Table::get_column_count() const TIGHTDB_NOEXCEPT
{
    return m_spec_set.get_column_count();
}

inline StringData Table::get_column_name(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < get_column_count());
    return m_spec_set.get_column_name(ndx);
}

inline std::size_t Table::get_column_index(StringData name) const
{
    return m_spec_set.get_column_index(name);
}

inline ColumnType Table::get_real_column_type(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < get_column_count());
    return m_spec_set.get_real_column_type(ndx);
}

inline DataType Table::get_column_type(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < get_column_count());
    return m_spec_set.get_column_type(ndx);
}

template <class C, ColumnType coltype>
C& Table::GetColumn(size_t ndx)
{
    ColumnBase& column = GetColumnBase(ndx);
#ifdef TIGHTDB_DEBUG
    validate_column_type(column, coltype, ndx);
#endif
    return static_cast<C&>(column);
}

template <class C, ColumnType coltype>
const C& Table::GetColumn(size_t ndx) const TIGHTDB_NOEXCEPT
{
    const ColumnBase& column = GetColumnBase(ndx);
#ifdef TIGHTDB_DEBUG
    validate_column_type(column, coltype, ndx);
#endif
    return static_cast<const C&>(column);
}


inline bool Table::has_shared_spec() const
{
    const Array& top_array = m_top.IsValid() ? m_top : m_columns;
    ArrayParent* parent = top_array.GetParent();
    if (!parent) return false;
    TIGHTDB_ASSERT(dynamic_cast<Parent*>(parent));
    return static_cast<Parent*>(parent)->subtables_have_shared_spec();
}

struct Table::UnbindGuard {
    UnbindGuard(Table* t) TIGHTDB_NOEXCEPT: m_table(t) {}
    ~UnbindGuard() { if(m_table) m_table->unbind_ref(); } // FIXME: Cannot be noexcept since ~Table() may throw
    Table* operator->() const { return m_table; }
    Table* get() const { return m_table; }
    Table* release() TIGHTDB_NOEXCEPT { Table* t = m_table; m_table = 0; return t; }
private:
    Table* m_table;
};

inline std::size_t Table::create_empty_table(Allocator& alloc)
{
    Array top(Array::coldef_HasRefs, 0, 0, alloc);
    top.add(Spec::create_empty_spec(alloc));
    top.add(Array::create_empty_array(Array::coldef_HasRefs, alloc)); // Columns
    return top.GetRef();
}

inline Table::Table(Allocator& alloc):
    m_size(0), m_top(alloc), m_columns(alloc), m_spec_set(this, alloc), m_ref_count(1), m_lookup_index(NULL)
{
    size_t ref = create_empty_table(alloc); // Throws
    init_from_ref(ref, 0, 0);
}

inline Table::Table(const Table& t, Allocator& alloc):
    m_size(0), m_top(alloc), m_columns(alloc), m_spec_set(this, alloc), m_ref_count(1), m_lookup_index(NULL)
{
    size_t ref = t.clone(alloc); // Throws
    init_from_ref(ref, 0, 0);
}

inline Table::Table(RefCountTag, Allocator& alloc, size_t top_ref,
                    Parent* parent, size_t ndx_in_parent):
    m_size(0), m_top(alloc), m_columns(alloc), m_spec_set(this, alloc), m_ref_count(0), m_lookup_index(NULL)
{
    init_from_ref(top_ref, parent, ndx_in_parent);
}

inline Table::Table(RefCountTag, Allocator& alloc, size_t spec_ref, size_t columns_ref,
                    Parent* parent, size_t ndx_in_parent):
    m_size(0), m_top(alloc), m_columns(alloc), m_spec_set(this, alloc), m_ref_count(0), m_lookup_index(NULL)
{
    init_from_ref(spec_ref, columns_ref, parent, ndx_in_parent);
}

inline TableRef Table::create(Allocator& alloc)
{
    std::size_t ref = create_empty_table(alloc); // Throws
    Table* const table = new Table(Table::RefCountTag(), alloc, ref, 0, 0); // Throws
    return table->get_table_ref();
}

inline TableRef Table::copy(Allocator& alloc) const
{
    std::size_t ref = clone(alloc); // Throws
    Table* const table = new Table(Table::RefCountTag(), alloc, ref, 0, 0); // Throws
    return table->get_table_ref();
}


inline void Table::insert_bool(size_t column_ndx, size_t row_ndx, bool value)
{
    insert_int(column_ndx, row_ndx, value);
}

inline void Table::insert_date(size_t column_ndx, size_t row_ndx, Date value)
{
    insert_int(column_ndx, row_ndx, value.get_date());
}

template<class E> inline void Table::insert_enum(size_t column_ndx, size_t row_ndx, E value)
{
    insert_int(column_ndx, row_ndx, value);
}

inline void Table::insert_subtable(size_t col_ndx, size_t row_ndx)
{
    insert_subtable(col_ndx, row_ndx, 0); // Null stands for an empty table
}

template<class E> inline void Table::set_enum(size_t column_ndx, size_t row_ndx, E value)
{
    set_int(column_ndx, row_ndx, value);
}

inline TableRef Table::get_subtable(size_t column_ndx, size_t row_ndx)
{
    return TableRef(get_subtable_ptr(column_ndx, row_ndx));
}

inline ConstTableRef Table::get_subtable(size_t column_ndx, size_t row_ndx) const
{
    return ConstTableRef(get_subtable_ptr(column_ndx, row_ndx));
}

inline bool Table::operator==(const Table& t) const
{
    return m_spec_set == t.m_spec_set && compare_rows(t);
}

inline bool Table::operator!=(const Table& t) const
{
    return m_spec_set != t.m_spec_set || !compare_rows(t);
}

inline void Table::insert_into(Table* parent, std::size_t col_ndx, std::size_t row_ndx) const
{
    parent->insert_subtable(col_ndx, row_ndx, this);
}

inline void Table::set_into_mixed(Table* parent, std::size_t col_ndx, std::size_t row_ndx) const
{
    parent->insert_mixed_subtable(col_ndx, row_ndx, this);
}


#ifdef TIGHTDB_ENABLE_REPLICATION

struct Table::LocalTransactLog {
    template<class T> void set_value(size_t column_ndx, size_t row_ndx, const T& value)
    {
        if (m_repl) m_repl->set_value(m_table, column_ndx, row_ndx, value); // Throws
    }

    template<class T> void insert_value(size_t column_ndx, size_t row_ndx, const T& value)
    {
        if (m_repl) m_repl->insert_value(m_table, column_ndx, row_ndx, value); // Throws
    }

    void row_insert_complete()
    {
        if (m_repl) m_repl->row_insert_complete(m_table); // Throws
    }

    void insert_empty_rows(std::size_t row_ndx, std::size_t num_rows)
    {
        if (m_repl) m_repl->insert_empty_rows(m_table, row_ndx, num_rows); // Throws
    }

    void remove_row(std::size_t row_ndx)
    {
        if (m_repl) m_repl->remove_row(m_table, row_ndx); // Throws
    }

    void add_int_to_column(std::size_t column_ndx, int64_t value)
    {
        if (m_repl) m_repl->add_int_to_column(m_table, column_ndx, value); // Throws
    }

    void add_index_to_column(std::size_t column_ndx)
    {
        if (m_repl) m_repl->add_index_to_column(m_table, column_ndx); // Throws
    }

    void clear_table()
    {
        if (m_repl) m_repl->clear_table(m_table); // Throws
    }

    void optimize_table()
    {
        if (m_repl) m_repl->optimize_table(m_table); // Throws
    }

    void add_column(DataType type, StringData name)
    {
        if (m_repl) m_repl->add_column(m_table, &m_table->m_spec_set, type, name); // Throws
    }

    void on_table_destroyed() TIGHTDB_NOEXCEPT
    {
        if (m_repl) m_repl->on_table_destroyed(m_table);
    }

private:
    Replication* const m_repl;
    Table* const m_table;
    LocalTransactLog(Replication* r, Table* t) TIGHTDB_NOEXCEPT: m_repl(r), m_table(t) {}
    friend class Table;
};

inline Table::LocalTransactLog Table::transact_log() TIGHTDB_NOEXCEPT
{
    return LocalTransactLog(m_top.GetAllocator().get_replication(), this);
}

inline size_t* Table::record_subspec_path(const Spec* spec, size_t* begin,
                                          size_t* end) const TIGHTDB_NOEXCEPT
{
    if (spec != &m_spec_set) {
        TIGHTDB_ASSERT(m_spec_set.m_subSpecs.IsValid());
        return spec->record_subspec_path(&m_spec_set.m_subSpecs, begin, end);
    }
    return begin;
}

inline size_t* Table::record_subtable_path(size_t* begin, size_t* end) const TIGHTDB_NOEXCEPT
{
    const Array& real_top = m_top.IsValid() ? m_top : m_columns;
    const size_t index_in_parent = real_top.GetParentNdx();
    TIGHTDB_ASSERT(begin < end);
    *begin++ = index_in_parent;
    ArrayParent* parent = real_top.GetParent();
    TIGHTDB_ASSERT(parent);
    TIGHTDB_ASSERT(dynamic_cast<Parent*>(parent));
    return static_cast<Parent*>(parent)->record_subtable_path(begin, end);
}

inline size_t* Table::Parent::record_subtable_path(size_t* begin, size_t*) TIGHTDB_NOEXCEPT
{
    return begin;
}

#endif // TIGHTDB_ENABLE_REPLICATION


} // namespace tightdb

#endif // TIGHTDB_TABLE_HPP
