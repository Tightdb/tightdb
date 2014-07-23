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

#include <tightdb/link_view.hpp>
#include <tightdb/column_linklist.hpp>
#ifdef TIGHTDB_ENABLE_REPLICATION
#  include <tightdb/replication.hpp>
#endif

using namespace tightdb;


void LinkView::insert(std::size_t link_ndx, std::size_t target_row_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_target_row_indexes.is_attached() || link_ndx == 0);
    TIGHTDB_ASSERT(!m_target_row_indexes.is_attached() || link_ndx <= m_target_row_indexes.size());
    TIGHTDB_ASSERT(target_row_ndx < m_origin_column.get_target_table().size());
    m_origin_table->bump_version();

    size_t row_ndx = get_origin_row_index();

    // if there are no links yet, we have to create list
    if (!m_target_row_indexes.is_attached()) {
        TIGHTDB_ASSERT(link_ndx == 0);
        ref_type col_ref = Column::create(Array::type_Normal, 0, 0, m_origin_column.get_alloc());
        m_origin_column.set_row_ref(row_ndx, col_ref);
        m_target_row_indexes.get_root_array()->init_from_parent(); // re-attach
    }

    m_target_row_indexes.insert(link_ndx, target_row_ndx);
    m_origin_column.add_backlink(target_row_ndx, row_ndx);

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_insert(*this, link_ndx, target_row_ndx); // Throws
#endif
}


void LinkView::set(std::size_t link_ndx, std::size_t target_row_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_target_row_indexes.is_attached() && link_ndx < m_target_row_indexes.size());
    TIGHTDB_ASSERT(target_row_ndx < m_origin_column.get_target_table().size());
    m_origin_table->bump_version();

    // update backlinks
    size_t row_ndx = get_origin_row_index();
    size_t old_target_row_ndx = m_target_row_indexes.get(link_ndx);
    m_origin_column.remove_backlink(old_target_row_ndx, row_ndx);
    m_origin_column.add_backlink(target_row_ndx, row_ndx);
    m_target_row_indexes.set(link_ndx, target_row_ndx);

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_set(*this, link_ndx, target_row_ndx); // Throws
#endif
}


void LinkView::move(size_t old_link_ndx, size_t new_link_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_target_row_indexes.is_attached());
    TIGHTDB_ASSERT(old_link_ndx < m_target_row_indexes.size());
    TIGHTDB_ASSERT(new_link_ndx <= m_target_row_indexes.size());

    if (old_link_ndx == new_link_ndx)
        return;
    m_origin_table->bump_version();

    size_t link_ndx = (new_link_ndx <= old_link_ndx) ? new_link_ndx : new_link_ndx-1;
    size_t target_row_ndx = m_target_row_indexes.get(old_link_ndx);
    bool is_last = (old_link_ndx+1 == m_target_row_indexes.size());
    m_target_row_indexes.erase(old_link_ndx, is_last);
    m_target_row_indexes.insert(link_ndx, target_row_ndx);

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_move(*this, old_link_ndx, new_link_ndx); // Throws
#endif
}


void LinkView::remove(std::size_t link_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_target_row_indexes.is_attached() && link_ndx < m_target_row_indexes.size());
    m_origin_table->bump_version();

    // update backlinks
    size_t target_row_ndx = m_target_row_indexes.get(link_ndx);
    size_t row_ndx = get_origin_row_index();
    m_origin_column.remove_backlink(target_row_ndx, row_ndx);

    bool is_last = (link_ndx+1 == m_target_row_indexes.size());
    m_target_row_indexes.erase(link_ndx, is_last);

    if (m_target_row_indexes.is_empty()) {
        m_target_row_indexes.detach();
        m_origin_column.set_row_ref(row_ndx, 0);
    }

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_erase(*this, link_ndx); // Throws
#endif
}


void LinkView::clear()
{
    TIGHTDB_ASSERT(is_attached());

    if (!m_target_row_indexes.is_attached())
        return;

    m_origin_table->bump_version();

    // Update backlinks
    size_t row_ndx = get_origin_row_index();
    size_t n = m_target_row_indexes.size();
    for (size_t i = 0; i < n; ++i) {
        size_t target_row_ndx = m_target_row_indexes.get(i);
        m_origin_column.remove_backlink(target_row_ndx, row_ndx);
    }

    m_target_row_indexes.destroy();
    m_origin_column.set_row_ref(row_ndx, 0);

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (Replication* repl = get_repl())
        repl->link_list_clear(*this); // Throws
#endif
}


void LinkView::remove_target_row(std::size_t link_ndx)
{
    TIGHTDB_ASSERT(is_attached());
    TIGHTDB_ASSERT(m_target_row_indexes.is_attached() && link_ndx < m_target_row_indexes.size());

    size_t target_row_ndx = m_target_row_indexes.get(link_ndx);
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
        size_t target_row_ndx = m_target_row_indexes.get(last_link_ndx);

        // Deleting the target row will automatically remove all links
        // to it. So we do not have to manually remove the deleted link
        target_table.move_last_over(target_row_ndx);
    }
}


void LinkView::do_nullify_link(std::size_t old_target_row_ndx)
{
    TIGHTDB_ASSERT(m_target_row_indexes.is_attached());

    size_t pos = m_target_row_indexes.find_first(old_target_row_ndx);
    TIGHTDB_ASSERT(pos != not_found);

    bool is_last = (pos+1 == m_target_row_indexes.size());
    m_target_row_indexes.erase(pos, is_last);

    if (m_target_row_indexes.is_empty()) {
        m_target_row_indexes.destroy();
        size_t row_ndx = get_origin_row_index();
        m_origin_column.set_row_ref(row_ndx, 0);
    }
}


void LinkView::do_update_link(size_t old_target_row_ndx, std::size_t new_target_row_ndx)
{
    TIGHTDB_ASSERT(m_target_row_indexes.is_attached());

    size_t pos = m_target_row_indexes.find_first(old_target_row_ndx);
    TIGHTDB_ASSERT(pos != not_found);

    m_target_row_indexes.set(pos, new_target_row_ndx);
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

    TIGHTDB_ASSERT(m_target_row_indexes.get_root_array()->get_ndx_in_parent() == row_ndx);
    bool not_degenerate = m_target_row_indexes.get_root_array()->get_ref_from_parent() != 0;
    TIGHTDB_ASSERT(not_degenerate == m_target_row_indexes.is_attached());
    if (m_target_row_indexes.is_attached())
        m_target_row_indexes.Verify();
}

#endif // TIGHTDB_DEBUG
