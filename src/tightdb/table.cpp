#define _CRT_SECURE_NO_WARNINGS
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

#include <tightdb/config.h>
#include <tightdb/table.hpp>
#include <tightdb/alloc_slab.hpp>
#include <tightdb/column.hpp>
#include <tightdb/column_basic.hpp>

#include <tightdb/column_string.hpp>
#include <tightdb/column_string_enum.hpp>
#include <tightdb/column_binary.hpp>
#include <tightdb/column_table.hpp>
#include <tightdb/column_mixed.hpp>
#include <tightdb/index_string.hpp>

using namespace std;
using namespace tightdb;


// -- Table ---------------------------------------------------------------------------------

void Table::init_from_ref(ref_type top_ref, ArrayParent* parent, size_t ndx_in_parent)
{
    // Load from allocated memory
    m_top.init_from_ref(top_ref);
    m_top.set_parent(parent, ndx_in_parent);
    TIGHTDB_ASSERT(m_top.size() == 2);

    ref_type spec_ref    = m_top.get_as_ref(0);
    ref_type columns_ref = m_top.get_as_ref(1);

    init_from_ref(spec_ref, columns_ref, &m_top, 1);
    m_spec.set_parent(&m_top, 0);
}

void Table::init_from_ref(ref_type spec_ref, ref_type columns_ref,
                          ArrayParent* parent, size_t ndx_in_parent)
{
    m_spec.init_from_ref(spec_ref, 0, 0);

    // A table instatiated with a zero-ref is just an empty table
    // but it will have to create itself on first modification
    if (columns_ref != 0) {
        m_columns.init_from_ref(columns_ref);
        cache_columns(); // Also initializes m_size
    }
    m_columns.set_parent(parent, ndx_in_parent);
}

// Create columns as well as column accessors.
void Table::create_columns()
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || m_columns.is_empty()); // only on initial creation

    // Instantiate first if we have an empty table (from zero-ref)
    if (!m_columns.is_attached()) {
        m_columns.create(Array::type_HasRefs);
        m_columns.update_parent();
    }

    size_t subtable_count = 0;
    ColumnType attr = col_attr_None;
    Allocator& alloc = m_columns.get_alloc();

    // Add the newly defined columns
    size_t n = m_spec.get_type_attr_count();
    for (size_t i=0; i<n; ++i) {
        ColumnType type = m_spec.get_type_attr(i);
        size_t ref_pos =  m_columns.size();
        ColumnBase* new_col = 0;

        switch (type) {
            case type_Int:
            case type_Bool:
            case type_Date: {
                Column* c = new Column(Array::type_Normal, alloc);
                m_columns.add(c->get_ref());
                c->set_parent(&m_columns, ref_pos);
                new_col = c;
                break;
            }
            case type_Float: {
                ColumnFloat* c = new ColumnFloat(alloc);
                m_columns.add(c->get_ref());
                c->set_parent(&m_columns, ref_pos);
                new_col = c;
                break;
            }
            case type_Double: {
                ColumnDouble* c = new ColumnDouble(alloc);
                m_columns.add(c->get_ref());
                c->set_parent(&m_columns, ref_pos);
                new_col = c;
                break;
            }
            case type_String: {
                AdaptiveStringColumn* c = new AdaptiveStringColumn(alloc);
                m_columns.add(c->get_ref());
                c->set_parent(&m_columns, ref_pos);
                new_col = c;
                break;
            }
            case type_Binary: {
                ColumnBinary* c = new ColumnBinary(alloc);
                m_columns.add(c->get_ref());
                c->set_parent(&m_columns, ref_pos);
                new_col = c;
                break;
            }
            case type_Table: {
                size_t column_ndx = m_cols.size();
                ref_type subspec_ref = m_spec.get_subspec_ref(subtable_count);
                ColumnTable* c = new ColumnTable(alloc, this, column_ndx, subspec_ref);
                m_columns.add(c->get_ref());
                c->set_parent(&m_columns, ref_pos);
                new_col = c;
                ++subtable_count;
                break;
            }
            case type_Mixed: {
                size_t column_ndx = m_cols.size();
                ColumnMixed* c = new ColumnMixed(alloc, this, column_ndx);
                m_columns.add(c->get_ref());
                c->set_parent(&m_columns, ref_pos);
                new_col = c;
                break;
            }

            // Attributes
            case col_attr_Indexed:
            case col_attr_Unique:
                attr = type;
                continue; // attr prefix column types)

            default:
                TIGHTDB_ASSERT(false);
        }

        // Cache Columns
        m_cols.add(reinterpret_cast<intptr_t>(new_col)); // FIXME: intptr_t is not guaranteed to exists, not even in C++11

        // Atributes on columns may define that they come with an index
        if (attr != col_attr_None) {
            size_t column_ndx = m_cols.size()-1;
            set_index(column_ndx, false);

            attr = col_attr_None;
        }
    }
}


void Table::detach() TIGHTDB_NOEXCEPT
{
    // This prevents the destructor from deallocating the underlying
    // memory structure, and from attempting to notify the parent. It
    // also causes is_attached() to return false.
    m_columns.set_parent(0,0);

    // Detach all offspring accessors
    detach_subtable_accessors();

    destroy_column_accessors();
}


void Table::detach_subtable_accessors() TIGHTDB_NOEXCEPT
{
    size_t n = m_cols.size();
    for (size_t i=0; i<n; ++i) {
        ColumnBase* c = reinterpret_cast<ColumnBase*>(uintptr_t(m_cols.get(i)));
        c->detach_subtable_accessors_virtual();
    }
}


void Table::instantiate_before_change()
{
    // Empty (zero-ref'ed) tables need to be instantiated before first modification
    if (!m_columns.is_attached())
        create_columns();
}

void Table::cache_columns()
{
    TIGHTDB_ASSERT(m_cols.is_empty()); // only done on creation

    Allocator& alloc = m_columns.get_alloc();
    ColumnType attr = col_attr_None;
    size_t num_rows = size_t(-1);
    size_t ndx_in_parent = 0;
    size_t subtable_count = 0;

    // Cache columns
    size_t num_entries_in_spec = m_spec.get_type_attr_count();
    for (size_t i = 0; i < num_entries_in_spec; ++i) {
        ColumnType type = m_spec.get_type_attr(i);
        ref_type ref = m_columns.get_as_ref(ndx_in_parent);

        ColumnBase* new_col = 0;
        size_t colsize = size_t(-1);
        switch (type) {
            case type_Int:
            case type_Bool:
            case type_Date: {
                Column* c = new Column(ref, &m_columns, ndx_in_parent, alloc);
                colsize = c->size();
                new_col = c;
                break;
            }
            case type_Float: {
                ColumnFloat* c = new ColumnFloat(ref, &m_columns, ndx_in_parent, alloc);
                colsize = c->size();
                new_col = c;
            }
                break;
            case type_Double: {
                ColumnDouble* c = new ColumnDouble(ref, &m_columns, ndx_in_parent, alloc);
                colsize = c->size();
                new_col = c;
                break;
            }
            case type_String: {
                AdaptiveStringColumn* c =
                    new AdaptiveStringColumn(ref, &m_columns, ndx_in_parent, alloc);
                colsize = c->size();
                new_col = c;
                break;
            }
            case type_Binary: {
                ColumnBinary* c = new ColumnBinary(ref, &m_columns, ndx_in_parent, alloc);
                colsize = c->size();
                new_col = c;
                break;
            }
            case col_type_StringEnum: {
                ref_type values_ref = m_columns.get_as_ref(ndx_in_parent+1);
                ColumnStringEnum* c =
                    new ColumnStringEnum(ref, values_ref, &m_columns, ndx_in_parent, alloc);
                colsize = c->size();
                new_col = c;
                ++ndx_in_parent; // advance one matchcount pos to account for keys/values pair
                break;
            }
            case type_Table: {
                size_t column_ndx = m_cols.size();
                ref_type spec_ref = m_spec.get_subspec_ref(subtable_count);
                ColumnTable* c = new ColumnTable(alloc, this, column_ndx, &m_columns, ndx_in_parent,
                                                 spec_ref, ref);
                colsize = c->size();
                new_col = c;
                ++subtable_count;
                break;
            }
            case type_Mixed: {
                size_t column_ndx = m_cols.size();
                ColumnMixed* c =
                    new ColumnMixed(alloc, this, column_ndx, &m_columns, ndx_in_parent, ref);
                colsize = c->size();
                new_col = c;
                break;
            }

            case col_attr_Indexed:
                // Attributes (prefixing column types)
                attr = type;
                continue;

            case col_type_Reserved1:
            case col_type_Reserved4:
            case col_attr_Unique:
            case col_attr_Sorted:
            case col_attr_None:
                // These have no function yet and are therefore
                // unexpected.
                break;
        }

        TIGHTDB_ASSERT(new_col);

        m_cols.add(reinterpret_cast<intptr_t>(new_col)); // FIXME: intptr_t is not guaranteed to exists, even in C++11

        // Atributes on columns may define that they come with an index
        if (attr != col_attr_None) {
            TIGHTDB_ASSERT(attr == col_attr_Indexed); // only attribute supported for now
            TIGHTDB_ASSERT(type == col_type_String ||
                           type == col_type_StringEnum);  // index only for strings

            size_t pndx = ndx_in_parent + 1;
            ref_type index_ref = m_columns.get_as_ref(pndx);
            new_col->set_index_ref(index_ref, &m_columns, pndx);

            ++ndx_in_parent; // advance one matchcount pos to account for index
            attr = col_attr_None;
        }

        // Set table size
        // (and verify that all column are same size)
        if (num_rows == size_t(-1)) num_rows = colsize;
        else TIGHTDB_ASSERT(num_rows == colsize);

        ++ndx_in_parent;
    }

    if (num_rows != size_t(-1)) m_size = num_rows;
}

void Table::destroy_column_accessors() TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(m_cols.is_attached());

    size_t count = m_cols.size();
    for (size_t i = 0; i < count; ++i) {
        ColumnBase* column = reinterpret_cast<ColumnBase*>(m_cols.get(i));
        delete column;
    }
    m_cols.destroy();
}

