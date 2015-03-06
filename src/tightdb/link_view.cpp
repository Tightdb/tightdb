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

#include <algorithm>

#include <tightdb/link_view.hpp>
#include <tightdb/column_linklist.hpp>
#ifdef TIGHTDB_ENABLE_REPLICATION
#  include <tightdb/replication.hpp>
#endif
#include <tightdb/table_view.hpp>

using namespace std;
using namespace tightdb;

LinkViewRef LinkView::prepare_for_import(Handover_data& handover_data, Group& group) {
    TableRef tr(group.get_table(handover_data.m_table_num));
    return tr->get_linklist(handover_data.m_col_num, handover_data.m_row_ndx);
}


void LinkView::insert(size_t link_ndx, size_t target_row_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_row_indexes.is_attached() || link_ndx == 0);
    TIGHTDB_ASSERT(!m_row_indexes.is_attached() || link_ndx <= m_row_indexes.size());
    TIGHTDB_ASSERT_3(target_row_ndx, <, m_origin_column.get_target_table().size());
    typedef _impl::TableFriend tf;
    tf::bump_version(*m_origin_table);

    size_t origin_row_ndx = get_origin_row_index();

    // if there are no links yet, we have to create list
    if (!m_row_indexes.is_attached()) {
        TIGHTDB_ASSERT_3(link_ndx, ==, 0);
        ref_type ref = Column::create(m_origin_column.get_alloc()); // Throws
        m_origin_column.set_row_ref(origin_row_ndx, ref); // Throws
        m_row_indexes.get_root_array()->init_from_parent(); // re-attach
    }

    m_row_indexes.insert(link_ndx, target_row_ndx); // Throws
    m_origin_column.add_backlink(target_row_ndx, origin_row_ndx); // Throws

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_insert(*this, link_ndx, target_row_ndx); // Throws
#endif
}


void LinkView::set(size_t link_ndx, size_t target_row_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_row_indexes.is_attached() && link_ndx < m_row_indexes.size());
    TIGHTDB_ASSERT_3(target_row_ndx, <, m_origin_column.get_target_table().size());

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_set(*this, link_ndx, target_row_ndx); // Throws
#endif

    size_t old_target_row_ndx = do_set(link_ndx, target_row_ndx); // Throws
    if (m_origin_column.m_weak_links)
        return;

    Table& target_table = m_origin_column.get_target_table();
    size_t num_remaining = target_table.get_num_strong_backlinks(old_target_row_ndx);
    if (num_remaining > 0)
        return;

    ColumnBase::CascadeState::row target_row;
    target_row.table_ndx = target_table.get_index_in_group();
    target_row.row_ndx   = old_target_row_ndx;
    ColumnBase::CascadeState state;
    state.rows.push_back(target_row);

    typedef _impl::TableFriend tf;
    tf::cascade_break_backlinks_to(target_table, old_target_row_ndx, state); // Throws
    tf::remove_backlink_broken_rows(target_table, state.rows); // Throws
}


// Replication instruction 'link-list-set' calls this function directly.
size_t LinkView::do_set(size_t link_ndx, size_t target_row_ndx)
{
    size_t old_target_row_ndx = m_row_indexes.get(link_ndx);
    size_t origin_row_ndx = get_origin_row_index();
    m_origin_column.remove_backlink(old_target_row_ndx, origin_row_ndx); // Throws
    m_origin_column.add_backlink(target_row_ndx, origin_row_ndx); // Throws
    m_row_indexes.set(link_ndx, target_row_ndx); // Throws
    typedef _impl::TableFriend tf;
    tf::bump_version(*m_origin_table);
    return old_target_row_ndx;
}


void LinkView::move(size_t old_link_ndx, size_t new_link_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_row_indexes.is_attached());
    TIGHTDB_ASSERT_3(old_link_ndx, <, m_row_indexes.size());
    TIGHTDB_ASSERT_3(new_link_ndx, <=, m_row_indexes.size());

    if (old_link_ndx == new_link_ndx)
        return;
    typedef _impl::TableFriend tf;
    tf::bump_version(*m_origin_table);

    size_t link_ndx = (new_link_ndx <= old_link_ndx) ? new_link_ndx : new_link_ndx-1;
    size_t target_row_ndx = m_row_indexes.get(old_link_ndx);
    bool is_last = (old_link_ndx + 1 == m_row_indexes.size());
    m_row_indexes.erase(old_link_ndx, is_last);
    m_row_indexes.insert(link_ndx, target_row_ndx);

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_move(*this, old_link_ndx, new_link_ndx); // Throws
#endif
}


