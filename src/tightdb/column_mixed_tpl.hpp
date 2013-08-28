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

namespace tightdb {

inline ColumnMixed::ColumnMixed(): m_data(0)
{
    create(Allocator::get_default(), 0, 0);
}

inline ColumnMixed::ColumnMixed(Allocator& alloc, const Table* table, std::size_t column_ndx):
    m_data(0)
{
    create(alloc, table, column_ndx);
}

inline ColumnMixed::ColumnMixed(Allocator& alloc, const Table* table, std::size_t column_ndx,
                                ArrayParent* parent, std::size_t ndx_in_parent, ref_type ref):
    m_data(0)
{
    create(alloc, table, column_ndx, parent, ndx_in_parent, ref);
}

inline ref_type ColumnMixed::get_subtable_ref(std::size_t row_idx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(row_idx < m_types->size());
    if (m_types->get(row_idx) != type_Table) return 0;
    return m_refs->get_as_ref(row_idx);
}

inline std::size_t ColumnMixed::get_subtable_size(std::size_t row_idx) const TIGHTDB_NOEXCEPT
{
    // FIXME: If the table object is cached, it is possible to get the
    // size from it. Maybe it is faster in general to check for the
    // the presence of the cached object and use it when available.
    ref_type top_ref = get_subtable_ref(row_idx);
    if (!top_ref) return 0;
    ref_type columns_ref = Array(top_ref, 0, 0, m_refs->get_alloc()).get_as_ref(1);
    Array columns(columns_ref, 0, 0, m_refs->get_alloc());
    if (columns.is_empty()) return 0;
    ref_type first_col_ref = columns.get_as_ref(0);
    return get_size_from_ref(first_col_ref, m_refs->get_alloc());
}

inline Table* ColumnMixed::get_subtable_ptr(std::size_t row_idx) const
{
    TIGHTDB_ASSERT(row_idx < m_types->size());
    if (m_types->get(row_idx) != type_Table)
        return 0;
    return m_refs->get_subtable_ptr(row_idx);
}

inline void ColumnMixed::invalidate_subtables() TIGHTDB_NOEXCEPT
{
    m_refs->invalidate_subtables();
}

inline void ColumnMixed::invalidate_subtables_virtual() TIGHTDB_NOEXCEPT
{
    invalidate_subtables();
}

inline ref_type ColumnMixed::create(std::size_t size, Allocator& alloc)
{
    ColumnMixed c(alloc, 0, 0);
    c.fill(size);
    return c.get_ref();
}


//
// Getters
//

#define TIGHTDB_BIT63 0x8000000000000000

inline int64_t ColumnMixed::get_value(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < m_types->size());

    // Shift the unsigned value right - ensuring 0 gets in from left.
    // Shifting signed integers right doesn't ensure 0's.
    uint64_t value = uint64_t(m_refs->get(ndx)) >> 1;
    return int64_t(value);
}

inline int64_t ColumnMixed::get_int(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    // Get first 63 bits of the integer value
    int64_t value = get_value(ndx);

    // restore 'sign'-bit from the column-type
    MixedColType col_type = MixedColType(m_types->get(ndx));
    if (col_type == mixcol_IntNeg)
        value |= TIGHTDB_BIT63; // set sign bit (63)
    else {
        TIGHTDB_ASSERT(col_type == mixcol_Int);
    }
    return value;
}

inline bool ColumnMixed::get_bool(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(m_types->get(ndx) == mixcol_Bool);

    return (get_value(ndx) != 0);
}

inline Date ColumnMixed::get_date(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(m_types->get(ndx) == mixcol_Date);

    return Date(std::time_t(get_value(ndx)));
}

inline float ColumnMixed::get_float(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_STATIC_ASSERT(std::numeric_limits<float>::is_iec559, "'float' is not IEEE");
    TIGHTDB_STATIC_ASSERT((sizeof (float) * CHAR_BIT == 32), "Assume 32 bit float.");
    TIGHTDB_ASSERT(m_types->get(ndx) == mixcol_Float);

    return type_punning<float>(get_value(ndx));
}