Table::~Table() TIGHTDB_NOEXCEPT
{
#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().on_table_destroyed();
#endif

    if (!is_attached()) {
        // This table has been detached.
        TIGHTDB_ASSERT(m_ref_count == 0);
        return;
    }

    if (!m_top.is_attached()) {
        // This is a table with a shared spec, and its lifetime is
        // managed by reference counting, so we must let our parent
        // know about our demise.
        ArrayParent* parent = m_columns.get_parent();
        TIGHTDB_ASSERT(parent);
        TIGHTDB_ASSERT(m_ref_count == 0);
        TIGHTDB_ASSERT(dynamic_cast<Parent*>(parent));
        static_cast<Parent*>(parent)->child_accessor_destroyed(m_columns.get_ndx_in_parent());
        destroy_column_accessors();
        return;
    }

    // This is a table with an independent spec.
    if (ArrayParent* parent = m_top.get_parent()) {
        // This is a table whose lifetime is managed by reference
        // counting, so we must let our parent know about our demise.
        TIGHTDB_ASSERT(m_ref_count == 0);
        TIGHTDB_ASSERT(dynamic_cast<Parent*>(parent));
        static_cast<Parent*>(parent)->child_accessor_destroyed(m_top.get_ndx_in_parent());
        destroy_column_accessors();
        return;
    }

    // This is a freestanding table, so we are responsible for
    // deallocating the underlying memory structure. If the table was
    // created using the public table constructor (a stack allocated
    // table) then the reference count must be strictly positive at
    // this point. Otherwise the table has been created using
    // LangBindHelper::new_table(), and then the reference count must
    // be zero, because that is what has caused the destructor to be
    // called. In the latter case, there can be no subtables to
    // detach, because attached ones would have kept their parent
    // alive.
    if (0 < m_ref_count) {
        detach();
    }
    else {
        destroy_column_accessors();
    }
    m_top.destroy();
}

size_t Table::add_column(DataType type, StringData name)
{
    TIGHTDB_ASSERT(!has_shared_spec());
    detach_subtable_accessors();

    // Update spec
    m_spec.add_column(type, name);

    // Create column and add cached instance
    size_t column_ndx = do_add_column(type);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().add_column(type, name); // Throws
#endif

    return column_ndx;
}

size_t Table::add_subcolumn(const vector<size_t>& column_path, DataType type, StringData name)
{
    TIGHTDB_ASSERT(!column_path.empty());
    TIGHTDB_ASSERT(!has_shared_spec());
    detach_subtable_accessors();

    // Update spec
    size_t column_ndx = m_spec.add_subcolumn(column_path, type, name);

    // Update existing tables
    do_add_subcolumn(column_path, 0, type);

#ifdef TIGHTDB_ENABLE_REPLICATION
    // FIXME: Implement this!
    //transact_log().add_column(type, name); // Throws
#endif

    return column_ndx;
}

size_t Table::do_add_column(DataType type)
{
    size_t count      = size();
    size_t column_ndx = m_cols.size();

    ColumnBase* new_col = 0;
    Allocator& alloc = m_columns.get_alloc();

    switch (type) {
        case type_Int:
        case type_Bool:
        case type_Date: {
            Column* c = new Column(Array::type_Normal, alloc);
            m_columns.add(c->get_ref());
            c->set_parent(&m_columns, m_columns.size()-1);
            new_col = c;
            c->fill(count);
            break;
        }
        case type_Float: {
            ColumnFloat* c = new ColumnFloat(alloc);
            m_columns.add(c->get_ref());
            c->set_parent(&m_columns, m_columns.size()-1);
            new_col = c;
            c->fill(count);
            break;
        }
        case type_Double: {
            ColumnDouble* c = new ColumnDouble(alloc);
            m_columns.add(c->get_ref());
            c->set_parent(&m_columns, m_columns.size()-1);
            new_col = c;
            c->fill(count);
            break;
        }
        case type_String: {
            AdaptiveStringColumn* c = new AdaptiveStringColumn(alloc);
            m_columns.add(c->get_ref());
            c->set_parent(&m_columns, m_columns.size()-1);
            new_col = c;
            c->fill(count);
            break;
        }
        case type_Binary: {
            ColumnBinary* c = new ColumnBinary(alloc);
            m_columns.add(c->get_ref());
            c->set_parent(&m_columns, m_columns.size()-1);
            new_col = c;
            c->fill(count);
            break;
        }
        case type_Table: {
            ref_type spec_ref = m_spec.get_subspec_ref(m_spec.get_num_subspecs()-1);
            ColumnTable* c = new ColumnTable(alloc, this, column_ndx, spec_ref);
            m_columns.add(c->get_ref());
            c->set_parent(&m_columns, m_columns.size()-1);
            new_col = c;
            c->fill(count);
            break;
        }
        case type_Mixed: {
            ColumnMixed* c = new ColumnMixed(alloc, this, column_ndx);
            m_columns.add(c->get_ref());
            c->set_parent(&m_columns, m_columns.size()-1);
            new_col = c;
            c->fill(count);
            break;
        }
    }

    m_cols.add(reinterpret_cast<intptr_t>(new_col)); // FIXME: intptr_t is not guaranteed to exists, even in C++11

    return column_ndx;
}

void Table::do_add_subcolumn(const vector<size_t>& column_path, size_t column_path_ndx,
                             DataType type)
{
    TIGHTDB_ASSERT(1 <= column_path.size());
    TIGHTDB_ASSERT(column_path_ndx <= column_path.size() - 1);

    size_t column_ndx = column_path[column_path_ndx];
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == col_type_Table);

    ColumnTable& subtables = get_column_table(column_ndx);
    size_t num_rows = size();
    bool modify_level = column_path.size() <= column_path_ndx + 1;
    if (modify_level) {
        Allocator& alloc = m_columns.get_alloc();
        for (size_t i = 0; i < num_rows; ++i) {
            ref_type subtable_ref = subtables.get_as_ref(i);
            if (subtable_ref == 0) continue; // Degenerate empty subatble
            size_t subtable_size = subtables.get_subtable_size(i);
            ref_type column_ref = create_column(type, subtable_size, alloc);
            Array subcolumns(subtable_ref, &subtables, i, alloc);
            subcolumns.add(column_ref);
        }
    }
    else {
        for (size_t i = 0; i < num_rows; ++i) {
            if (subtables.get_as_ref(i) == 0) continue; // Degenerate empty subatble
            TableRef subtable = subtables.get_subtable_ptr(i)->get_table_ref();
            subtable->do_add_subcolumn(column_path, column_path_ndx+1, type);
        }
    }
}

void Table::remove_column(size_t column_ndx)
{
    TIGHTDB_ASSERT(!has_shared_spec());
    detach_subtable_accessors();

    Spec::ColumnInfo info;
    m_spec.get_column_info(column_ndx, info);

    // Update Spec
    m_spec.remove_column(column_ndx);

    // Remove the column from this table
    do_remove_column(m_columns, info);

    // Delete the cached column
    delete reinterpret_cast<ColumnBase*>(m_cols.get(column_ndx));
    m_cols.erase(column_ndx);

    // Update 'indexes in parent' for subsequent columns
    adjust_column_ndx_in_parent(column_ndx, -(info.m_has_index ? 2 : 1));

    // If there are no columns left, mark the table as empty
    if (get_column_count() == 0)
        m_size = 0;
}

void Table::remove_subcolumn(const vector<size_t>& column_path)
{
    TIGHTDB_ASSERT(2 <= column_path.size());
    TIGHTDB_ASSERT(!has_shared_spec());
    detach_subtable_accessors();

    Spec::ColumnInfo info;
    m_spec.get_subcolumn_info(column_path, 0, info);

    // Update Spec
    m_spec.remove_column(column_path);

    // Remove the column from all tables using the affected subspec
    do_remove_subcolumn(column_path, 0, info);
}


void Table::do_remove_column(Array& column_refs, const Spec::ColumnInfo& info)
{
    // Remove from column list
    ref_type column_ref = column_refs.get_as_ref(info.m_column_ref_ndx);
    Array::destroy(column_ref, column_refs.get_alloc());
    column_refs.erase(info.m_column_ref_ndx);

    // If the column had an index we have to remove that as well
    if (info.m_has_index) {
        ref_type index_ref = column_refs.get_as_ref(info.m_column_ref_ndx);
        Array::destroy(index_ref, column_refs.get_alloc());
        column_refs.erase(info.m_column_ref_ndx);
    }
}

void Table::do_remove_subcolumn(const vector<size_t>& column_path, size_t column_path_ndx,
                                const Spec::ColumnInfo& info)
{
    TIGHTDB_ASSERT(2 <= column_path.size());
    TIGHTDB_ASSERT(column_path_ndx <= column_path.size() - 2);

    size_t column_ndx = column_path[column_path_ndx];
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == col_type_Table);

    ColumnTable& subtables = get_column_table(column_ndx);
    size_t num_rows = size();
    bool modify_level = column_path.size() <= column_path_ndx + 2;
    if (modify_level) {
        Allocator& alloc = m_columns.get_alloc();
        for (size_t i = 0; i < num_rows; ++i) {
            ref_type subtable_ref = subtables.get_as_ref(i);
            if (subtable_ref == 0) continue; // Degenerate empty subatble
            Array subcolumns(subtable_ref, &subtables, i, alloc);
            do_remove_column(subcolumns, info);
        }
    }
    else {
        for (size_t i = 0; i < num_rows; ++i) {
            if (subtables.get_as_ref(i) == 0) continue; // Degenerate empty subatble
            TableRef subtable = subtables.get_subtable_ptr(i)->get_table_ref();
            subtable->do_remove_subcolumn(column_path, column_path_ndx+1, info);
        }
    }
}


void Table::rename_column(size_t column_ndx, StringData name)
{
    TIGHTDB_ASSERT(!has_shared_spec());
    detach_subtable_accessors();
    m_spec.rename_column(column_ndx, name);
}

