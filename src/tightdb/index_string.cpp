#include <cstdio>

#include <tightdb/index_string.hpp>

using namespace std;
using namespace tightdb;
using namespace tightdb::util;


namespace {

void get_child(Array& parent, size_t child_ref_ndx, Array& child) TIGHTDB_NOEXCEPT
{
    ref_type child_ref = parent.get_as_ref(child_ref_ndx);
    child.init_from_ref(child_ref);
    child.set_parent(&parent, child_ref_ndx);
}

} // anonymous namespace


Array* StringIndex::create_node(Allocator& alloc, bool is_leaf)
{
    Array::Type type = is_leaf ? Array::type_HasRefs : Array::type_InnerBptreeNode;
    UniquePtr<Array> top(new Array(type, 0, 0, alloc));

    // Mark that this is part of index
    // (as opposed to columns under leaves)
    top->set_is_index_node(true);

    // Add subcolumns for leaves
    Array values(Array::type_Normal, 0, 0, alloc);
    values.ensure_minimum_width(0x7FFFFFFF); // This ensures 31 bits plus a sign bit
    top->add(values.get_ref()); // first entry in refs points to offsets
    values.set_parent(top.get(), 0);

    return top.release();
}

void StringIndex::set_target(void* target_column, StringGetter get_func) TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(target_column);
    m_target_column = target_column;
    m_get_func      = get_func;
}

StringIndex::key_type StringIndex::GetLastKey() const
{
    Array offsets(m_array->get_alloc());
    get_child(*m_array, 0, offsets);
    return key_type(offsets.back());
}

void StringIndex::set(size_t ndx, StringData old_value, StringData new_value)
{
    bool is_last = true; // To avoid updating refs
    erase(ndx, old_value, is_last);
    insert(ndx, new_value, is_last);
}

void StringIndex::insert(size_t row_ndx, StringData value, bool is_last)
{
    // If it is last item in column, we don't have to update refs
    if (!is_last)
        UpdateRefs(row_ndx, 1);

    InsertWithOffset(row_ndx, 0, value);
}

void StringIndex::InsertWithOffset(size_t row_ndx, size_t offset, StringData value)
{
    // Create 4 byte index key
    key_type key = create_key(value.substr(offset));

    TreeInsert(row_ndx, key, offset, value);
}

void StringIndex::InsertRowList(size_t ref, size_t offset, StringData value)
{
    TIGHTDB_ASSERT(!m_array->is_inner_bptree_node()); // only works in leaves

    // Create 4 byte index key
    key_type key = create_key(value.substr(offset));

    // Get subnode table
    Allocator& alloc = m_array->get_alloc();
    Array values(alloc);
    get_child(*m_array, 0, values);
    TIGHTDB_ASSERT(m_array->size() == values.size()+1);

    size_t ins_pos = values.lower_bound_int(key);
    if (ins_pos == values.size()) {
        // When key is outside current range, we can just add it
        values.add(key);
        m_array->add(ref);
        return;
    }

#ifdef TIGHTDB_DEBUG
    // Since we only use this for moving existing values to new
    // sub-indexes, there should never be an existing match.
    key_type k = key_type(values.get(ins_pos));
    TIGHTDB_ASSERT(k != key);
#endif

    // If key is not present we add it at the correct location
    values.insert(ins_pos, key);
    m_array->insert(ins_pos+1, ref);
}

void StringIndex::TreeInsert(size_t row_ndx, key_type key, size_t offset, StringData value)
{
    NodeChange nc = DoInsert(row_ndx, key, offset, value);
    switch (nc.type) {
        case NodeChange::none:
            return;
        case NodeChange::insert_before: {
            StringIndex new_node(inner_node_tag(), m_array->get_alloc());
            new_node.NodeAddKey(nc.ref1);
            new_node.NodeAddKey(get_ref());
            m_array->init_from_ref(new_node.get_ref());
            m_array->update_parent();
            return;
        }
        case NodeChange::insert_after: {
            StringIndex new_node(inner_node_tag(), m_array->get_alloc());
            new_node.NodeAddKey(get_ref());
            new_node.NodeAddKey(nc.ref1);
            m_array->init_from_ref(new_node.get_ref());
            m_array->update_parent();
            return;
        }
        case NodeChange::split: {
            StringIndex new_node(inner_node_tag(), m_array->get_alloc());
            new_node.NodeAddKey(nc.ref1);
            new_node.NodeAddKey(nc.ref2);
            m_array->init_from_ref(new_node.get_ref());
            m_array->update_parent();
            return;
        }
    }
    TIGHTDB_ASSERT(false);
}

