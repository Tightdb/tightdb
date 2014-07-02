#include <iostream>
#include <iomanip>

#include <tightdb/column_table.hpp>

using namespace std;
using namespace tightdb;
using namespace tightdb::util;


void ColumnSubtableParent::update_from_parent(size_t old_baseline) TIGHTDB_NOEXCEPT
{
    if (!m_array->update_from_parent(old_baseline))
        return;
    m_subtable_map.update_from_parent(old_baseline);
}


Table* ColumnSubtableParent::get_subtable_ptr(size_t subtable_ndx)
{
    TIGHTDB_ASSERT(subtable_ndx < size());
    if (Table* subtable = m_subtable_map.find(subtable_ndx))
        return subtable;

    typedef _impl::TableFriend tf;
    ref_type top_ref = get_as_ref(subtable_ndx);
    Allocator& alloc = get_alloc();
    ColumnSubtableParent* parent = this;
    UniquePtr<Table> subtable(tf::create_ref_counted(alloc, top_ref, parent,
                                                     subtable_ndx)); // Throws
    // FIXME: Note that if the following map insertion fails, then the
    // destructor of the newly created child will call
    // ColumnSubtableParent::child_accessor_destroyed() with a pointer that is
    // not in the map. Fortunatly, that situation is properly handled.
    bool was_empty = m_subtable_map.empty();
    m_subtable_map.add(subtable_ndx, subtable.get()); // Throws
    if (was_empty && m_table)
        tf::bind_ref(*m_table);
    return subtable.release();
}


Table* ColumnTable::get_subtable_ptr(size_t subtable_ndx)
{
    TIGHTDB_ASSERT(subtable_ndx < size());
    if (Table* subtable = m_subtable_map.find(subtable_ndx))
        return subtable;

    typedef _impl::TableFriend tf;
    const Spec* spec = tf::get_spec(*m_table);
    size_t subspec_ndx = get_subspec_ndx();
    ConstSubspecRef shared_subspec = spec->get_subspec_by_ndx(subspec_ndx);
    ref_type columns_ref = get_as_ref(subtable_ndx);
    ColumnTable* parent = this;
    UniquePtr<Table> subtable(tf::create_ref_counted(shared_subspec, columns_ref,
                                                     parent, subtable_ndx)); // Throws
    // FIXME: Note that if the following map insertion fails, then the
    // destructor of the newly created child will call
    // ColumnSubtableParent::child_accessor_destroyed() with a pointer that is
    // not in the map. Fortunatly, that situation is properly handled.
    bool was_empty = m_subtable_map.empty();
    m_subtable_map.add(subtable_ndx, subtable.get()); // Throws
    if (was_empty && m_table)
        tf::bind_ref(*m_table);
    return subtable.release();
}


void ColumnSubtableParent::child_accessor_destroyed(Table* child) TIGHTDB_NOEXCEPT
{
    // This function must assume no more than minimal consistency of the
    // accessor hierarchy. This means in particular that it cannot access the
    // underlying node structure. See AccessorConcistncyLevels.

    // Note that due to the possibility of a failure during child creation, it
    // is possible that the calling child is not in the map.

    bool last_entry_removed = m_subtable_map.remove(child);

    // Note that this column instance may be destroyed upon return
    // from Table::unbind_ref(), i.e., a so-called suicide is
    // possible.
    typedef _impl::TableFriend tf;
    if (last_entry_removed && m_table)
        tf::unbind_ref(*m_table);
}


Table* ColumnSubtableParent::get_parent_table(size_t* column_ndx_out) TIGHTDB_NOEXCEPT
{
    if (column_ndx_out)
        *column_ndx_out = m_column_ndx;
    return m_table;
}


Table* ColumnSubtableParent::SubtableMap::find(size_t subtable_ndx) const TIGHTDB_NOEXCEPT
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i)
        if (i->m_subtable_ndx == subtable_ndx)
            return i->m_table;
    return 0;
}


bool ColumnSubtableParent::SubtableMap::detach_and_remove_all() TIGHTDB_NOEXCEPT
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        // Must hold a counted reference while detaching
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::detach(*table);
    }
    bool was_empty = m_entries.empty();
    m_entries.clear();
    return !was_empty;
}