void Table::rename_subcolumn(const vector<size_t>& column_path, StringData name)
{
    TIGHTDB_ASSERT(2 <= column_path.size());
    TIGHTDB_ASSERT(!has_shared_spec());
    detach_subtable_accessors();
    m_spec.rename_column(column_path, name);
}


bool Table::has_index(size_t column_ndx) const
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    const ColumnBase& col = get_column_base(column_ndx);
    return col.has_index();
}

void Table::set_index(size_t column_ndx, bool update_spec)
{
    TIGHTDB_ASSERT(!has_shared_spec());
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    if (has_index(column_ndx)) return;

    ColumnType ct = get_real_column_type(column_ndx);
    Spec::ColumnInfo info;
    m_spec.get_column_info(column_ndx, info);
    size_t column_pos = info.m_column_ref_ndx;
    ref_type index_ref = 0;

    if (ct == col_type_String) {
        AdaptiveStringColumn& col = get_column_string(column_ndx);

        // Create the index
        StringIndex& index = col.create_index();
        index.set_parent(&m_columns, column_pos+1);
        index_ref = index.get_ref();
    }
    else if (ct == col_type_StringEnum) {
        ColumnStringEnum& col = get_column_string_enum(column_ndx);

        // Create the index
        StringIndex& index = col.create_index();
        index.set_parent(&m_columns, column_pos+1);
        index_ref = index.get_ref();
    }
    else {
        TIGHTDB_ASSERT(false);
        return;
    }

    // Insert ref into columns list after the owning column
    m_columns.insert(column_pos+1, index_ref);
    adjust_column_ndx_in_parent(column_ndx+1, 1);

    // Update spec
    if (update_spec)
        m_spec.set_column_attr(column_ndx, col_attr_Indexed);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().add_index_to_column(column_ndx); // Throws
#endif
}



ColumnBase& Table::get_column_base(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < get_column_count());
    instantiate_before_change();
    TIGHTDB_ASSERT(m_cols.size() == get_column_count());
    return *reinterpret_cast<ColumnBase*>(m_cols.get(ndx));
}

const ColumnBase& Table::get_column_base(size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < get_column_count());
    TIGHTDB_ASSERT(m_cols.size() == get_column_count());
    return *reinterpret_cast<ColumnBase*>(m_cols.get(ndx));
}


void Table::validate_column_type(const ColumnBase& column, ColumnType coltype, size_t ndx) const
{
    if (coltype == col_type_Int || coltype == col_type_Date || coltype == col_type_Bool) {
        TIGHTDB_ASSERT(column.IsIntColumn());
    }
    else {
        TIGHTDB_ASSERT(coltype == get_real_column_type(ndx));
    }
    static_cast<void>(column);
    static_cast<void>(ndx);
}


ref_type Table::create_column(DataType column_type, size_t num_default_values, Allocator& alloc)
{
    switch (column_type) {
        case type_Int:
        case type_Bool:
        case type_Date: {
            Column c(Array::type_Normal, alloc);
            c.fill(num_default_values);
            return c.get_ref();
        }
        case type_Float: {
            ColumnFloat c(alloc);
            c.fill(num_default_values);
            return c.get_ref();
        }
        case type_Double: {
            ColumnDouble c(alloc);
            c.fill(num_default_values);
            return c.get_ref();
        }
        case type_String: {
            AdaptiveStringColumn c(alloc);
            c.fill(num_default_values);
            return c.get_ref();
        }
        case type_Binary: {
            ColumnBinary c(alloc);
            c.fill(num_default_values);
            return c.get_ref();
        }
        case type_Table:
            return ColumnTable::create(num_default_values, alloc);
        case type_Mixed: {
            return ColumnMixed::create(num_default_values, alloc);
        }
    }

    TIGHTDB_ASSERT(false);
    return 0;
}


ref_type Table::clone_columns(Allocator& alloc) const
{
    Array new_columns(Array::type_HasRefs, 0, 0, alloc);
    size_t n = get_column_count();
    for (size_t i=0; i<n; ++i) {
        ref_type new_col_ref;
        const ColumnBase* col = &get_column_base(i);
        if (const ColumnStringEnum* enum_col = dynamic_cast<const ColumnStringEnum*>(col)) {
            AdaptiveStringColumn new_col(alloc);
            // FIXME: Should be optimized with something like
            // new_col.add(seq_tree_accessor.begin(),
            // seq_tree_accessor.end())
            size_t n2 = enum_col->size();
            for (size_t i2=0; i2<n2; ++i2)
                new_col.add(enum_col->get(i));
            new_col_ref = new_col.get_ref();
        }
        else {
            const Array& root = *col->get_root_array();
            new_col_ref = root.clone(alloc); // Throws
        }
        new_columns.add(new_col_ref);
    }
    return new_columns.get_ref();
}


ref_type Table::clone(Allocator& alloc) const
{
    if (m_top.is_attached())
        return m_top.clone(alloc); // Throws

    Array new_top(Array::type_HasRefs, 0, 0, alloc); // Throws
    new_top.add(m_spec.m_top.clone(alloc)); // Throws
    new_top.add(m_columns.clone(alloc)); // Throws
    return new_top.get_ref();
}



// TODO: get rid of the Column* template parameter

Column& Table::get_column(size_t ndx)
{
    return get_column<Column, col_type_Int>(ndx);
}

const Column& Table::get_column(size_t ndx) const TIGHTDB_NOEXCEPT
{
    return get_column<Column, col_type_Int>(ndx);
}

AdaptiveStringColumn& Table::get_column_string(size_t ndx)
{
    return get_column<AdaptiveStringColumn, col_type_String>(ndx);
}

const AdaptiveStringColumn& Table::get_column_string(size_t ndx) const TIGHTDB_NOEXCEPT
{
    return get_column<AdaptiveStringColumn, col_type_String>(ndx);
}

ColumnStringEnum& Table::get_column_string_enum(size_t ndx)
{
    return get_column<ColumnStringEnum, col_type_StringEnum>(ndx);
}

const ColumnStringEnum& Table::get_column_string_enum(size_t ndx) const TIGHTDB_NOEXCEPT
{
    return get_column<ColumnStringEnum, col_type_StringEnum>(ndx);
}

ColumnFloat& Table::get_column_float(size_t ndx)
{
    return get_column<ColumnFloat, col_type_Float>(ndx);
}

const ColumnFloat& Table::get_column_float(size_t ndx) const TIGHTDB_NOEXCEPT
{
    return get_column<ColumnFloat, col_type_Float>(ndx);
}

ColumnDouble& Table::get_column_double(size_t ndx)
{
    return get_column<ColumnDouble, col_type_Double>(ndx);
}

const ColumnDouble& Table::get_column_double(size_t ndx) const TIGHTDB_NOEXCEPT
{
    return get_column<ColumnDouble, col_type_Double>(ndx);
}

ColumnBinary& Table::get_column_binary(size_t ndx)
{
    return get_column<ColumnBinary, col_type_Binary>(ndx);
}

const ColumnBinary& Table::get_column_binary(size_t ndx) const TIGHTDB_NOEXCEPT
{
    return get_column<ColumnBinary, col_type_Binary>(ndx);
}

ColumnTable &Table::get_column_table(size_t ndx)
{
    return get_column<ColumnTable, col_type_Table>(ndx);
}

const ColumnTable &Table::get_column_table(size_t ndx) const TIGHTDB_NOEXCEPT
{
    return get_column<ColumnTable, col_type_Table>(ndx);
}

ColumnMixed& Table::get_column_mixed(size_t ndx)
{
    return get_column<ColumnMixed, col_type_Mixed>(ndx);
}

const ColumnMixed& Table::get_column_mixed(size_t ndx) const TIGHTDB_NOEXCEPT
{
    return get_column<ColumnMixed, col_type_Mixed>(ndx);
}


size_t Table::add_empty_row(size_t num_rows)
{
    size_t n = get_column_count();
    for (size_t i=0; i<n; ++i) {
        ColumnBase& column = get_column_base(i);
        for (size_t j=0; j<num_rows; ++j) {
            column.add();
        }
    }

    // Return index of first new added row
    size_t new_ndx = m_size;
    m_size += num_rows;

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_empty_rows(new_ndx, 1); // Throws
#endif

    return new_ndx;
}

void Table::insert_empty_row(size_t ndx, size_t num_rows)
{
    size_t ndx2 = ndx + num_rows; // FIXME: Should we check for overflow?
    size_t n = get_column_count();
    for (size_t i=0; i<n; ++i) {
        ColumnBase& column = get_column_base(i);
        // FIXME: This could maybe be optimized by passing 'num_rows' to column.insert()
        for (size_t j=ndx; j<ndx2; ++j) {
            column.insert(j);
        }
    }

    m_size += num_rows;

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_empty_rows(ndx, num_rows); // Throws
#endif
}

void Table::clear()
{
    size_t count = get_column_count();
    for (size_t i = 0; i < count; ++i) {
        ColumnBase& column = get_column_base(i);
        column.clear();
    }
    m_size = 0;

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().clear_table(); // Throws
#endif
}

void Table::remove(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < m_size);

    size_t count = get_column_count();
    for (size_t i = 0; i < count; ++i) {
        ColumnBase& column = get_column_base(i);
        column.erase(ndx);
    }
    --m_size;

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().remove_row(ndx); // Throws
#endif
}

void Table::move_last_over(size_t ndx)
{
    TIGHTDB_ASSERT(ndx+1 < m_size);

    size_t count = get_column_count();
    for (size_t i = 0; i < count; ++i) {
        ColumnBase& column = get_column_base(i);
        column.move_last_over(ndx);
    }
    --m_size;

#ifdef TIGHTDB_ENABLE_REPLICATION
    //TODO: transact_log().move_last_over(ndx); // Throws
#endif
}