StringIndex::NodeChange StringIndex::DoInsert(size_t row_ndx, key_type key, size_t offset, StringData value)
{
    Allocator& alloc = m_array->get_alloc();
    if (!root_is_leaf()) {
        // Get subnode table
        Array offsets(alloc);
        get_child(*m_array, 0, offsets);
        TIGHTDB_ASSERT(m_array->size() == offsets.size()+1);

        // Find the subnode containing the item
        size_t node_ndx = offsets.lower_bound_int(key);
        if (node_ndx == offsets.size()) {
            // node can never be empty, so try to fit in last item
            node_ndx = offsets.size()-1;
        }

        // Get sublist
        size_t refs_ndx = node_ndx+1; // first entry in refs points to offsets
        ref_type ref = m_array->get_as_ref(refs_ndx);
        StringIndex target(ref, m_array, refs_ndx, m_target_column, m_get_func, alloc);

        // Insert item
        const NodeChange nc = target.DoInsert(row_ndx, key, offset, value);
        if (nc.type ==  NodeChange::none) {
            // update keys
            key_type last_key = target.GetLastKey();
            offsets.set(node_ndx, last_key);
            return NodeChange::none; // no new nodes
        }

        if (nc.type == NodeChange::insert_after) {
            ++node_ndx;
            ++refs_ndx;
        }

        // If there is room, just update node directly
        if (offsets.size() < TIGHTDB_MAX_LIST_SIZE) {
            if (nc.type == NodeChange::split) {
                NodeInsertSplit(node_ndx, nc.ref2);
            }
            else {
                NodeInsert(node_ndx, nc.ref1); // ::INSERT_BEFORE/AFTER
            }
            return NodeChange::none;
        }

        // Else create new node
        StringIndex new_node(inner_node_tag(), alloc);
        if (nc.type == NodeChange::split) {
            // update offset for left node
            key_type last_key = target.GetLastKey();
            offsets.set(node_ndx, last_key);

            new_node.NodeAddKey(nc.ref2);
            ++node_ndx;
            ++refs_ndx;
        }
        else {
            new_node.NodeAddKey(nc.ref1);
        }

        switch (node_ndx) {
            case 0:             // insert before
                return NodeChange(NodeChange::insert_before, new_node.get_ref());
            case TIGHTDB_MAX_LIST_SIZE: // insert after
                if (nc.type == NodeChange::split)
                    return NodeChange(NodeChange::split, get_ref(), new_node.get_ref());
                return NodeChange(NodeChange::insert_after, new_node.get_ref());
            default:            // split
                // Move items after split to new node
                size_t len = m_array->size();
                for (size_t i = refs_ndx; i < len; ++i) {
                    ref_type ref = m_array->get_as_ref(i);
                    new_node.NodeAddKey(ref);
                }
                offsets.truncate(node_ndx);
                m_array->truncate(refs_ndx);
                return NodeChange(NodeChange::split, get_ref(), new_node.get_ref());
        }
    }
    else {
        // Is there room in the list?
        Array old_offsets(m_array->get_alloc());
        get_child(*m_array, 0, old_offsets);
        TIGHTDB_ASSERT(m_array->size() == old_offsets.size()+1);

        size_t count = old_offsets.size();
        bool noextend = count >= TIGHTDB_MAX_LIST_SIZE;

        // See if we can fit entry into current leaf
        // Works if there is room or it can join existing entries
        if (LeafInsert(row_ndx, key, offset, value, noextend))
            return NodeChange::none;

        // Create new list for item (a leaf)
        StringIndex new_list(m_target_column, m_get_func, m_array->get_alloc());

        new_list.LeafInsert(row_ndx, key, offset, value);

        size_t ndx = old_offsets.lower_bound_int(key);

        // insert before
        if (ndx == 0)
            return NodeChange(NodeChange::insert_before, new_list.get_ref());

        // insert after
        if (ndx == old_offsets.size())
            return NodeChange(NodeChange::insert_after, new_list.get_ref());

        // split
        Array new_offsets(alloc);
        get_child(*new_list.m_array, 0, new_offsets);
        // Move items after split to new list
        for (size_t i = ndx; i < count; ++i) {
            int64_t v2 = old_offsets.get(i);
            int64_t v3 = m_array->get(i+1);

            new_offsets.add(v2);
            new_list.m_array->add(v3);
        }
        old_offsets.truncate(ndx);
        m_array->truncate(ndx+1);

        return NodeChange(NodeChange::split, get_ref(), new_list.get_ref());
    }

    TIGHTDB_ASSERT(false); // never reach here
    return NodeChange::none;
}