bool ColumnSubtableParent::SubtableMap::detach_and_remove(size_t subtable_ndx) TIGHTDB_NOEXCEPT
{
    typedef entries::iterator iter;
    iter i = m_entries.begin(), end = m_entries.end();
    for (;;) {
        if (i == end)
            return false;
        if (i->m_subtable_ndx == subtable_ndx)
            break;
        ++i;
    }

    // Must hold a counted reference while detaching
    TableRef table(i->m_table);
    typedef _impl::TableFriend tf;
    tf::detach(*table);

    *i = *--end; // Move last over
    m_entries.pop_back();
    return m_entries.empty();
}


bool ColumnSubtableParent::SubtableMap::remove(Table* subtable) TIGHTDB_NOEXCEPT
{
    typedef entries::iterator iter;
    iter i = m_entries.begin(), end = m_entries.end();
    for (;;) {
        if (i == end)
            return false;
        if (i->m_table == subtable)
            break;
        ++i;
    }
    *i = *--end; // Move last over
    m_entries.pop_back();
    return m_entries.empty();
}


void ColumnSubtableParent::SubtableMap::update_from_parent(size_t old_baseline)
    const TIGHTDB_NOEXCEPT
{
    typedef _impl::TableFriend tf;
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i)
        tf::update_from_parent(*i->m_table, old_baseline);
}


void ColumnSubtableParent::SubtableMap::
update_accessors(const size_t* col_path_begin, const size_t* col_path_end,
                 _impl::TableFriend::AccessorUpdater& updater)
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        // Must hold a counted reference while updating
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::update_accessors(*table, col_path_begin, col_path_end, updater);
    }
}


void ColumnSubtableParent::SubtableMap::recursive_mark() TIGHTDB_NOEXCEPT
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::recursive_mark(*table);
    }
}


void ColumnSubtableParent::SubtableMap::refresh_accessor_tree(size_t spec_ndx_in_parent)
{
    typedef entries::const_iterator iter;
    iter end = m_entries.end();
    for (iter i = m_entries.begin(); i != end; ++i) {
        // Must hold a counted reference while refreshing
        TableRef table(i->m_table);
        typedef _impl::TableFriend tf;
        tf::set_shared_subspec_ndx_in_parent(*table, spec_ndx_in_parent);
        tf::refresh_accessor_tree(*table, i->m_subtable_ndx);
    }
}


#ifdef TIGHTDB_DEBUG

pair<ref_type, size_t> ColumnSubtableParent::get_to_dot_parent(size_t ndx_in_parent) const
{
    pair<MemRef, size_t> p = m_array->get_bptree_leaf(ndx_in_parent);
    return make_pair(p.first.m_ref, p.second);
}

#endif


size_t ColumnTable::get_subtable_size(size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < size());

    ref_type columns_ref = get_as_ref(ndx);
    if (columns_ref == 0)
        return 0;

    typedef _impl::TableFriend tf;
    size_t subspec_ndx = get_subspec_ndx();
    Spec* spec = tf::get_spec(*m_table);
    ref_type subspec_ref = spec->get_subspec_ref(subspec_ndx);
    Allocator& alloc = spec->get_alloc();
    return tf::get_size_from_ref(subspec_ref, columns_ref, alloc);
}


void ColumnTable::add(const Table* subtable)
{
    ref_type columns_ref = 0;
    if (subtable && !subtable->is_empty())
        columns_ref = clone_table_columns(subtable); // Throws

    std::size_t row_ndx = tightdb::npos;
    int_fast64_t value = int_fast64_t(columns_ref);
    std::size_t num_rows = 1;
    do_insert(row_ndx, value, num_rows); // Throws
}


void ColumnTable::insert(size_t row_ndx, const Table* subtable)
{
    ref_type columns_ref = 0;
    if (subtable && !subtable->is_empty())
        columns_ref = clone_table_columns(subtable); // Throws

    std::size_t size = this->size(); // Slow
    TIGHTDB_ASSERT(row_ndx <= size);
    std::size_t row_ndx_2 = row_ndx == size ? tightdb::npos : row_ndx;
    int_fast64_t value = int_fast64_t(columns_ref);
    std::size_t num_rows = 1;
    do_insert(row_ndx_2, value, num_rows); // Throws
}