void LinkView::remove(size_t link_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_row_indexes.is_attached() && link_ndx < m_row_indexes.size());

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_erase(*this, link_ndx); // Throws
#endif

    size_t target_row_ndx = do_remove(link_ndx); // Throws
    if (m_origin_column.m_weak_links)
        return;

    Table& target_table = m_origin_column.get_target_table();
    size_t num_remaining = target_table.get_num_strong_backlinks(target_row_ndx);
    if (num_remaining > 0)
        return;

    ColumnBase::CascadeState::row target_row;
    target_row.table_ndx = target_table.get_index_in_group();
    target_row.row_ndx   = target_row_ndx;
    ColumnBase::CascadeState state;
    state.rows.push_back(target_row);

    typedef _impl::TableFriend tf;
    tf::cascade_break_backlinks_to(target_table, target_row_ndx, state); // Throws
    tf::remove_backlink_broken_rows(target_table, state.rows); // Throws
}


// Replication instruction 'link-list-erase' calls this function directly.
size_t LinkView::do_remove(size_t link_ndx)
{
    size_t target_row_ndx = m_row_indexes.get(link_ndx);
    size_t origin_row_ndx = get_origin_row_index();
    m_origin_column.remove_backlink(target_row_ndx, origin_row_ndx); // Throws
    bool is_last = (link_ndx + 1 == m_row_indexes.size());
    m_row_indexes.erase(link_ndx, is_last); // Throws
    typedef _impl::TableFriend tf;
    tf::bump_version(*m_origin_table);
    return target_row_ndx;
}


void LinkView::clear()
{
    TIGHTDB_ASSERT(is_attached());

    if (!m_row_indexes.is_attached())
        return;

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_clear(*this); // Throws
#endif

    if (m_origin_column.m_weak_links) {
        bool broken_reciprocal_backlinks = false;
        do_clear(broken_reciprocal_backlinks); // Throws
        return;
    }

    size_t origin_row_ndx = get_origin_row_index();
    ColumnBase::CascadeState state;
    state.stop_on_link_list_column  = &m_origin_column;
    state.stop_on_link_list_row_ndx = origin_row_ndx;

    typedef _impl::TableFriend tf;
    size_t num_links = m_row_indexes.size();
    for (size_t link_ndx = 0; link_ndx < num_links; ++link_ndx) {
        size_t target_row_ndx = m_row_indexes.get(link_ndx);
        m_origin_column.remove_backlink(target_row_ndx, origin_row_ndx); // Throws
        Table& target_table = m_origin_column.get_target_table();
        size_t num_remaining = target_table.get_num_strong_backlinks(target_row_ndx);
        if (num_remaining > 0)
            continue;
        ColumnBase::CascadeState::row target_row;
        target_row.table_ndx = target_table.get_index_in_group();
        target_row.row_ndx   = target_row_ndx;
        typedef ColumnBase::CascadeState::row_set::iterator iter;
        iter i = ::upper_bound(state.rows.begin(), state.rows.end(), target_row);
        // This target row cannot already be in state.rows
        TIGHTDB_ASSERT(i == state.rows.begin() || i[-1] != target_row);
        state.rows.insert(i, target_row);
        tf::cascade_break_backlinks_to(target_table, target_row_ndx, state); // Throws
    }

    bool broken_reciprocal_backlinks = true;
    do_clear(broken_reciprocal_backlinks); // Throws

    tf::remove_backlink_broken_rows(*m_origin_table, state.rows); // Throws
}


// Replication instruction 'link-list-clear' calls this function directly.
void LinkView::do_clear(bool broken_reciprocal_backlinks)
{
    size_t origin_row_ndx = get_origin_row_index();
    if (!broken_reciprocal_backlinks) {
        size_t num_links = m_row_indexes.size();
        for (size_t link_ndx = 0; link_ndx < num_links; ++link_ndx) {
            size_t target_row_ndx = m_row_indexes.get(link_ndx);
            m_origin_column.remove_backlink(target_row_ndx, origin_row_ndx); // Throws
        }
    }

    m_row_indexes.destroy();
    m_origin_column.set_row_ref(origin_row_ndx, 0); // Throws

    typedef _impl::TableFriend tf;
    tf::bump_version(*m_origin_table);
}