void StringIndex::NodeInsertSplit(size_t ndx, size_t new_ref)
{
    TIGHTDB_ASSERT(!root_is_leaf());
    TIGHTDB_ASSERT(new_ref);

    Allocator& alloc = m_array->get_alloc();
    Array offsets(alloc);
    get_child(*m_array, 0, offsets);

    TIGHTDB_ASSERT(m_array->size() == offsets.size()+1);
    TIGHTDB_ASSERT(ndx < offsets.size());
    TIGHTDB_ASSERT(offsets.size() < TIGHTDB_MAX_LIST_SIZE);

    // Get sublists
    size_t refs_ndx = ndx+1; // first entry in refs points to offsets
    ref_type orig_ref = m_array->get_as_ref(refs_ndx);
    StringIndex orig_col(orig_ref, m_array, refs_ndx, m_target_column, m_get_func, alloc);
    StringIndex new_col(new_ref, 0, 0, m_target_column, m_get_func, alloc);

    // Update original key
    key_type last_key = orig_col.GetLastKey();
    offsets.set(ndx, last_key);

    // Insert new ref
    key_type new_key = new_col.GetLastKey();
    offsets.insert(ndx+1, new_key);
    m_array->insert(ndx+2, new_ref);
}

void StringIndex::NodeInsert(size_t ndx, size_t ref)
{
    TIGHTDB_ASSERT(ref);
    TIGHTDB_ASSERT(!root_is_leaf());

    Allocator& alloc = m_array->get_alloc();
    Array offsets(alloc);
    get_child(*m_array, 0, offsets);
    TIGHTDB_ASSERT(m_array->size() == offsets.size()+1);

    TIGHTDB_ASSERT(ndx <= offsets.size());
    TIGHTDB_ASSERT(offsets.size() < TIGHTDB_MAX_LIST_SIZE);

    StringIndex col(ref, 0, 0, m_target_column, m_get_func, alloc);
    key_type last_key = col.GetLastKey();

    offsets.insert(ndx, last_key);
    m_array->insert(ndx+1, ref);
}