void Table::insert_subtable(size_t col_ndx, size_t row_ndx, const Table* table)
{
    TIGHTDB_ASSERT(col_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(col_ndx) == col_type_Table);
    TIGHTDB_ASSERT(row_ndx <= m_size);

    ColumnTable& subtables = get_column_table(col_ndx);
    subtables.insert(row_ndx, table);

    // FIXME: Replication is not yet able to handle copying insertion of non-empty tables.
#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_value(col_ndx, row_ndx, Replication::subtable_tag()); // Throws
#endif
}


void Table::set_subtable(size_t col_ndx, size_t row_ndx, const Table* table)
{
    TIGHTDB_ASSERT(col_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(col_ndx) == col_type_Table);
    TIGHTDB_ASSERT(row_ndx < m_size);

    ColumnTable& subtables = get_column_table(col_ndx);
    subtables.set(row_ndx, table);

    // FIXME: Replication is not yet able to handle copying insertion of non-empty tables.
#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(col_ndx, row_ndx, Replication::subtable_tag()); // Throws
#endif
}


void Table::insert_mixed_subtable(size_t col_ndx, size_t row_ndx, const Table* t)
{
    TIGHTDB_ASSERT(col_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(col_ndx) == col_type_Mixed);
    TIGHTDB_ASSERT(row_ndx <= m_size);

    ColumnMixed& mixed_col = get_column_mixed(col_ndx);
    mixed_col.insert_subtable(row_ndx, t);

    // FIXME: Replication is not yet able to handle copuing insertion of non-empty tables.
#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_value(col_ndx, row_ndx, Replication::subtable_tag()); // Throws
#endif
}


void Table::set_mixed_subtable(size_t col_ndx, size_t row_ndx, const Table* t)
{
    TIGHTDB_ASSERT(col_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(col_ndx) == col_type_Mixed);
    TIGHTDB_ASSERT(row_ndx < m_size);

    ColumnMixed& mixed_col = get_column_mixed(col_ndx);
    mixed_col.set_subtable(row_ndx, t);

    // FIXME: Replication is not yet able to handle copying assignment of non-empty tables.
#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(col_ndx, row_ndx, Replication::subtable_tag()); // Throws
#endif
}


Table* Table::get_subtable_ptr(size_t col_idx, size_t row_idx)
{
    TIGHTDB_ASSERT(col_idx < get_column_count());
    TIGHTDB_ASSERT(row_idx < m_size);

    ColumnType type = get_real_column_type(col_idx);
    if (type == col_type_Table) {
        ColumnTable& subtables = get_column_table(col_idx);
        return subtables.get_subtable_ptr(row_idx);
    }
    if (type == col_type_Mixed) {
        ColumnMixed& subtables = get_column_mixed(col_idx);
        return subtables.get_subtable_ptr(row_idx);
    }
    TIGHTDB_ASSERT(false);
    return 0;
}

const Table* Table::get_subtable_ptr(size_t col_idx, size_t row_idx) const
{
    TIGHTDB_ASSERT(col_idx < get_column_count());
    TIGHTDB_ASSERT(row_idx < m_size);

    ColumnType type = get_real_column_type(col_idx);
    if (type == col_type_Table) {
        const ColumnTable& subtables = get_column_table(col_idx);
        return subtables.get_subtable_ptr(row_idx);
    }
    if (type == col_type_Mixed) {
        const ColumnMixed& subtables = get_column_mixed(col_idx);
        return subtables.get_subtable_ptr(row_idx);
    }
    TIGHTDB_ASSERT(false);
    return 0;
}

size_t Table::get_subtable_size(size_t col_idx, size_t row_idx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(col_idx < get_column_count());
    TIGHTDB_ASSERT(row_idx < m_size);

    ColumnType type = get_real_column_type(col_idx);
    if (type == col_type_Table) {
        const ColumnTable& subtables = get_column_table(col_idx);
        return subtables.get_subtable_size(row_idx);
    }
    if (type == col_type_Mixed) {
        const ColumnMixed& subtables = get_column_mixed(col_idx);
        return subtables.get_subtable_size(row_idx);
    }
    TIGHTDB_ASSERT(false);
    return 0;
}

void Table::clear_subtable(size_t col_idx, size_t row_idx)
{
    TIGHTDB_ASSERT(col_idx < get_column_count());
    TIGHTDB_ASSERT(row_idx <= m_size);

    ColumnType type = get_real_column_type(col_idx);
    if (type == col_type_Table) {
        ColumnTable& subtables = get_column_table(col_idx);
        subtables.set(row_idx, 0);

#ifdef TIGHTDB_ENABLE_REPLICATION
        transact_log().set_value(col_idx, row_idx, Replication::subtable_tag()); // Throws
#endif
    }
    else if (type == col_type_Mixed) {
        ColumnMixed& subtables = get_column_mixed(col_idx);
        subtables.set_subtable(row_idx, 0);

#ifdef TIGHTDB_ENABLE_REPLICATION
        transact_log().set_value(col_idx, row_idx, Mixed(Mixed::subtable_tag())); // Throws
#endif
    }
    else {
        TIGHTDB_ASSERT(false);
    }
}


int64_t Table::get_int(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    const Column& column = get_column(column_ndx);
    return column.get(ndx);
}

void Table::set_int(size_t column_ndx, size_t ndx, int64_t value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    Column& column = get_column(column_ndx);
    column.set(ndx, value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(column_ndx, ndx, value); // Throws
#endif
}

void Table::add_int(size_t column_ndx, int64_t value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == col_type_Int);
    get_column(column_ndx).Increment64(value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().add_int_to_column(column_ndx, value); // Throws
#endif
}


bool Table::get_bool(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == col_type_Bool);
    TIGHTDB_ASSERT(ndx < m_size);

    const Column& column = get_column(column_ndx);
    return column.get(ndx) != 0;
}

void Table::set_bool(size_t column_ndx, size_t ndx, bool value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == col_type_Bool);
    TIGHTDB_ASSERT(ndx < m_size);

    Column& column = get_column(column_ndx);
    column.set(ndx, value ? 1 : 0);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(column_ndx, ndx, int(value)); // Throws
#endif
}

Date Table::get_date(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == col_type_Date);
    TIGHTDB_ASSERT(ndx < m_size);

    const Column& column = get_column(column_ndx);
    return time_t(column.get(ndx));
}

void Table::set_date(size_t column_ndx, size_t ndx, Date value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == col_type_Date);
    TIGHTDB_ASSERT(ndx < m_size);

    Column& column = get_column(column_ndx);
    column.set(ndx, int64_t(value.get_date()));

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(column_ndx, ndx, value.get_date()); // Throws
#endif
}

void Table::insert_int(size_t column_ndx, size_t ndx, int64_t value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx <= m_size);

    Column& column = get_column(column_ndx);
    column.insert(ndx, value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_value(column_ndx, ndx, value); // Throws
#endif
}


float Table::get_float(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    const ColumnFloat& column = get_column_float(column_ndx);
    return column.get(ndx);
}

void Table::set_float(size_t column_ndx, size_t ndx, float value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    ColumnFloat& column = get_column_float(column_ndx);
    column.set(ndx, value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(column_ndx, ndx, value); // Throws
#endif
}

void Table::insert_float(size_t column_ndx, size_t ndx, float value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx <= m_size);

    ColumnFloat& column = get_column_float(column_ndx);
    column.insert(ndx, value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_value(column_ndx, ndx, value); // Throws
#endif
}


double Table::get_double(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    const ColumnDouble& column = get_column_double(column_ndx);
    return column.get(ndx);
}

void Table::set_double(size_t column_ndx, size_t ndx, double value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    ColumnDouble& column = get_column_double(column_ndx);
    column.set(ndx, value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(column_ndx, ndx, value); // Throws
#endif
}

void Table::insert_double(size_t column_ndx, size_t ndx, double value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx <= m_size);

    ColumnDouble& column = get_column_double(column_ndx);
    column.insert(ndx, value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_value(column_ndx, ndx, value); // Throws
#endif
}


StringData Table::get_string(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < m_columns.size());
    TIGHTDB_ASSERT(ndx < m_size);

    ColumnType type = get_real_column_type(column_ndx);
    if (type == col_type_String) {
        const AdaptiveStringColumn& column = get_column_string(column_ndx);
        return column.get(ndx);
    }

    TIGHTDB_ASSERT(type == col_type_StringEnum);
    const ColumnStringEnum& column = get_column_string_enum(column_ndx);
    return column.get(ndx);
}

void Table::set_string(size_t column_ndx, size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    ColumnType type = get_real_column_type(column_ndx);
    if (type == col_type_String) {
        AdaptiveStringColumn& column = get_column_string(column_ndx);
        column.set(ndx, value);
    }
    else {
        TIGHTDB_ASSERT(type == col_type_StringEnum);
        ColumnStringEnum& column = get_column_string_enum(column_ndx);
        column.set(ndx, value);
    }

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(column_ndx, ndx, value); // Throws
#endif
}

void Table::insert_string(size_t column_ndx, size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx <= m_size);

    ColumnType type = get_real_column_type(column_ndx);
    if (type == col_type_String) {
        AdaptiveStringColumn& column = get_column_string(column_ndx);
        column.insert(ndx, value);
    }
    else {
        TIGHTDB_ASSERT(type == col_type_StringEnum);
        ColumnStringEnum& column = get_column_string_enum(column_ndx);
        column.insert(ndx, value);
    }

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_value(column_ndx, ndx, value); // Throws
#endif
}


BinaryData Table::get_binary(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < m_columns.size());
    TIGHTDB_ASSERT(ndx < m_size);

    const ColumnBinary& column = get_column_binary(column_ndx);
    return column.get(ndx);
}

void Table::set_binary(size_t column_ndx, size_t ndx, BinaryData value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    ColumnBinary& column = get_column_binary(column_ndx);
    column.set(ndx, value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(column_ndx, ndx, value); // Throws
#endif
}

void Table::insert_binary(size_t column_ndx, size_t ndx, BinaryData value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx <= m_size);

    ColumnBinary& column = get_column_binary(column_ndx);
    column.insert(ndx, value);

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_value(column_ndx, ndx, value); // Throws
#endif
}