void LinkView::sort(size_t column, bool ascending)
{
    std::vector<size_t> c;
    std::vector<bool> a;
    c.push_back(column);
    a.push_back(ascending);
    sort(c, a);
}


void LinkView::sort(std::vector<size_t> columns, std::vector<bool> ascending)
{
#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl()) {
        // todo, write to the replication log that we're doing a sort
        repl->set_link_list(*this, m_row_indexes); // Throws
    }
#endif
    Sorter predicate(columns, ascending);
    RowIndexes::sort(predicate);
}


TableView LinkView::get_sorted_view(vector<size_t> column_indexes, vector<bool> ascending) const
{
    TableView v(m_origin_column.get_target_table()); // sets m_table
    v.m_last_seen_version = m_origin_table->m_version;
    // sets m_linkview_source to indicate that this TableView was generated from a LinkView
    v.m_linkview_source = ConstLinkViewRef(this);
    if (m_row_indexes.is_attached()) {
        for (size_t t = 0; t < m_row_indexes.size(); t++) // todo, simpler way?
            v.m_row_indexes.add(get(t).get_index());
        v.sort(column_indexes, ascending);
    }
    return v;
}

TableView LinkView::get_sorted_view(size_t column_index, bool ascending) const
{
    vector<size_t> vec;
    vector<bool> a;
    vec.push_back(column_index);
    a.push_back(ascending);
    TableView v = get_sorted_view(vec, a);
    return v;
}


void LinkView::remove_target_row(size_t link_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_row_indexes.is_attached() && link_ndx < m_row_indexes.size());

    size_t target_row_ndx = m_row_indexes.get(link_ndx);
    Table& target_table = get_target_table();

    // Deleting the target row will automatically remove all links
    // to it. So we do not have to manually remove the deleted link
    target_table.move_last_over(target_row_ndx);
}


void LinkView::remove_all_target_rows()
{
    TIGHTDB_ASSERT(is_attached());

    Table& target_table = get_target_table();

    // Delete all rows targeted by links. We have to keep checking the
    // size as the list may contain multiple links to the same row, so
    // one delete could remove multiple entries.
    while (size_t count = size()) {
        size_t last_link_ndx = count-1;
        size_t target_row_ndx = m_row_indexes.get(last_link_ndx);

        // Deleting the target row will automatically remove all links
        // to it. So we do not have to manually remove the deleted link
        target_table.move_last_over(target_row_ndx);
    }
}


void LinkView::do_nullify_link(size_t old_target_row_ndx)
{
    TIGHTDB_ASSERT(m_row_indexes.is_attached());

    size_t pos = m_row_indexes.find_first(old_target_row_ndx);
    TIGHTDB_ASSERT_3(pos, !=, tightdb::not_found);

    bool is_last = (pos + 1 == m_row_indexes.size());
    m_row_indexes.erase(pos, is_last);

    if (m_row_indexes.is_empty()) {
        m_row_indexes.destroy();
        size_t origin_row_ndx = get_origin_row_index();
        m_origin_column.set_row_ref(origin_row_ndx, 0);
    }
}


void LinkView::do_update_link(size_t old_target_row_ndx, size_t new_target_row_ndx)
{
    TIGHTDB_ASSERT(m_row_indexes.is_attached());

    size_t pos = m_row_indexes.find_first(old_target_row_ndx);
    TIGHTDB_ASSERT_3(pos, !=, tightdb::not_found);

    m_row_indexes.set(pos, new_target_row_ndx);
}


#ifdef TIGHTDB_ENABLE_REPLICATION

void LinkView::repl_unselect() TIGHTDB_NOEXCEPT
{
    if (Replication* repl = get_repl())
        repl->on_link_list_destroyed(*this);
}

#endif // TIGHTDB_ENABLE_REPLICATION


#ifdef TIGHTDB_DEBUG

void LinkView::Verify(size_t row_ndx) const
{
    // Only called for attached lists
    TIGHTDB_ASSERT(is_attached());

    TIGHTDB_ASSERT_3(m_row_indexes.get_root_array()->get_ndx_in_parent(), ==, row_ndx);
    bool not_degenerate = m_row_indexes.get_root_array()->get_ref_from_parent() != 0;
    TIGHTDB_ASSERT_3(not_degenerate, ==, m_row_indexes.is_attached());
    if (m_row_indexes.is_attached())
        m_row_indexes.Verify();
}

#endif // TIGHTDB_DEBUG