bool StringIndex::LeafInsert(size_t row_ndx, key_type key, size_t offset, StringData value, bool noextend)
{
    TIGHTDB_ASSERT(root_is_leaf());

    // Get subnode table
    Allocator& alloc = m_array->get_alloc();
    Array values(alloc);
    get_child(*m_array, 0, values);
    TIGHTDB_ASSERT(m_array->size() == values.size()+1);

    size_t ins_pos = values.lower_bound_int(key);
    size_t ins_pos_refs = ins_pos + 1; // first entry in refs points to offsets
    if (ins_pos == values.size()) {
        if (noextend)
            return false;

        // When key is outside current range, we can just add it
        values.add(key);
        int64_t shifted = int64_t((uint64_t(row_ndx) << 1) + 1); // shift to indicate literal
        m_array->add(shifted);
        return true;
    }

    key_type k = key_type(values.get(ins_pos));

    // If key is not present we add it at the correct location
    if (k != key) {
        if (noextend)
            return false;

        values.insert(ins_pos, key);
        int64_t shifted = int64_t((uint64_t(row_ndx) << 1) + 1); // shift to indicate literal
        m_array->insert(ins_pos_refs, shifted);
        return true;
    }

    int64_t ref = m_array->get(ins_pos+1);
    size_t sub_offset = offset + 4;

    // Single match (lowest bit set indicates literal row_ndx)
    if (ref & 1) {
        size_t row_ndx2 = size_t(uint64_t(ref) >> 1);
        StringData v2 = get(row_ndx2);
        if (v2 == value) {
            // convert to list (in sorted order)
            Array row_list(Array::type_Normal, 0, 0, alloc);
            row_list.add(row_ndx < row_ndx2 ? row_ndx : row_ndx2);
            row_list.add(row_ndx < row_ndx2 ? row_ndx2 : row_ndx);
            m_array->set(ins_pos_refs, row_list.get_ref());
        }
        else {
            // convert to sub-index
            StringIndex sub_index(m_target_column, m_get_func, m_array->get_alloc());
            sub_index.InsertWithOffset(row_ndx2, sub_offset, v2);
            sub_index.InsertWithOffset(row_ndx, sub_offset, value);
            m_array->set(ins_pos_refs, sub_index.get_ref());
        }
        return true;
    }

    // If there alrady is a list of matches, we see if we fit there
    // or it has to be split into a sub-index
    if (!Array::is_index_node(to_ref(ref), alloc)) {
        Column sub(to_ref(ref), m_array, ins_pos_refs, alloc);

        size_t r1 = size_t(sub.get(0));
        StringData v2 = get(r1);
        if (v2 ==  value) {
            // find insert position (the list has to be kept in sorted order)
            // In most cases we refs will be added to the end. So we test for that
            // first to see if we can avoid the binary search for insert position
            size_t last_ref = size_t(sub.back());
            if (row_ndx > last_ref) {
                sub.add(row_ndx);
            }
            else {
                size_t pos = sub.lower_bound_int(row_ndx);
                if (pos == sub.size()) {
                    sub.add(row_ndx);
                }
                else {
                    sub.insert(pos, row_ndx);
                }
            }
        }
        else {
            StringIndex sub_index(m_target_column, m_get_func, m_array->get_alloc());
            sub_index.InsertRowList(sub.get_ref(), sub_offset, v2);
            sub_index.InsertWithOffset(row_ndx, sub_offset, value);
            m_array->set(ins_pos_refs, sub_index.get_ref());
        }
        return true;
    }

    // sub-index
    StringIndex sub_index(to_ref(ref), m_array, ins_pos_refs, m_target_column, m_get_func, alloc);
    sub_index.InsertWithOffset(row_ndx, sub_offset, value);

    return true;
}

size_t StringIndex::find_first(StringData value) const
{
    // Use direct access method
    return m_array->IndexStringFindFirst(value, m_target_column, m_get_func);
}

void StringIndex::find_all(Array& result, StringData value) const
{
    // Use direct access method
    return m_array->IndexStringFindAll(result, value, m_target_column, m_get_func);
}


FindRes StringIndex::find_all(StringData value, size_t& ref) const
{
    // Use direct access method
    return m_array->IndexStringFindAllNoCopy(value, ref, m_target_column, m_get_func);
}

size_t StringIndex::count(StringData value) const

{
    // Use direct access method
    return m_array->IndexStringCount(value, m_target_column, m_get_func);
}

