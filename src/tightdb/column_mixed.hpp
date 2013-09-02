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
#ifndef TIGHTDB_COLUMN_MIXED_HPP
#define TIGHTDB_COLUMN_MIXED_HPP

#include <limits>

#include <tightdb/column.hpp>
#include <tightdb/column_type.hpp>
#include <tightdb/column_table.hpp>
#include <tightdb/column_binary.hpp>
#include <tightdb/table.hpp>
#include <tightdb/utilities.hpp>


namespace tightdb {


// Pre-declarations
class ColumnBinary;

class ColumnMixed: public ColumnBase {
public:
    /// Create a free-standing mixed column.
    ColumnMixed();

    /// Create a mixed column wrapper and have it instantiate a new
    /// underlying structure of arrays.
    ///
    /// \param table If this column is used as part of a table you
    /// must pass a pointer to that table. Otherwise you must pass
    /// null.
    ///
    /// \param column_ndx If this column is used as part of a table
    /// you must pass the logical index of the column within that
    /// table. Otherwise you should pass zero.
    ColumnMixed(Allocator&, const Table* table, std::size_t column_ndx);

    /// Create a mixed column wrapper and attach it to a preexisting
    /// underlying structure of arrays.
    ///
    /// \param table If this column is used as part of a table you
    /// must pass a pointer to that table. Otherwise you must pass
    /// null.
    ///
    /// \param column_ndx If this column is used as part of a table
    /// you must pass the logical index of the column within that
    /// table. Otherwise you should pass zero.
    ColumnMixed(Allocator&, const Table* table, std::size_t column_ndx,
                ArrayParent*, std::size_t ndx_in_parent, ref_type);

    ~ColumnMixed() TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;

    void update_from_parent(std::size_t old_baseline) TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;

    DataType get_type(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    std::size_t size() const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE { return m_types->size(); }
    bool is_empty() const TIGHTDB_NOEXCEPT { return m_types->is_empty(); }

    int64_t get_int(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    bool get_bool(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    Date get_date(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    float get_float(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    double get_double(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    StringData get_string(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    BinaryData get_binary(std::size_t ndx) const TIGHTDB_NOEXCEPT;

    /// The returned array ref is zero if the specified row does not
    /// contain a subtable.
    ref_type get_subtable_ref(std::size_t row_idx) const TIGHTDB_NOEXCEPT;

    /// The returned size is zero if the specified row does not
    /// contain a subtable.
    std::size_t get_subtable_size(std::size_t row_idx) const TIGHTDB_NOEXCEPT;

    /// Returns null if the specified row does not contain a subtable,
    /// otherwise the returned table pointer must end up being wrapped
    /// by an instance of BasicTableRef.
    Table* get_subtable_ptr(std::size_t row_idx) const;

    void set_int(std::size_t ndx, int64_t value);
    void set_bool(std::size_t ndx, bool value);
    void set_date(std::size_t ndx, Date value);
    void set_float(std::size_t ndx, float value);
    void set_double(std::size_t ndx, double value);
    void set_string(std::size_t ndx, StringData value);
    void set_binary(std::size_t ndx, BinaryData value);
    void set_subtable(std::size_t ndx, const Table*);

    void insert_int(std::size_t ndx, int64_t value);
    void insert_bool(std::size_t ndx, bool value);
    void insert_date(std::size_t ndx, Date value);
    void insert_float(std::size_t ndx, float value);
    void insert_double(std::size_t ndx, double value);
    void insert_string(std::size_t ndx, StringData value);
    void insert_binary(std::size_t ndx, BinaryData value);
    void insert_subtable(std::size_t ndx, const Table*);

    void add() TIGHTDB_OVERRIDE { insert_int(size(), 0); }
    void insert(std::size_t ndx) TIGHTDB_OVERRIDE { insert_int(ndx, 0); }
    void clear() TIGHTDB_OVERRIDE;
    void erase(std::size_t ndx) TIGHTDB_OVERRIDE;
    void move_last_over(std::size_t ndx) TIGHTDB_OVERRIDE;
    void fill(std::size_t count);

    /// Compare two mixed columns for equality.
    bool compare_mixed(const ColumnMixed&) const;

    void detach_subtable_accessors() TIGHTDB_NOEXCEPT;

    void detach_subtable_accessors_virtual() TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;

    static ref_type create(std::size_t num_default_values, Allocator&);

#ifdef TIGHTDB_DEBUG
    void Verify() const TIGHTDB_OVERRIDE; // Must be upper case to avoid conflict with macro in ObjC
    void to_dot(std::ostream&, StringData title) const TIGHTDB_OVERRIDE;
#endif

private:
    enum MixedColType {
        // NOTE: below numbers must be kept in sync with ColumnType
        // Column types used in Mixed
        mixcol_Int         =  0,
        mixcol_Bool        =  1,
        mixcol_String      =  2,
        //                    3, used for STRING_ENUM in ColumnType
        mixcol_Binary      =  4,
        mixcol_Table       =  5,
        mixcol_Mixed       =  6,
        mixcol_Date        =  7,
        //                    8, used for RESERVED1 in ColumnType
        mixcol_Float       =  9,
        mixcol_Double      = 10, // Positive Double
        mixcol_DoubleNeg   = 11, // Negative Double
        mixcol_IntNeg      = 12  // Negative Integers
    };

    class RefsColumn;

    /// Stores the MixedColType of each value at the given index. For
    /// values that uses all 64 bits, the type also encodes the sign
    /// bit by having distinct types for positive negative values.
    Column* m_types;

    /// Bit 0 is used to indicate if it's a 'ref'. If not, the data
    /// value is stored (shifted 1 bit left), and the sign bit is
    /// encoded in the type storeed in m_types at the corresponding
    /// index.
    RefsColumn* m_refs;

    /// For string and binary data types, the bytes are stored here.
    ColumnBinary* m_data;

    void create(Allocator&, const Table*, std::size_t column_ndx);
    void create(Allocator&, const Table*, std::size_t column_ndx,
                ArrayParent*, std::size_t ndx_in_parent, ref_type);
    void init_data_column();

    void clear_value(std::size_t ndx, MixedColType new_type);

    // Get/set/insert 64-bit values in m_refs/m_types
    int64_t get_value(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    void set_value(std::size_t ndx, int64_t value, MixedColType);
    void insert_int64(std::size_t ndx, int64_t value, MixedColType pos_type, MixedColType neg_type);
    void set_int64(std::size_t ndx, int64_t value, MixedColType pos_type, MixedColType neg_type);
};


class ColumnMixed::RefsColumn: public ColumnSubtableParent {
public:
    RefsColumn(Allocator& alloc, const Table* table, std::size_t column_ndx):
        ColumnSubtableParent(alloc, table, column_ndx) {}
    RefsColumn(Allocator& alloc, const Table* table, std::size_t column_ndx,
               ArrayParent* parent, std::size_t ndx_in_parent, ref_type ref):
        ColumnSubtableParent(alloc, table, column_ndx, parent, ndx_in_parent, ref) {}
    ~RefsColumn() TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE {}
    using ColumnSubtableParent::get_subtable_ptr;
    using ColumnSubtableParent::get_subtable;
};


} // namespace tightdb


// Implementation
#include <tightdb/column_mixed_tpl.hpp>


#endif // TIGHTDB_COLUMN_MIXED_HPP