void ColumnTable::set(size_t row_ndx, const Table* subtable)
{
    TIGHTDB_ASSERT(row_ndx < size());
    destroy_subtable(row_ndx);

    ref_type columns_ref = 0;
    if (subtable && !subtable->is_empty())
        columns_ref = clone_table_columns(subtable); // Throws

    int_fast64_t value = int_fast64_t(columns_ref);
    Column::set(row_ndx, value); // Throws

    // Refresh the accessors, if present
    if (Table* table = m_subtable_map.find(row_ndx)) {
        TableRef table_2;
        table_2.reset(table); // Must hold counted reference
        typedef _impl::TableFriend tf;
        tf::discard_child_accessors(*table_2);
        tf::mark(*table_2);
        tf::refresh_accessor_tree(*table_2, row_ndx);
    }
}


void ColumnTable::clear()
{
    discard_child_accessors();
    Column::clear(); // Throws
    // FIXME: This one is needed because Column::clear() forgets about the leaf
    // type. A better solution should probably be sought after.
    m_array->set_type(Array::type_HasRefs);
}


void ColumnTable::erase(size_t row_ndx, bool is_last)
{
    TIGHTDB_ASSERT(row_ndx < size());
    destroy_subtable(row_ndx);
    ColumnSubtableParent::erase(row_ndx, is_last); // Throws
}


void ColumnTable::move_last_over(size_t target_row_ndx, size_t last_row_ndx)
{
    TIGHTDB_ASSERT(target_row_ndx < size());
    destroy_subtable(target_row_ndx);
    ColumnSubtableParent::move_last_over(target_row_ndx, last_row_ndx); // Throws
}


void ColumnTable::destroy_subtable(size_t ndx) TIGHTDB_NOEXCEPT
{
    ref_type columns_ref = get_as_ref(ndx);
    if (columns_ref == 0)
        return; // It was never created

    // Delete sub-tree
    Allocator& alloc = get_alloc();
    Array columns(columns_ref, 0, 0, alloc);
    columns.destroy_deep();
}


bool ColumnTable::compare_table(const ColumnTable& c) const
{
    size_t n = size();
    if (c.size() != n)
        return false;
    for (size_t i = 0; i != n; ++i) {
        ConstTableRef t1 = get_subtable_ptr(i)->get_table_ref(); // Throws
        ConstTableRef t2 = c.get_subtable_ptr(i)->get_table_ref(); // throws
        if (!compare_subtable_rows(*t1, *t2))
            return false;
    }
    return true;
}


void ColumnTable::do_discard_child_accessors() TIGHTDB_NOEXCEPT
{
    discard_child_accessors();
}


#ifdef TIGHTDB_DEBUG

void ColumnTable::Verify() const
{
    Column::Verify();

    // Verify each sub-table
    size_t n = size();
    for (size_t i = 0; i != n; ++i) {
        // We want to verify any cached table accessors so we do not
        // want to skip null refs here.
        ConstTableRef subtable = get_subtable_ptr(i)->get_table_ref();
        subtable->Verify();
    }
}

void ColumnTable::to_dot(ostream& out, StringData title) const
{
    ref_type ref = m_array->get_ref();
    out << "subgraph cluster_subtable_column" << ref << " {" << endl;
    out << " label = \"Subtable column";
    if (title.size() != 0)
        out << "\\n'" << title << "'";
    out << "\";" << endl;
    tree_to_dot(out);
    out << "}" << endl;

    size_t n = size();
    for (size_t i = 0; i != n; ++i) {
        if (get_as_ref(i) == 0)
            continue;
        ConstTableRef subtable = get_subtable_ptr(i)->get_table_ref();
        subtable->to_dot(out);
    }
}

namespace {

void leaf_dumper(MemRef mem, Allocator& alloc, ostream& out, int level)
{
    Array leaf(mem, 0, 0, alloc);
    int indent = level * 2;
    out << setw(indent) << "" << "Subtable leaf (size: "<<leaf.size()<<")\n";
}

} // anonymous namespace

void ColumnTable::dump_node_structure(ostream& out, int level) const
{
    m_array->dump_bptree_structure(out, level, &leaf_dumper);
}

#endif // TIGHTDB_DEBUG