void StringIndex::distinct(Array& result) const
{
    Allocator& alloc = m_array->get_alloc();
    const size_t count = m_array->size();

    // Get first matching row for every key
    if (m_array->is_inner_bptree_node()) {
        for (size_t i = 1; i < count; ++i) {
            size_t ref = m_array->get_as_ref(i);
            const StringIndex ndx(ref, 0, 0, m_target_column, m_get_func, alloc);
            ndx.distinct(result);
        }
    }
    else {
        for (size_t i = 1; i < count; ++i) {
            int64_t ref = m_array->get(i);

            // low bit set indicate literal ref (shifted)
            if (ref & 1) {
               size_t r = to_size_t((uint64_t(ref) >> 1));
               result.add(r);
            }
            else {
                // A real ref either points to a list or a sub-index
                if (Array::is_index_node(to_ref(ref), alloc)) {
                    const StringIndex ndx(to_ref(ref), m_array, i, m_target_column, m_get_func, alloc);
                    ndx.distinct(result);
                }
                else {
                    const Column sub(to_ref(ref), m_array, i, alloc);
                    size_t r = to_size_t(sub.get(0)); // get first match
                    result.add(r);
                }
            }
        }
    }
}

void StringIndex::UpdateRefs(size_t pos, int diff)
{
    TIGHTDB_ASSERT(diff == 1 || diff == -1); // only used by insert and delete

    Allocator& alloc = m_array->get_alloc();
    const size_t count = m_array->size();

    if (m_array->is_inner_bptree_node()) {
        for (size_t i = 1; i < count; ++i) {
            size_t ref = m_array->get_as_ref(i);
            StringIndex ndx(ref, m_array, i, m_target_column, m_get_func, alloc);
            ndx.UpdateRefs(pos, diff);
        }
    }
    else {
        for (size_t i = 1; i < count; ++i) {
            int64_t ref = m_array->get(i);

            // low bit set indicate literal ref (shifted)
            if (ref & 1) {
                size_t r = size_t(uint64_t(ref) >> 1);
                if (r >= pos) {
                    size_t adjusted_ref = ((r + diff) << 1)+1;
                    m_array->set(i, adjusted_ref);
                }
            }
            else {
                // A real ref either points to a list or a sub-index
                if (Array::is_index_node(to_ref(ref), alloc)) {
                    StringIndex ndx(to_ref(ref), m_array, i, m_target_column, m_get_func, alloc);
                    ndx.UpdateRefs(pos, diff);
                }
                else {
                    Column sub(to_ref(ref), m_array, i, alloc);
                    sub.adjust_ge(pos, diff);
                }
            }
        }
    }
}

void StringIndex::clear()
{
    Array values(m_array->get_alloc());
    get_child(*m_array, 0, values);
    TIGHTDB_ASSERT(m_array->size() == values.size()+1);

    values.clear();
    values.ensure_minimum_width(0x7FFFFFFF); // This ensures 31 bits plus a sign bit

    m_array->set(0, 1); // Don't delete values
    m_array->clear();
    m_array->add(values.get_ref());
    m_array->set_type(Array::type_HasRefs);
}

void StringIndex::erase(size_t row_ndx, StringData value, bool is_last)
{
    DoDelete(row_ndx, value, 0);

    // Collapse top nodes with single item
    while (!root_is_leaf()) {
        TIGHTDB_ASSERT(m_array->size() > 1); // node cannot be empty
        if (m_array->size() > 2)
            break;

        ref_type ref = m_array->get_as_ref(1);
        m_array->erase(1); // avoid deleting subtree
        m_array->destroy();
        m_array->init_from_ref(ref);
        m_array->update_parent();
    }

    // If it is last item in column, we don't have to update refs
    if (!is_last)
        UpdateRefs(row_ndx, -1);
}

