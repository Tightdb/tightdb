#include <iostream>
#include <iomanip>

#include <tightdb/column_mixed.hpp>

using namespace std;
using namespace tightdb;


ColumnMixed::~ColumnMixed() TIGHTDB_NOEXCEPT
{
    delete m_types;
    delete m_data;
    delete m_binary_data;
    delete m_array;
}


void ColumnMixed::update_from_parent(size_t old_baseline) TIGHTDB_NOEXCEPT
{
    if (!m_array->update_from_parent(old_baseline))
        return;

    m_types->update_from_parent(old_baseline);
    m_data->update_from_parent(old_baseline);
    if (m_binary_data)
        m_binary_data->update_from_parent(old_baseline);
}


void ColumnMixed::create(Allocator& alloc, const Table* table, size_t column_ndx)
{
    m_array = new Array(Array::type_HasRefs, 0, 0, alloc);

    m_types = new Column(Array::type_Normal, alloc);
    m_data  = new RefsColumn(alloc, table, column_ndx);

    m_array->add(m_types->get_ref());
    m_array->add(m_data->get_ref());

    m_types->set_parent(m_array, 0);
    m_data->set_parent(m_array, 1);
}

void ColumnMixed::create(Allocator& alloc, const Table* table, size_t column_ndx,
                         ArrayParent* parent, size_t ndx_in_parent, ref_type ref)
{
    m_array = new Array(ref, parent, ndx_in_parent, alloc);
    TIGHTDB_ASSERT(m_array->size() == 2 || m_array->size() == 3);

    ref_type types_ref = m_array->get_as_ref(0);
    ref_type refs_ref  = m_array->get_as_ref(1);

    m_types = new Column(types_ref, m_array, 0, alloc);
    m_data  = new RefsColumn(alloc, table, column_ndx, m_array, 1, refs_ref);
    TIGHTDB_ASSERT(m_types->size() == m_data->size());

    // Binary column with values that does not fit in refs
    // is only there if needed
    if (m_array->size() == 3) {
        ref_type data_ref = m_array->get_as_ref(2);
        m_binary_data = new ColumnBinary(data_ref, m_array, 2, alloc);
    }
}

void ColumnMixed::init_data_column()
{
    if (m_binary_data)
        return;

    TIGHTDB_ASSERT(m_array->size() == 2);

    // Create new data column for items that do not fit in refs
    m_binary_data = new ColumnBinary(m_array->get_alloc());
    ref_type ref = m_binary_data->get_ref();

    m_array->add(ref);
    m_binary_data->set_parent(m_array, 2);
}

void ColumnMixed::clear_value(size_t ndx, MixedColType new_type)
{
    TIGHTDB_ASSERT(ndx < m_types->size());

    MixedColType type = MixedColType(m_types->get(ndx));
    if (type != mixcol_Int) {
        switch (type) {
            case mixcol_IntNeg:
            case mixcol_Bool:
            case mixcol_Date:
            case mixcol_Float:
            case mixcol_Double:
            case mixcol_DoubleNeg:
                break;
            case mixcol_String:
            case mixcol_Binary: {
                // If item is in middle of the column, we just clear
                // it to avoid having to adjust refs to following items
                // FIXME: this is a leak. We should adjust
                size_t data_ndx = size_t(uint64_t(m_data->get(ndx)) >> 1);
                if (data_ndx == m_binary_data->size()-1) {
                    bool is_last = true;
                    m_binary_data->erase(data_ndx, is_last);
                }
                else {
                    // FIXME: But this will lead to unbounded in-file
                    // leaking in for(;;) { insert_binary(i, ...);
                    // erase(i); }
                    m_binary_data->set(data_ndx, BinaryData());
                }
                break;
            }
            case mixcol_Table: {
                // Delete entire table
                ref_type ref = m_data->get_as_ref(ndx);
                Array top(ref, 0, 0, m_array->get_alloc());
                top.destroy();
                break;
            }
            default:
                TIGHTDB_ASSERT(false);
        }
    }
    if (type != new_type)
        m_types->set(ndx, new_type);
    m_data->set(ndx, 0);
}