Mixed Table::get_mixed(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < m_columns.size());
    TIGHTDB_ASSERT(ndx < m_size);

    const ColumnMixed& column = get_column_mixed(column_ndx);

    DataType type = column.get_type(ndx);
    switch (type) {
        case type_Int:
            return Mixed(column.get_int(ndx));
        case type_Bool:
            return Mixed(column.get_bool(ndx));
        case type_Date:
            return Mixed(Date(column.get_date(ndx)));
        case type_Float:
            return Mixed(column.get_float(ndx));
        case type_Double:
            return Mixed(column.get_double(ndx));
        case type_String:
            return Mixed(column.get_string(ndx)); // Throws
        case type_Binary:
            return Mixed(column.get_binary(ndx)); // Throws
        case type_Table:
            return Mixed::subtable_tag();
        case type_Mixed:
            break;
    }
    TIGHTDB_ASSERT(false);
    return Mixed(int64_t(0));
}

DataType Table::get_mixed_type(size_t column_ndx, size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(column_ndx < m_columns.size());
    TIGHTDB_ASSERT(ndx < m_size);

    const ColumnMixed& column = get_column_mixed(column_ndx);
    return column.get_type(ndx);
}

void Table::set_mixed(size_t column_ndx, size_t ndx, Mixed value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx < m_size);

    ColumnMixed& column = get_column_mixed(column_ndx);
    DataType type = value.get_type();

    switch (type) {
        case type_Int:
            column.set_int(ndx, value.get_int());
            break;
        case type_Bool:
            column.set_bool(ndx, value.get_bool());
            break;
        case type_Date:
            column.set_date(ndx, value.get_date());
            break;
        case type_Float:
            column.set_float(ndx, value.get_float());
            break;
        case type_Double:
            column.set_double(ndx, value.get_double());
            break;
        case type_String:
            column.set_string(ndx, value.get_string());
            break;
        case type_Binary:
            column.set_binary(ndx, value.get_binary());
            break;
        case type_Table:
            column.set_subtable(ndx, 0);
            break;
        case type_Mixed:
            TIGHTDB_ASSERT(false);
            break;
    }

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().set_value(column_ndx, ndx, value); // Throws
#endif
}

void Table::insert_mixed(size_t column_ndx, size_t ndx, Mixed value)
{
    TIGHTDB_ASSERT(column_ndx < get_column_count());
    TIGHTDB_ASSERT(ndx <= m_size);

    ColumnMixed& column = get_column_mixed(column_ndx);
    DataType type = value.get_type();

    switch (type) {
        case type_Int:
            column.insert_int(ndx, value.get_int());
            break;
        case type_Bool:
            column.insert_bool(ndx, value.get_bool());
            break;
        case type_Date:
            column.insert_date(ndx, value.get_date());
            break;
        case type_Float:
            column.insert_float(ndx, value.get_float());
            break;
        case type_Double:
            column.insert_double(ndx, value.get_double());
            break;
        case type_String:
            column.insert_string(ndx, value.get_string());
            break;
        case type_Binary:
            column.insert_binary(ndx, value.get_binary());
            break;
        case type_Table:
            column.insert_subtable(ndx, 0);
            break;
        case type_Mixed:
            TIGHTDB_ASSERT(false);
            break;
    }

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().insert_value(column_ndx, ndx, value); // Throws
#endif
}

void Table::insert_done()
{
    ++m_size;

#ifdef TIGHTDB_DEBUG
    Verify();
#endif

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().row_insert_complete(); // Throws
#endif
}


// count ----------------------------------------------

size_t Table::count_int(size_t column_ndx, int64_t value) const
{
    if(!m_columns.is_attached())
        return 0;

    const Column& column = get_column<Column, col_type_Int>(column_ndx);
    return column.count(value);
}
size_t Table::count_float(size_t column_ndx, float value) const
{
    if(!m_columns.is_attached())
        return 0;

    const ColumnFloat& column = get_column<ColumnFloat, col_type_Float>(column_ndx);
    return column.count(value);
}
size_t Table::count_double(size_t column_ndx, double value) const
{
    if(!m_columns.is_attached())
        return 0;

    const ColumnDouble& column = get_column<ColumnDouble, col_type_Double>(column_ndx);
    return column.count(value);
}
size_t Table::count_string(size_t column_ndx, StringData value) const
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < get_column_count());

    if(!m_columns.is_attached())
        return 0;

    ColumnType type = get_real_column_type(column_ndx);
    if (type == col_type_String) {
        const AdaptiveStringColumn& column = get_column_string(column_ndx);
        return column.count(value);
    }
    else {
        TIGHTDB_ASSERT(type == col_type_StringEnum);
        const ColumnStringEnum& column = get_column_string_enum(column_ndx);
        return column.count(value);
    }
}

// sum ----------------------------------------------

int64_t Table::sum(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0;

    const Column& column = get_column<Column, col_type_Int>(column_ndx);
    return column.sum();
}
double Table::sum_float(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0.f;

    const ColumnFloat& column = get_column<ColumnFloat, col_type_Float>(column_ndx);
    return column.sum();
}
double Table::sum_double(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0.;

    const ColumnDouble& column = get_column<ColumnDouble, col_type_Double>(column_ndx);
    return column.sum();
}

// average ----------------------------------------------

double Table::average(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0;

    const Column& column = get_column<Column, col_type_Int>(column_ndx);
    return column.average();
}
double Table::average_float(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0.f;

    const ColumnFloat& column = get_column<ColumnFloat, col_type_Float>(column_ndx);
    return column.average();
}
double Table::average_double(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0.;

    const ColumnDouble& column = get_column<ColumnDouble, col_type_Double>(column_ndx);
    return column.average();
}

// minimum ----------------------------------------------

#define USE_COLUMN_AGGREGATE 1

int64_t Table::minimum(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0;

#if USE_COLUMN_AGGREGATE
    const Column& column = get_column<Column, col_type_Int>(column_ndx);
    return column.minimum();
#else
    if (is_empty())
        return 0;

    int64_t mv = get_int(column_ndx, 0);
    for (size_t i = 1; i < size(); ++i) {
        int64_t v = get_int(column_ndx, i);
        if (v < mv) {
            mv = v;
        }
    }
    return mv;
#endif
}

float Table::minimum_float(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0.f;

    const ColumnFloat& column = get_column<ColumnFloat, col_type_Float>(column_ndx);
    return column.minimum();
}
double Table::minimum_double(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0.;

    const ColumnDouble& column = get_column<ColumnDouble, col_type_Double>(column_ndx);
    return column.minimum();
}

// maximum ----------------------------------------------

int64_t Table::maximum(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0;

#if USE_COLUMN_AGGREGATE
    const Column& column = get_column<Column, col_type_Int>(column_ndx);
    return column.maximum();
#else
    if (is_empty())
        return 0;

    int64_t mv = get_int(column_ndx, 0);
    for (size_t i = 1; i < size(); ++i) {
        int64_t v = get_int(column_ndx, i);
        if (v > mv) {
            mv = v;
        }
    }
    return mv;
#endif
}
float Table::maximum_float(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0.f;

    const ColumnFloat& column = get_column<ColumnFloat, col_type_Float>(column_ndx);
    return column.maximum();
}
double Table::maximum_double(size_t column_ndx) const
{
    if(!m_columns.is_attached())
        return 0.;

    const ColumnDouble& column = get_column<ColumnDouble, col_type_Double>(column_ndx);
    return column.maximum();
}



size_t Table::lookup(StringData value) const
{
    if(!m_columns.is_attached())
        return not_found;

    // First time we do a lookup we check if we can cache the index
    if (!m_lookup_index) {
        if (get_column_count() < 1)
            return not_found; // no column to lookup in

        ColumnType type = get_real_column_type(0);
        if (type == col_type_String) {
            const AdaptiveStringColumn& column = get_column_string(0);
            if (!column.has_index()) {
                return column.find_first(value);
            }
            else {
                m_lookup_index = &column.get_index();
            }
        }
        else if (type == col_type_StringEnum) {
            const ColumnStringEnum& column = get_column_string_enum(0);
            if (!column.has_index()) {
                return column.find_first(value);
            }
            else {
                m_lookup_index = &column.get_index();
            }
        }
        else {
            return not_found; // invalid column type
        }
    }

    // Do lookup directly on cached index
    return m_lookup_index->find_first(value);
}

template <class T> std::size_t Table::find_first(std::size_t column_ndx, T value) const
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == ColumnTypeTraits<T>::ct_id_real);

    if(!m_columns.is_attached())
        return not_found;

    typedef typename ColumnTypeTraits<T>::column_type ColType;
    const ColType& column = get_column<ColType, ColumnTypeTraits<T>::ct_id>(column_ndx);
    return column.find_first(value);
}

size_t Table::find_first_int(size_t column_ndx, int64_t value) const
{    
    return find_first<int64_t>(column_ndx, value);
}

size_t Table::find_first_bool(size_t column_ndx, bool value) const
{
    return find_first<bool>(column_ndx, value);
}

size_t Table::find_first_date(size_t column_ndx, Date value) const
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    TIGHTDB_ASSERT(get_real_column_type(column_ndx) == col_type_Date);

    if(!m_columns.is_attached())
        return not_found;

    const Column& column = get_column(column_ndx);

    return column.find_first(int64_t(value.get_date()));
}

size_t Table::find_first_float(size_t column_ndx, float value) const
{
    return find_first<float>(column_ndx, value);
}

size_t Table::find_first_double(size_t column_ndx, double value) const
{
    return find_first<double>(column_ndx, value);
}

size_t Table::find_first_string(size_t column_ndx, StringData value) const
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    if(!m_columns.is_attached())
        return not_found;

    ColumnType type = get_real_column_type(column_ndx);
    if (type == col_type_String) {
        const AdaptiveStringColumn& column = get_column_string(column_ndx);
        return column.find_first(value);
    }
    TIGHTDB_ASSERT(type == col_type_StringEnum);
    const ColumnStringEnum& column = get_column_string_enum(column_ndx);
    return column.find_first(value);
}