void StringIndex::DoDelete(size_t row_ndx, StringData value, size_t offset)
{
    Allocator& alloc = m_array->get_alloc();
    Array values(alloc);
    get_child(*m_array, 0, values);
    TIGHTDB_ASSERT(m_array->size() == values.size()+1);

    // Create 4 byte index key
    key_type key = create_key(value.substr(offset));

    const size_t pos = values.lower_bound_int(key);
    const size_t pos_refs = pos + 1; // first entry in refs points to offsets
    TIGHTDB_ASSERT(pos != values.size());

    if (m_array->is_inner_bptree_node()) {
        ref_type ref = m_array->get_as_ref(pos_refs);
        StringIndex node(ref, m_array, pos_refs, m_target_column, m_get_func, alloc);
        node.DoDelete(row_ndx, value, offset);

        // Update the ref
        if (node.is_empty()) {
            values.erase(pos);
            m_array->erase(pos_refs);
            node.destroy();
        }
        else {
            key_type max_val = node.GetLastKey();
            if (max_val != key_type(values.get(pos)))
                values.set(pos, max_val);
        }
    }
    else {
        int64_t ref = m_array->get(pos_refs);
        if (ref & 1) {
            TIGHTDB_ASSERT((uint64_t(ref) >> 1) == uint64_t(row_ndx));
            values.erase(pos);
            m_array->erase(pos_refs);
        }
        else {
            // A real ref either points to a list or a sub-index
            if (Array::is_index_node(to_ref(ref), alloc)) {
                StringIndex subNdx(to_ref(ref), m_array, pos_refs, m_target_column, m_get_func, alloc);
                subNdx.DoDelete(row_ndx, value, offset+4);

                if (subNdx.is_empty()) {
                    values.erase(pos);
                    m_array->erase(pos_refs);
                    subNdx.destroy();
                }
            }
            else {
                Column sub(to_ref(ref), m_array, pos_refs, alloc);
                size_t r = sub.find_first(row_ndx);
                TIGHTDB_ASSERT(r != not_found);
                bool is_last = r == sub.size() - 1;
                sub.erase(r, is_last);

                if (sub.size() == 0) {
                    values.erase(pos);
                    m_array->erase(pos_refs);
                    sub.destroy();
                }
            }
        }
    }
}

void StringIndex::update_ref(StringData value, size_t old_row_ndx, size_t new_row_ndx)
{
    do_update_ref(value, old_row_ndx, new_row_ndx, 0);
}

void StringIndex::do_update_ref(StringData value, size_t row_ndx, size_t new_row_ndx, size_t offset)
{
    Allocator& alloc = m_array->get_alloc();
    Array values(alloc);
    get_child(*m_array, 0, values);
    TIGHTDB_ASSERT(m_array->size() == values.size()+1);

    // Create 4 byte index key
    key_type key = create_key(value.substr(offset));

    size_t pos = values.lower_bound_int(key);
    size_t pos_refs = pos + 1; // first entry in refs points to offsets
    TIGHTDB_ASSERT(pos != values.size());

    if (m_array->is_inner_bptree_node()) {
        ref_type ref = m_array->get_as_ref(pos_refs);
        StringIndex node(ref, m_array, pos_refs, m_target_column, m_get_func, alloc);
        node.do_update_ref(value, row_ndx, new_row_ndx, offset);
    }
    else {
        int64_t ref = m_array->get(pos_refs);
        if (ref & 1) {
            TIGHTDB_ASSERT((uint64_t(ref) >> 1) == uint64_t(row_ndx));
            size_t shifted = (new_row_ndx << 1) + 1; // shift to indicate literal
            m_array->set(pos_refs, shifted);
        }
        else {
            // A real ref either points to a list or a sub-index
            if (Array::is_index_node(to_ref(ref), alloc)) {
                StringIndex subNdx(to_ref(ref), m_array, pos_refs, m_target_column, m_get_func, alloc);
                subNdx.do_update_ref(value, row_ndx, new_row_ndx, offset+4);
            }
            else {
                Column sub(to_ref(ref), m_array, pos_refs, alloc);
                size_t r = sub.find_first(row_ndx);
                TIGHTDB_ASSERT(r != not_found);
                sub.set(r, new_row_ndx);
            }
        }
    }
}

bool StringIndex::is_empty() const
{
    return m_array->size() == 1; // first entry in refs points to offsets
}


void StringIndex::NodeAddKey(ref_type ref)
{
    TIGHTDB_ASSERT(ref);
    TIGHTDB_ASSERT(!root_is_leaf());

    Allocator& alloc = m_array->get_alloc();
    Array offsets(alloc);
    get_child(*m_array, 0, offsets);
    TIGHTDB_ASSERT(m_array->size() == offsets.size()+1);
    TIGHTDB_ASSERT(offsets.size() < TIGHTDB_MAX_LIST_SIZE+1);

    Array new_top(ref, 0, 0, m_array->get_alloc());
    Array new_offsets(new_top.get_as_ref(0), 0, 0, alloc);
    TIGHTDB_ASSERT(!new_offsets.is_empty());

    int64_t key = new_offsets.back();
    offsets.add(key);
    m_array->add(ref);
}