void ColumnMixed::erase(size_t ndx, bool is_last)
{
    TIGHTDB_ASSERT(ndx < m_types->size());

    detach_subtable_accessors();

    // Remove refs or binary data
    clear_value(ndx, mixcol_Int);

    m_types->erase(ndx, is_last);
    m_data->erase(ndx, is_last);
}

void ColumnMixed::move_last_over(size_t ndx)
{
    TIGHTDB_ASSERT(ndx+1 < size());
    detach_subtable_accessors();

    // Remove refs or binary data
    clear_value(ndx, mixcol_Int);

    m_types->move_last_over(ndx);
    m_data->move_last_over(ndx);
}

void ColumnMixed::clear()
{
    detach_subtable_accessors();
    m_types->clear();
    m_data->clear();
    if (m_binary_data)
        m_binary_data->clear();
}

DataType ColumnMixed::get_type(size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < m_types->size());
    MixedColType coltype = MixedColType(m_types->get(ndx));
    switch (coltype) {
        case mixcol_IntNeg:    return type_Int;
        case mixcol_DoubleNeg: return type_Double;
        default: return DataType(coltype);   // all others must be in sync with ColumnType
    }
}


void ColumnMixed::set_string(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx < m_types->size());
    detach_subtable_accessors();
    init_data_column();

    MixedColType type = MixedColType(m_types->get(ndx));

    // See if we can reuse data position
    if (type == mixcol_String) {
        size_t data_ndx = size_t(uint64_t(m_data->get(ndx)) >> 1);
        m_binary_data->set_string(data_ndx, value);
    }
    else if (type == mixcol_Binary) {
        size_t data_ndx = size_t(uint64_t(m_data->get(ndx)) >> 1);
        m_binary_data->set_string(data_ndx, value);
        m_types->set(ndx, mixcol_String);
    }
    else {
        // Remove refs or binary data
        clear_value(ndx, mixcol_String);

        // Add value to data column
        size_t data_ndx = m_binary_data->size();
        m_binary_data->add_string(value);

        // Shift value one bit and set lowest bit to indicate that this is not a ref
        int64_t v = int64_t((uint64_t(data_ndx) << 1) + 1);

        m_types->set(ndx, mixcol_String);
        m_data->set(ndx, v);
    }
}

void ColumnMixed::set_binary(size_t ndx, BinaryData value)
{
    TIGHTDB_ASSERT(ndx < m_types->size());
    detach_subtable_accessors();
    init_data_column();

    MixedColType type = MixedColType(m_types->get(ndx));

    // See if we can reuse data position
    if (type == mixcol_String) {
        size_t data_ndx = size_t(uint64_t(m_data->get(ndx)) >> 1);
        m_binary_data->set(data_ndx, value);
        m_types->set(ndx, mixcol_Binary);
    }
    else if (type == mixcol_Binary) {
        size_t data_ndx = size_t(uint64_t(m_data->get(ndx)) >> 1);
        m_binary_data->set(data_ndx, value);
    }
    else {
        // Remove refs or binary data
        clear_value(ndx, mixcol_Binary);

        // Add value to data column
        size_t data_ndx = m_binary_data->size();
        m_binary_data->add(value);

        // Shift value one bit and set lowest bit to indicate that this is not a ref
        int64_t v = int64_t((uint64_t(data_ndx) << 1) + 1);

        m_types->set(ndx, mixcol_Binary);
        m_data->set(ndx, v);
    }
}