size_t Table::find_first_binary(size_t, BinaryData) const
{
    // FIXME: Implement this!
    throw runtime_error("Not implemented");
}


template <class T> ConstTableView Table::find_all(size_t column_ndx, T value) const
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    TableView tv(*this);

    if(m_columns.is_attached()) {
        typedef typename ColumnTypeTraits<T>::column_type ColType;
        const ColType& column = get_column<ColType, ColumnTypeTraits<T>::ct_id>(column_ndx);
        column.find_all(tv.get_ref_column(), value);
    }
    return tv;
}


TableView Table::find_all_int(size_t column_ndx, int64_t value)
{
    return const_cast<const Table*>(this)->find_all<int64_t>(column_ndx, value);
}

ConstTableView Table::find_all_int(size_t column_ndx, int64_t value) const
{
    return find_all<int64_t>(column_ndx, value);
}

TableView Table::find_all_bool(size_t column_ndx, bool value)
{
    return const_cast<const Table*>(this)->find_all<bool>(column_ndx, value);
}

ConstTableView Table::find_all_bool(size_t column_ndx, bool value) const
{
    return find_all<int64_t>(column_ndx, value);
}


TableView Table::find_all_float(size_t column_ndx, float value)
{
    return const_cast<const Table*>(this)->find_all<float>(column_ndx, value);
}

ConstTableView Table::find_all_float(size_t column_ndx, float value) const
{
    return find_all<float>(column_ndx, value);
}

TableView Table::find_all_double(size_t column_ndx, double value)
{
    return const_cast<const Table*>(this)->find_all<double>(column_ndx, value);
}

ConstTableView Table::find_all_double(size_t column_ndx, double value) const
{
    return find_all<double>(column_ndx, value);
}

TableView Table::find_all_date(size_t column_ndx, Date value)
{
    return const_cast<const Table*>(this)->find_all<int64_t>(column_ndx, static_cast<int64_t>(value.get_date()));
}

ConstTableView Table::find_all_date(size_t column_ndx, Date value) const
{
    return find_all<int64_t>(column_ndx, static_cast<int64_t>(value.get_date()));
}

TableView Table::find_all_string(size_t column_ndx, StringData value)
{
    ConstTableView ctv = const_cast<const Table*>(this)->find_all_string(column_ndx, value);
    return ctv;
}

ConstTableView Table::find_all_string(size_t column_ndx, StringData value) const
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());

    ColumnType type = get_real_column_type(column_ndx);
    ConstTableView tv(*this);
    
    if(m_columns.is_attached()) {
        if (type == col_type_String) {
            const AdaptiveStringColumn& column = get_column_string(column_ndx);
            column.find_all(tv.get_ref_column(), value);
        }
        else {
            TIGHTDB_ASSERT(type == col_type_StringEnum);
            const ColumnStringEnum& column = get_column_string_enum(column_ndx);
            column.find_all(tv.get_ref_column(), value);
        }
    }
    return tv;
}

TableView Table::find_all_binary(size_t, BinaryData)
{
    // FIXME: Implement this!
    throw runtime_error("Not implemented");
}

ConstTableView Table::find_all_binary(size_t, BinaryData) const
{
    // FIXME: Implement this!
    throw runtime_error("Not implemented");
}

TableView Table::distinct(size_t column_ndx)
{
    ConstTableView ctv = const_cast<const Table*>(this)->distinct(column_ndx);
    return ctv;
}

ConstTableView Table::distinct(size_t column_ndx) const
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    TIGHTDB_ASSERT(has_index(column_ndx));

    ConstTableView tv(*this);
    Array& refs = tv.get_ref_column();

    if(m_columns.is_attached())
    {
        ColumnType type = get_real_column_type(column_ndx);
        if (type == col_type_String) {
            const AdaptiveStringColumn& column = get_column_string(column_ndx);
            const StringIndex& index = column.get_index();
            index.distinct(refs);
        }
        else {
            TIGHTDB_ASSERT(type == col_type_StringEnum);
            const ColumnStringEnum& column = get_column_string_enum(column_ndx);
            const StringIndex& index = column.get_index();
            index.distinct(refs);
        }
    }
    return tv;
}

TableView Table::get_sorted_view(size_t column_ndx, bool ascending)
{
    ConstTableView ctv = const_cast<const Table*>(this)->get_sorted_view(column_ndx, ascending);
    return ctv;
}

ConstTableView Table::get_sorted_view(size_t column_ndx, bool ascending) const
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());

    ConstTableView tv(*this);

    if(m_columns.is_attached()) {
        // Insert refs to all rows in table
        Array& refs = tv.get_ref_column();
        size_t count = size();
        for (size_t i = 0; i < count; ++i) {
            refs.add(i);
        }

        // Sort the refs based on the given column
        tv.sort(column_ndx, ascending);
    }
    return move(tv);
}


size_t Table::lower_bound_int(size_t column_ndx, int64_t value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    return !m_columns.is_attached() ? 0 : get_column(column_ndx).lower_bound_int(value);
}

size_t Table::upper_bound_int(size_t column_ndx, int64_t value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    return !m_columns.is_attached() ? 0 : get_column(column_ndx).upper_bound_int(value);
}

size_t Table::lower_bound_bool(size_t column_ndx, bool value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    return !m_columns.is_attached() ? 0 : get_column(column_ndx).lower_bound_int(value);
}

size_t Table::upper_bound_bool(size_t column_ndx, bool value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    return !m_columns.is_attached() ? 0 : get_column(column_ndx).upper_bound_int(value);
}

size_t Table::lower_bound_float(size_t column_ndx, float value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    return !m_columns.is_attached() ? 0 : get_column_float(column_ndx).lower_bound(value);
}

size_t Table::upper_bound_float(size_t column_ndx, float value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    return !m_columns.is_attached() ? 0 : get_column_float(column_ndx).upper_bound(value);
}

size_t Table::lower_bound_double(size_t column_ndx, double value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    return !m_columns.is_attached() ? 0 : get_column_double(column_ndx).lower_bound(value);
}

size_t Table::upper_bound_double(size_t column_ndx, double value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    return !m_columns.is_attached() ? 0 : get_column_double(column_ndx).upper_bound(value);
}

size_t Table::lower_bound_string(size_t column_ndx, StringData value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    if(!m_columns.is_attached())
        return 0;

    ColumnType type = get_real_column_type(column_ndx);
    if (type == col_type_String) {
        const AdaptiveStringColumn& column = get_column_string(column_ndx);
        return column.lower_bound_string(value);
    }
    TIGHTDB_ASSERT(type == col_type_StringEnum);
    const ColumnStringEnum& column = get_column_string_enum(column_ndx);
    return column.lower_bound_string(value);
}

size_t Table::upper_bound_string(size_t column_ndx, StringData value) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(!m_columns.is_attached() || column_ndx < m_columns.size());
    if(!m_columns.is_attached())
        return 0;

    ColumnType type = get_real_column_type(column_ndx);
    if (type == col_type_String) {
        const AdaptiveStringColumn& column = get_column_string(column_ndx);
        return column.upper_bound_string(value);
    }
    TIGHTDB_ASSERT(type == col_type_StringEnum);
    const ColumnStringEnum& column = get_column_string_enum(column_ndx);
    return column.upper_bound_string(value);
}

void Table::optimize()
{
    // At the present time there is only one kind of optimization that
    // we can do, and that is to replace a string column with a string
    // enumeration column. Since this involves changing the spec of
    // the table, it is not something we can do for a subtable with
    // shared spec.
    if (has_shared_spec())
        return;

    Allocator& alloc = m_columns.get_alloc();

    size_t column_count = get_column_count();
    for (size_t i = 0; i < column_count; ++i) {
        ColumnType type = get_real_column_type(i);
        if (type == col_type_String) {
            AdaptiveStringColumn* column = &get_column_string(i);

            ref_type keys_ref, values_ref;
            bool res = column->auto_enumerate(keys_ref, values_ref);
            if (!res)
                continue;

            // Add to spec and column refs
            m_spec.set_column_type(i, col_type_StringEnum);
            Spec::ColumnInfo info;
            m_spec.get_column_info(i, info);
            size_t column_ref_ndx = info.m_column_ref_ndx;
            m_columns.set(column_ref_ndx, keys_ref);
            m_columns.insert(column_ref_ndx+1, values_ref);

            // There are still same number of columns, but since
            // the enum type takes up two posistions in m_columns
            // we have to move refs in all following columns
            adjust_column_ndx_in_parent(i+1, 1);

            // Replace cached column
            ColumnStringEnum* e =
                new ColumnStringEnum(keys_ref, values_ref, &m_columns, column_ref_ndx, alloc);
            m_cols.set(i, intptr_t(e));

            // Inherit any existing index
            if (info.m_has_index) {
                StringIndex* index = column->release_index();
                e->install_index(index);
            }

            // Clean up the old column
            column->destroy();
            delete column;
        }
    }

#ifdef TIGHTDB_ENABLE_REPLICATION
    transact_log().optimize_table(); // Throws
#endif
}

void Table::adjust_column_ndx_in_parent(size_t column_ndx_begin, int diff) TIGHTDB_NOEXCEPT
{
    for (size_t i = column_ndx_begin; i < m_cols.size(); ++i) {
        ColumnBase* column = reinterpret_cast<ColumnBase*>(m_cols.get(i));
        column->adjust_ndx_in_parent(diff);
    }
}

void Table::update_from_parent(size_t old_baseline) TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(is_attached());

    // There is no top for sub-tables sharing spec
    if (m_top.is_attached()) {
        if (!m_top.update_from_parent(old_baseline))
            return;
    }

    m_spec.update_from_parent(old_baseline);

    if (!m_columns.update_from_parent(old_baseline))
        return;

    // Update column accessors
    size_t n = m_cols.size();
    for (size_t i = 0; i < n; ++i) {
        ColumnBase* column = reinterpret_cast<ColumnBase*>(uintptr_t(m_cols.get(i)));
        column->update_from_parent(old_baseline);
    }
}