#ifdef TIGHTDB_DEBUG

void StringIndex::verify_entries(const AdaptiveStringColumn& column) const
{
    Array results;

    size_t count = column.size();
    for (size_t i = 0; i < count; ++i) {
        StringData value = column.get(i);

        find_all(results, value);

        size_t ndx = results.find_first(i);
        TIGHTDB_ASSERT(ndx != not_found);
        results.clear();
    }
    results.destroy(); // clean-up
}

void StringIndex::to_dot(ostream& out, StringData title) const
{
    out << "digraph G {" << endl;

    to_dot_2(out, title);

    out << "}" << endl;
}


void StringIndex::to_dot_2(ostream& out, StringData title) const
{
    ref_type ref = get_ref();

    out << "subgraph cluster_string_index" << ref << " {" << endl;
    out << " label = \"String index";
    if (title.size() != 0)
        out << "\\n'" << title << "'";
    out << "\";" << endl;

    array_to_dot(out, *m_array);

    out << "}" << endl;
}

void StringIndex::array_to_dot(ostream& out, const Array& array)
{
    if (!array.is_index_node()) {
        Column col(array.get_ref(), array.get_parent(), array.get_ndx_in_parent(), array.get_alloc());
        col.to_dot(out, "ref_list");
        return;
    }

    Allocator& alloc = array.get_alloc();
    Array offsets(alloc);
    get_child(const_cast<Array&>(array), 0, offsets);
    TIGHTDB_ASSERT(array.size() == offsets.size()+1);
    ref_type ref  = array.get_ref();

    if (array.is_inner_bptree_node()) {
        out << "subgraph cluster_string_index_inner_node" << ref << " {" << endl;
        out << " label = \"Inner node\";" << endl;
    }
    else {
        out << "subgraph cluster_string_index_leaf" << ref << " {" << endl;
        out << " label = \"Leaf\";" << endl;
    }

    array.to_dot(out);
    keys_to_dot(out, offsets, "keys");

    out << "}" << endl;

    size_t count = array.size();
    for (size_t i = 1; i < count; ++i) {
        int64_t v = array.get(i);
        if (v & 1)
            continue; // ignore literals

        Array r(alloc);
        get_child(const_cast<Array&>(array), i, r);
        array_to_dot(out, r);
    }
}

void StringIndex::keys_to_dot(ostream& out, const Array& array, StringData title)
{
    ref_type ref = array.get_ref();

    if (0 < title.size()) {
        out << "subgraph cluster_" << ref << " {" << endl;
        out << " label = \"" << title << "\";" << endl;
        out << " color = white;" << endl;
    }

    out << "n" << hex << ref << dec << "[shape=none,label=<";
    out << "<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\"><TR>" << endl;

    // Header
    out << "<TD BGCOLOR=\"lightgrey\"><FONT POINT-SIZE=\"7\"> ";
    out << "0x" << hex << ref << dec << "<BR/>";
    if (array.is_inner_bptree_node())
        out << "IsNode<BR/>";
    if (array.has_refs())
        out << "HasRefs<BR/>";
    out << "</FONT></TD>" << endl;

    // Values
    size_t count = array.size();
    for (size_t i = 0; i < count; ++i) {
        uint64_t v =  array.get(i); // Never right shift signed values

        char str[5] = "\0\0\0\0";
        str[3] = char(v & 0xFF);
        str[2] = char((v >> 8) & 0xFF);
        str[1] = char((v >> 16) & 0xFF);
        str[0] = char((v >> 24) & 0xFF);
        const char* s = str;

        out << "<TD>" << s << "</TD>" << endl;
    }

    out << "</TR></TABLE>>];" << endl;
    if (0 < title.size())
        out << "}" << endl;

    array.to_dot_parent_edge(out);

    out << endl;
}


#endif // TIGHTDB_DEBUG