bool ColumnMixed::compare_mixed(const ColumnMixed& c) const
{
    const size_t n = size();
    if (c.size() != n)
        return false;

    for (size_t i=0; i<n; ++i) {
        DataType type = get_type(i);
        if (c.get_type(i) != type)
            return false;
        switch (type) {
            case type_Int:
                if (get_int(i) != c.get_int(i)) return false;
                break;
            case type_Bool:
                if (get_bool(i) != c.get_bool(i)) return false;
                break;
            case type_DateTime:
                if (get_datetime(i) != c.get_datetime(i)) return false;
                break;
            case type_Float:
                if (get_float(i) != c.get_float(i)) return false;
                break;
            case type_Double:
                if (get_double(i) != c.get_double(i)) return false;
                break;
            case type_String:
                if (get_string(i) != c.get_string(i)) return false;
                break;
            case type_Binary:
                if (get_binary(i) != c.get_binary(i)) return false;
                break;
            case type_Table: {
                ConstTableRef t1 = get_subtable_ptr(i)->get_table_ref();
                ConstTableRef t2 = c.get_subtable_ptr(i)->get_table_ref();
                if (*t1 != *t2)
                    return false;
                break;
            }
            case type_Mixed:
                TIGHTDB_ASSERT(false);
                break;
        }
    }
    return true;
}

void ColumnMixed::do_detach_subtable_accessors() TIGHTDB_NOEXCEPT
{
    detach_subtable_accessors();
}

ref_type ColumnMixed::create(size_t size, Allocator& alloc)
{
    Array top(alloc);
    top.create(Array::type_HasRefs); // Throws
    try {
        int_fast64_t v = mixcol_Int;
        ref_type types_ref = Column::create(Array::type_Normal, size, v, alloc); // Throws
        try {
            v = types_ref; // FIXME: Dangerous cast: unsigned -> signed
            top.add(v); // Throws
        }
        catch (...) {
            Array::destroy(types_ref, alloc);
            throw;
        }
        v = 1; // 1 + 2*value where value is 0
        ref_type data_ref = Column::create(Array::type_HasRefs, size, v, alloc); // Throws
        try {
            v = data_ref; // FIXME: Dangerous cast: unsigned -> signed
            top.add(v); // Throws
        }
        catch (...) {
            Array::destroy(data_ref, alloc);
            throw;
        }
        return top.get_ref();
    }
    catch (...) {
        top.destroy();
        throw;
    }
}


#ifdef TIGHTDB_DEBUG

void ColumnMixed::Verify() const
{
    m_array->Verify();
    m_types->Verify();
    m_data->Verify();
    if (m_binary_data)
        m_binary_data->Verify();

    // types and refs should be in sync
    size_t types_len = m_types->size();
    size_t refs_len  = m_data->size();
    TIGHTDB_ASSERT(types_len == refs_len);

    // Verify each sub-table
    size_t count = size();
    for (size_t i = 0; i < count; ++i) {
        int64_t v = m_data->get(i);
        if (v == 0 || v & 0x1)
            continue;
        ConstTableRef subtable = m_data->get_subtable(i);
        subtable->Verify();
    }
}

void ColumnMixed::to_dot(ostream& out, StringData title) const
{
    ref_type ref = get_ref();
    out << "subgraph cluster_mixed_column" << ref << " {" << endl;
    out << " label = \"Mixed column";
    if (title.size() != 0)
        out << "\\n'" << title << "'";
    out << "\";" << endl;

    m_array->to_dot(out, "mixed_top");
    m_types->to_dot(out, "types");
    m_data->to_dot(out, "refs");
    if (m_array->size() > 2)
        m_binary_data->to_dot(out, "data");

    // Write sub-tables
    size_t n = size();
    for (size_t i = 0; i != n; ++i) {
        MixedColType type = MixedColType(m_types->get(i));
        if (type != mixcol_Table)
            continue;
        ConstTableRef subtable = m_data->get_subtable(i);
        subtable->to_dot(out);
    }

    out << "}" << endl;
}

void ColumnMixed::dump_node_structure(ostream& out, int level) const
{
    m_types->dump_node_structure(out, level); // FIXME: How to do this?
}

#endif // TIGHTDB_DEBUG