void Table::update_from_spec()
{
    TIGHTDB_ASSERT(!has_shared_spec());
    TIGHTDB_ASSERT(m_columns.is_empty() && m_cols.is_empty()); // only on initial creation

    detach_subtable_accessors();

    create_columns();
}


// to JSON: ------------------------------------------

void Table::to_json(ostream& out) const
{
    // Represent table as list of objects
    out << "[";

    size_t row_count = size();
    for (size_t r = 0; r < row_count; ++r) {
        if (r > 0)
            out << ",";
        to_json_row(r, out);
    }

    out << "]";
}


namespace {

inline void out_date(ostream& out, Date value)
{
    time_t rawtime = value.get_date();
    struct tm* t = gmtime(&rawtime);
    if (t) {
        // We need a buffer for formatting dates (and binary to hex). Max
        // size is 20 bytes (incl zero byte) "YYYY-MM-DD HH:MM:SS"\0
        char buffer[30];
        size_t res = strftime(buffer, 30, "%Y-%m-%d %H:%M:%S", t);
        if (res)
            out << buffer;
    }
}

inline void out_binary(ostream& out, const BinaryData bin)
{
    const char* p = bin.data();
    for (size_t i = 0; i < bin.size(); ++i)
        out << setw(2) << setfill('0') << hex << static_cast<unsigned int>(p[i]) << dec;
}

template<class T> void out_floats(ostream& out, T value)
{
    streamsize old = out.precision();
    out.precision(numeric_limits<T>::digits10 + 1);
    out << scientific << value;
    out.precision(old);
}

} // anonymous namespace


void Table::to_json_row(size_t row_ndx, ostream& out) const
{
    out << "{";
    size_t column_count = get_column_count();
    for (size_t i = 0; i < column_count; ++i) {
        if (i > 0)
            out << ",";

        StringData name = get_column_name(i);
        out << "\"" << name << "\":";

        DataType type = get_column_type(i);
        switch (type) {
            case type_Int:
                out << get_int(i, row_ndx);
                break;
            case type_Bool:
                out << (get_bool(i, row_ndx) ? "true" : "false");
                break;
            case type_Float:
                out_floats<float>(out, get_float(i, row_ndx));
                break;
            case type_Double:
                out_floats<double>(out, get_double(i, row_ndx));
                break;
            case type_String:
                out << "\"" << get_string(i, row_ndx) << "\"";
                break;
            case type_Date:
                out << "\""; out_date(out, get_date(i, row_ndx)); out << "\"";
                break;
            case type_Binary:
                out << "\""; out_binary(out, get_binary(i, row_ndx)); out << "\"";
                break;
            case type_Table:
                get_subtable(i, row_ndx)->to_json(out);
                break;
            case type_Mixed:
            {
                DataType mtype = get_mixed_type(i, row_ndx);
                if (mtype == type_Table) {
                    get_subtable(i, row_ndx)->to_json(out);
                }
                else {
                    Mixed m = get_mixed(i, row_ndx);
                    switch (mtype) {
                        case type_Int:
                            out << m.get_int();
                            break;
                        case type_Bool:
                            out << (m.get_bool() ? "true" : "false");
                            break;
                        case type_Float:
                            out_floats<float>(out, m.get_float());
                            break;
                        case type_Double:
                            out_floats<double>(out, m.get_double());
                            break;
                        case type_String:
                            out << "\"" << m.get_string() << "\"";
                            break;
                        case type_Date:
                            out << "\""; out_date(out, m.get_date()); out << "\"";
                            break;
                        case type_Binary:
                            out << "\""; out_binary(out, m.get_binary()); out << "\"";
                            break;
                        case type_Table:
                        case type_Mixed:
                            TIGHTDB_ASSERT(false);
                            break;
                    }
                }
                break;
            }
        }
    }
    out << "}";
}


// to_string --------------------------------------------------


namespace {

size_t chars_in_int(int64_t v)
{
    size_t count = 0;
    while (v /= 10)
        ++count;
    return count+1;
}

} // anonymous namespace


void Table::to_string(ostream& out, size_t limit) const
{
    // Print header (will also calculate widths)
    vector<size_t> widths;
    to_string_header(out, widths);

    // Set limit=-1 to print all rows, otherwise only print to limit
    size_t row_count = size();
    size_t out_count = (limit == size_t(-1)) ? row_count : (row_count < limit) ? row_count : limit;

    // Print rows
    for (size_t i = 0; i < out_count; ++i) {
        to_string_row(i, out, widths);
    }

    if (out_count < row_count) {
        size_t rest = row_count - out_count;
        out << "... and " << rest << " more rows (total " << row_count << ")";
    }
}

void Table::row_to_string(size_t row_ndx, ostream& out) const
{
    TIGHTDB_ASSERT(row_ndx < size());

    // Print header (will also calculate widths)
    vector<size_t> widths;
    to_string_header(out, widths);

    // Print row contents
    to_string_row(row_ndx, out, widths);
}

void Table::to_string_header(ostream& out, vector<size_t>& widths) const
{
    size_t column_count = get_column_count();
    size_t row_count = size();
    size_t row_ndx_width = chars_in_int(row_count);
    widths.push_back(row_ndx_width);

    // Empty space over row numbers
    for (size_t i = 0; i < row_ndx_width+1; ++i)
        out << " ";

    // Write header
    for (size_t col = 0; col < column_count; ++col) {
        StringData name = get_column_name(col);
        DataType type = get_column_type(col);
        size_t width = 0;
        switch (type) {
            case type_Bool:
                width = 5;
                break;
            case type_Date:
                width = 19;
                break;
            case type_Int:
                width = chars_in_int(maximum(col));
                break;
            case type_Float:
                // max chars for scientific notation:
                width = 14;
                break;
            case type_Double:
                width = 14;
                break;
            case type_Table:
                for (size_t row = 0; row < row_count; ++row) {
                    size_t len = chars_in_int(get_subtable_size(col, row));
                    width = max(width, len+2);
                }
                width += 2; // space for "[]"
                break;
            case type_Binary:
                for (size_t row = 0; row < row_count; ++row) {
                    size_t len = chars_in_int(get_binary(col, row).size()) + 2;
                    width = max(width, len);
                }
                width += 6; // space for " bytes"
                break;
            case type_String: {
                // Find max length of the strings
                for (size_t row = 0; row < row_count; ++row) {
                    size_t len = get_string(col, row).size();
                    width = max(width, len);
                }
                if (width > 20)
                    width = 23; // cut strings longer than 20 chars
                break;
            }
            case type_Mixed:
                // Find max length of the mixed values
                width = 0;
                for (size_t row = 0; row < row_count; ++row) {
                    DataType mtype = get_mixed_type(col, row);
                    if (mtype == type_Table) {
                        size_t len = chars_in_int( get_subtable_size(col, row) ) + 2;
                        width = max(width, len);
                        continue;
                    }
                    Mixed m = get_mixed(col, row);
                    switch (mtype) {
                        case type_Bool:
                            width = max(width, size_t(5));
                            break;
                        case type_Date:
                            width = max(width, size_t(19));
                            break;
                        case type_Int:
                            width = max(width, chars_in_int(m.get_int()));
                            break;
                        case type_Float:
                            width = max(width, size_t(14));
                            break;
                        case type_Double:
                            width = max(width, size_t(14));
                            break;
                        case type_Binary:
                            width = max(width, chars_in_int(m.get_binary().size()) + 6);
                            break;
                        case type_String: {
                            size_t len = m.get_string().size();
                            if (len > 20)
                                len = 23;
                            width = max(width, len);
                            break;
                        }
                        case type_Table:
                        case type_Mixed:
                            TIGHTDB_ASSERT(false);
                            break;
                    }
                }
                break;
        }
        // Set width to max of column name and the longest value
        size_t name_len = name.size();
        if (name_len > width)
            width = name_len;

        widths.push_back(width);
        out << "  "; // spacing

        out.width(width);
        out << string(name);
    }
    out << "\n";
}


namespace {

inline void out_string(ostream& out, const string text, const size_t max_len)
{
    out.setf(ostream::left, ostream::adjustfield);
    if (text.size() > max_len)
        out << text.substr(0, max_len) + "...";
    else
        out << text;
    out.unsetf(ostream::adjustfield);
}

inline void out_table(ostream& out, const size_t len)
{
    streamsize width = out.width() - chars_in_int(len) - 1;
    out.width(width);
    out << "[" << len << "]";
}

} // anonymous namespace


void Table::to_string_row(size_t row_ndx, ostream& out, const vector<size_t>& widths) const
{
    size_t column_count  = get_column_count();
    size_t row_ndx_width = widths[0];

    out << scientific;          // for float/double
    out.width(row_ndx_width);
    out << row_ndx << ":";

    for (size_t col = 0; col < column_count; ++col) {
        out << "  "; // spacing
        out.width(widths[col+1]);

        DataType type = get_column_type(col);
        switch (type) {
            case type_Bool:
                out << (get_bool(col, row_ndx) ? "true" : "false");
                break;
            case type_Int:
                out << get_int(col, row_ndx);
                break;
            case type_Float:
                out << get_float(col, row_ndx);
                break;
            case type_Double:
                out << get_double(col, row_ndx);
                break;
            case type_String:
                out_string(out, get_string(col, row_ndx), 20);
                break;
            case type_Date:
                out_date(out, get_date(col, row_ndx));
                break;
            case type_Table:
                out_table(out, get_subtable_size(col, row_ndx));
                break;
            case type_Binary:
                out.width(widths[col+1]-6); // adjust for " bytes" text
                out << get_binary(col, row_ndx).size() << " bytes";
                break;
            case type_Mixed:
            {
                DataType mtype = get_mixed_type(col, row_ndx);
                if (mtype == type_Table) {
                    out_table(out, get_subtable_size(col, row_ndx));
                }
                else {
                    Mixed m = get_mixed(col, row_ndx);
                    switch (mtype) {
                        case type_Bool:
                            out << (m.get_bool() ? "true" : "false");
                            break;
                        case type_Int:
                            out << m.get_int();
                            break;
                        case type_Float:
                            out << m.get_float();
                            break;
                        case type_Double:
                            out << m.get_double();
                            break;
                        case type_String:
                            out_string(out, m.get_string(), 20);
                            break;
                        case type_Date:
                            out_date(out, m.get_date());
                            break;
                        case type_Binary:
                            out.width(widths[col+1]-6); // adjust for " bytes" text
                            out << m.get_binary().size() << " bytes";
                            break;
                        case type_Table:
                        case type_Mixed:
                            TIGHTDB_ASSERT(false);
                            break;
                    }
                }
                break;
            }
        }
    }
    out << "\n";
}