inline double ColumnMixed::get_double(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_STATIC_ASSERT(std::numeric_limits<double>::is_iec559, "'double' is not IEEE");
    TIGHTDB_STATIC_ASSERT((sizeof (double) * CHAR_BIT == 64), "Assume 64 bit double.");

    int64_t int_val = get_value(ndx);

    // restore 'sign'-bit from the column-type
    MixedColType col_type = MixedColType(m_types->get(ndx));
    if (col_type == mixcol_DoubleNeg)
        int_val |= TIGHTDB_BIT63; // set sign bit (63)
    else {
        TIGHTDB_ASSERT(col_type == mixcol_Double);
    }
    return type_punning<double>(int_val);
}

inline StringData ColumnMixed::get_string(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < m_types->size());
    TIGHTDB_ASSERT(m_types->get(ndx) == mixcol_String);
    TIGHTDB_ASSERT(m_data);

    std::size_t data_ndx = std::size_t(int64_t(m_refs->get(ndx)) >> 1);
    return m_data->get_string(data_ndx);
}

inline BinaryData ColumnMixed::get_binary(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < m_types->size());
    TIGHTDB_ASSERT(m_types->get(ndx) == mixcol_Binary);
    TIGHTDB_ASSERT(m_data);

    std::size_t data_ndx = std::size_t(uint64_t(m_refs->get(ndx)) >> 1);
    return m_data->get(data_ndx);
}

//
// Setters
//

// Set a int64 value.
// Store 63 bit of the value in m_refs. Store sign bit in m_types.

inline void ColumnMixed::set_int64(std::size_t ndx, int64_t value, MixedColType pos_type, MixedColType neg_type)
{
    TIGHTDB_ASSERT(ndx < m_types->size());

    // If sign-bit is set in value, 'store' it in the column-type
    MixedColType coltype = ((value & TIGHTDB_BIT63) == 0) ? pos_type : neg_type;

    // Remove refs or binary data (sets type to double)
    clear_value(ndx, coltype);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    value = (value << 1) + 1;
    m_refs->set(ndx, value);
}

inline void ColumnMixed::set_int(std::size_t ndx, int64_t value)
{
    invalidate_subtables();
    set_int64(ndx, value, mixcol_Int, mixcol_IntNeg);
}

inline void ColumnMixed::set_double(std::size_t ndx, double value)
{
    invalidate_subtables();
    int64_t val64 = type_punning<int64_t>(value);
    set_int64(ndx, val64, mixcol_Double, mixcol_DoubleNeg);
}

inline void ColumnMixed::set_value(std::size_t ndx, int64_t value, MixedColType coltype)
{
    TIGHTDB_ASSERT(ndx < m_types->size());

    // Remove refs or binary data (sets type to float)
    clear_value(ndx, coltype);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    int64_t v = (value << 1) + 1;
    m_refs->set(ndx, v);
}

inline void ColumnMixed::set_float(std::size_t ndx, float value)
{
    invalidate_subtables();
    int64_t val64 = type_punning<int64_t>( value );
    set_value(ndx, val64, mixcol_Float);
}

inline void ColumnMixed::set_bool(std::size_t ndx, bool value)
{
    invalidate_subtables();
    set_value(ndx, (value ? 1 : 0), mixcol_Bool);
}

inline void ColumnMixed::set_date(std::size_t ndx, Date value)
{
    invalidate_subtables();
    set_value(ndx, int64_t(value.get_date()), mixcol_Date);
}

inline void ColumnMixed::set_subtable(std::size_t ndx, const Table* t)
{
    TIGHTDB_ASSERT(ndx < m_types->size());
    invalidate_subtables();
    ref_type ref;
    if (t) {
        ref = t->clone(m_array->get_alloc()); // Throws
    }
    else {
        ref = Table::create_empty_table(m_array->get_alloc()); // Throws
    }
    clear_value(ndx, mixcol_Table); // Remove any previous refs or binary data
    m_refs->set(ndx, ref);
}

//
// Inserts
//

// Insert a int64 value.
// Store 63 bit of the value in m_refs. Store sign bit in m_types.

inline void ColumnMixed::insert_int64(std::size_t ndx, int64_t value, MixedColType pos_type,
                                      MixedColType neg_type)
{
    TIGHTDB_ASSERT(ndx <= m_types->size());

    // 'store' the sign-bit in the integer-type
    if ((value & TIGHTDB_BIT63) == 0)
        m_types->insert(ndx, pos_type);
    else
        m_types->insert(ndx, neg_type);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    value = (value << 1) + 1;
    m_refs->insert(ndx, value);
}

inline void ColumnMixed::insert_int(std::size_t ndx, int64_t value)
{
    invalidate_subtables();
    insert_int64(ndx, value, mixcol_Int, mixcol_IntNeg);
}

inline void ColumnMixed::insert_double(std::size_t ndx, double value)
{
    invalidate_subtables();
    int64_t val64 = type_punning<int64_t>(value);
    insert_int64(ndx, val64, mixcol_Double, mixcol_DoubleNeg);
}

inline void ColumnMixed::insert_float(std::size_t ndx, float value)
{
    TIGHTDB_ASSERT(ndx <= m_types->size());
    invalidate_subtables();

    // Convert to int32_t first, to ensure we only access 32 bits from the float.
    int32_t val32 = type_punning<int32_t>(value);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    int64_t val64 = (int64_t(val32) << 1) + 1;
    m_refs->insert(ndx, val64);
    m_types->insert(ndx, mixcol_Float);
}

inline void ColumnMixed::insert_bool(std::size_t ndx, bool value)
{
    TIGHTDB_ASSERT(ndx <= m_types->size());
    invalidate_subtables();

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    int64_t v = ((value ? 1 : 0) << 1) + 1;

    m_types->insert(ndx, mixcol_Bool);
    m_refs->insert(ndx, v);
}

inline void ColumnMixed::insert_date(std::size_t ndx, Date value)
{
    TIGHTDB_ASSERT(ndx <= m_types->size());
    invalidate_subtables();

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    int64_t v = (int64_t(value.get_date()) << 1) + 1;

    m_types->insert(ndx, mixcol_Date);
    m_refs->insert(ndx, v);
}

inline void ColumnMixed::insert_string(std::size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx <= m_types->size());
    invalidate_subtables();
    init_data_column();

    std::size_t data_ndx = m_data->size();
    m_data->add_string(value);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    int64_t v = int64_t((uint64_t(data_ndx) << 1) + 1);

    m_types->insert(ndx, mixcol_String);
    m_refs->insert(ndx, v);
}

inline void ColumnMixed::insert_binary(std::size_t ndx, BinaryData value)
{
    TIGHTDB_ASSERT(ndx <= m_types->size());
    invalidate_subtables();
    init_data_column();

    std::size_t data_ndx = m_data->size();
    m_data->add(value);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    int64_t v = int64_t((uint64_t(data_ndx) << 1) + 1);

    m_types->insert(ndx, mixcol_Binary);
    m_refs->insert(ndx, v);
}

inline void ColumnMixed::insert_subtable(std::size_t ndx, const Table* t)
{
    TIGHTDB_ASSERT(ndx <= m_types->size());
    invalidate_subtables();
    ref_type ref;
    if (t) {
        ref = t->clone(m_array->get_alloc()); // Throws
    }
    else {
        ref = Table::create_empty_table(m_array->get_alloc()); // Throws
    }
    m_types->insert(ndx, mixcol_Table);
    m_refs->insert(ndx, ref);
}

} // namespace tightdb