bool Table::compare_rows(const Table& t) const
{
    // A wrapper for an empty subtable with shared spec may be created
    // with m_data == 0. In this case there are no Column wrappers, so
    // the standard comparison scheme becomes impossible.
    if (m_size == 0)
        return t.m_size == 0;

    // FIXME: The current column comparison implementation is very
    // inefficient, we should use sequential tree accessors when they
    // become available.

    size_t n = get_column_count();
    TIGHTDB_ASSERT(t.get_column_count() == n);
    for (size_t i=0; i<n; ++i) {
        ColumnType type = get_real_column_type(i);
        TIGHTDB_ASSERT(type == col_type_String     ||
                       type == col_type_StringEnum ||
                       type == t.get_real_column_type(i));

        switch (type) {
            case col_type_Int:
            case col_type_Bool:
            case col_type_Date: {
                const Column& c1 = get_column(i);
                const Column& c2 = t.get_column(i);
                if (!c1.compare_int(c2))
                    return false;
                break;
            }
            case col_type_Float: {
                const ColumnFloat& c1 = get_column_float(i);
                const ColumnFloat& c2 = t.get_column_float(i);
                if (!c1.compare(c2))
                    return false;
                break;
            }
            case col_type_Double: {
                const ColumnDouble& c1 = get_column_double(i);
                const ColumnDouble& c2 = t.get_column_double(i);
                if (!c1.compare(c2))
                    return false;
                break;
            }
            case col_type_String: {
                const AdaptiveStringColumn& c1 = get_column_string(i);
                ColumnType type2 = t.get_real_column_type(i);
                if (type2 == col_type_String) {
                    const AdaptiveStringColumn& c2 = t.get_column_string(i);
                    if (!c1.compare_string(c2))
                        return false;
                }
                else {
                    TIGHTDB_ASSERT(type2 == col_type_StringEnum);
                    const ColumnStringEnum& c2 = t.get_column_string_enum(i);
                    if (!c2.compare_string(c1))
                        return false;
                }
                break;
            }
            case col_type_StringEnum: {
                const ColumnStringEnum& c1 = get_column_string_enum(i);
                ColumnType type2 = t.get_real_column_type(i);
                if (type2 == col_type_StringEnum) {
                    const ColumnStringEnum& c2 = t.get_column_string_enum(i);
                    if (!c1.compare_string(c2))
                        return false;
                }
                else {
                    TIGHTDB_ASSERT(type2 == col_type_String);
                    const AdaptiveStringColumn& c2 = t.get_column_string(i);
                    if (!c1.compare_string(c2))
                        return false;
                }
                break;
            }
            case col_type_Binary: {
                const ColumnBinary& c1 = get_column_binary(i);
                const ColumnBinary& c2 = t.get_column_binary(i);
                if (!c1.compare_binary(c2))
                    return false;
                break;
            }
            case col_type_Table: {
                const ColumnTable& c1 = get_column_table(i);
                const ColumnTable& c2 = t.get_column_table(i);
                if (!c1.compare_table(c2))
                    return false;
                break;
            }
            case col_type_Mixed: {
                const ColumnMixed& c1 = get_column_mixed(i);
                const ColumnMixed& c2 = t.get_column_mixed(i);
                if (!c1.compare_mixed(c2))
                    return false;
                break;
            }
            default:
                TIGHTDB_ASSERT(false);
        }
    }
    return true;
}


const Array* Table::get_column_root(size_t col_ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(col_ndx < get_column_count());
    return reinterpret_cast<ColumnBase*>(m_cols.get(col_ndx))->get_root_array();
}


pair<const Array*, const Array*> Table::get_string_column_roots(size_t col_ndx) const
    TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(col_ndx < get_column_count());

    const ColumnBase* col = reinterpret_cast<ColumnBase*>(m_cols.get(col_ndx));

    const Array* root = col->get_root_array();
    const Array* enum_root = 0;

    if (const ColumnStringEnum* c = dynamic_cast<const ColumnStringEnum*>(col)) {
        enum_root = c->get_enum_root_array();
    }
    else {
        TIGHTDB_ASSERT(dynamic_cast<const AdaptiveStringColumn*>(col));
    }

    return make_pair(root, enum_root);
}


#ifdef TIGHTDB_DEBUG

void Table::Verify() const
{
    if (m_top.is_attached())
        m_top.Verify();
    m_columns.Verify();
    if (m_columns.is_attached()) {
        size_t column_count = get_column_count();
        TIGHTDB_ASSERT(column_count == m_cols.size());

        for (size_t i = 0; i < column_count; ++i) {
            ColumnType type = get_real_column_type(i);
            switch (type) {
                case type_Int:
                case type_Bool:
                case type_Date: {
                    const Column& column = get_column(i);
                    TIGHTDB_ASSERT(column.size() == m_size);
                    column.Verify();
                    break;
                }
                case type_Float: {
                    const ColumnFloat& column = get_column_float(i);
                    TIGHTDB_ASSERT(column.size() == m_size);
                    column.Verify();
                    break;
                }
                case type_Double: {
                    const ColumnDouble& column = get_column_double(i);
                    TIGHTDB_ASSERT(column.size() == m_size);
                    column.Verify();
                    break;
                }
                case type_String: {
                    const AdaptiveStringColumn& column = get_column_string(i);
                    TIGHTDB_ASSERT(column.size() == m_size);
                    column.Verify();
                    break;
                }
                case col_type_StringEnum: {
                    const ColumnStringEnum& column = get_column_string_enum(i);
                    TIGHTDB_ASSERT(column.size() == m_size);
                    column.Verify();
                    break;
                }
                case type_Binary: {
                    const ColumnBinary& column = get_column_binary(i);
                    TIGHTDB_ASSERT(column.size() == m_size);
                    column.Verify();
                    break;
                }
                case type_Table: {
                    const ColumnTable& column = get_column_table(i);
                    TIGHTDB_ASSERT(column.size() == m_size);
                    column.Verify();
                    break;
                }
                case type_Mixed: {
                    const ColumnMixed& column = get_column_mixed(i);
                    TIGHTDB_ASSERT(column.size() == m_size);
                    column.Verify();
                    break;
                }
                default:
                    TIGHTDB_ASSERT(false);
            }
        }
    }

    m_spec.Verify();

    Allocator& alloc = m_columns.get_alloc();
    alloc.Verify();
}


void Table::to_dot(ostream& out, StringData title) const
{
    if (m_top.is_attached()) {
        out << "subgraph cluster_topleveltable" << m_top.get_ref() << " {" << endl;
        out << " label = \"TopLevelTable";
        if (0 < title.size())
            out << "\\n'" << title << "'";
        out << "\";" << endl;
        m_top.to_dot(out, "table_top");
        const Spec& specset = get_spec();
        specset.to_dot(out);
    }
    else {
        out << "subgraph cluster_table_"  << m_columns.get_ref() <<  " {" << endl;
        out << " label = \"Table";
        if (0 < title.size())
            out << " " << title;
        out << "\";" << endl;
    }

    to_dot_internal(out);

    out << "}" << endl;
}


void Table::to_dot_internal(ostream& out) const
{
    m_columns.to_dot(out, "columns");

    // Columns
    size_t column_count = get_column_count();
    for (size_t i = 0; i < column_count; ++i) {
        const ColumnBase& column = get_column_base(i);
        StringData name = get_column_name(i);
        column.to_dot(out, name);
    }
}


void Table::print() const
{
    // Table header
    cout << "Table: len(" << m_size << ")\n    ";
    size_t column_count = get_column_count();
    for (size_t i = 0; i < column_count; ++i) {
        StringData name = m_spec.get_column_name(i);
        cout << left << setw(10) << name << right << " ";
    }

    // Types
    cout << "\n    ";
    for (size_t i = 0; i < column_count; ++i) {
        ColumnType type = get_real_column_type(i);
        switch (type) {
            case type_Int:
                cout << "Int        "; break;
            case type_Float:
                cout << "Float      "; break;
            case type_Double:
                cout << "Double     "; break;
            case type_Bool:
                cout << "Bool       "; break;
            case type_String:
                cout << "String     "; break;
            default:
                TIGHTDB_ASSERT(false);
        }
    }
    cout << "\n";

    // Columns
    for (size_t i = 0; i < m_size; ++i) {
        cout << setw(3) << i;
        for (size_t n = 0; n < column_count; ++n) {
            ColumnType type = get_real_column_type(n);
            switch (type) {
                case type_Int: {
                    const Column& column = get_column(n);
                    cout << setw(10) << column.get(i) << " ";
                    break;
                }
                case type_Float: {
                    const ColumnFloat& column = get_column_float(n);
                    cout << setw(10) << column.get(i) << " ";
                    break;
                }
                case type_Double: {
                    const ColumnDouble& column = get_column_double(n);
                    cout << setw(10) << column.get(i) << " ";
                    break;
                }
                case type_Bool: {
                    const Column& column = get_column(n);
                    cout << (column.get(i) == 0 ? "     false " : "      true ");
                    break;
                }
                case type_String: {
                    const AdaptiveStringColumn& column = get_column_string(n);
                    cout << setw(10) << column.get(i) << " ";
                    break;
                }
                default:
                    TIGHTDB_ASSERT(false);
            }
        }
        cout << "\n";
    }
    cout << "\n";
}


MemStats Table::stats() const
{
    MemStats stats;
    m_top.stats(stats);
    return stats;
}


#endif // TIGHTDB_DEBUG
