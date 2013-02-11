#include <tightdb/index_string.hpp>
#include <cstdio>

using namespace tightdb;

namespace {

// Pre-declaration
int32_t CreateKey(const char* v);

int32_t CreateKey(const char* v)
{
    // Create 4 byte index key
    // (encoded like this to allow literal comparisons
    // independently of endianness)
    int32_t key = 0;
    if (*v) key  = ((int32_t)(*v++) << 24);
    if (*v) key |= ((int32_t)(*v++) << 16);
    if (*v) key |= ((int32_t)(*v++) << 8);
    if (*v) key |=  (int32_t)(*v++);
    return key;
}

} // namespace

StringIndex::StringIndex(void* target_column, StringGetter get_func, Allocator& alloc)
: Column(coldef_HasRefs, NULL, 0, alloc), m_target_column(target_column), m_get_func(get_func)
{
    Create();
}

StringIndex::StringIndex(ColumnDef type, Allocator& alloc)
: Column(type, NULL, 0, alloc), m_target_column(NULL), m_get_func(NULL)
{
    TIGHTDB_ASSERT(type == coldef_Node); // only used for node creation at this point

    // Mark that this is part of index
    // (as opposed to columns under leafs)
    m_array->SetIsIndexNode(true);

    // no need to call create as sub-arrays have been created by column constructor
}

StringIndex::StringIndex(size_t ref, ArrayParent* parent, size_t pndx, void* target_column, StringGetter get_func, Allocator& alloc)
: Column(ref, parent, pndx, alloc), m_target_column(target_column), m_get_func(get_func)
{
    TIGHTDB_ASSERT(IsArrayIndexNode(ref, alloc));
}

void StringIndex::Create()
{
    // Mark that this is part of index
    // (as opposed to columns under leafs)
    m_array->SetIsIndexNode(true);

    // Add subcolumns for leafs
    Allocator& alloc = m_array->GetAllocator();
    Array values(coldef_Normal, NULL, 0, alloc);
    Array refs(coldef_HasRefs, NULL, 1, alloc);
    m_array->add(values.GetRef());
    m_array->add(refs.GetRef());
    values.SetParent((ArrayParent*)m_array, 0);
    refs.SetParent((ArrayParent*)m_array, 1);
}

void StringIndex::SetTarget(void* target_column, StringGetter get_func)
{
    TIGHTDB_ASSERT(target_column);
    m_target_column = target_column;
    m_get_func      = get_func;
}

int32_t StringIndex::GetLastKey() const
{
    const Array offsets =  m_array->GetSubArray(0);
    return (int32_t)offsets.back();
}

void StringIndex::Set(size_t ndx, const char* oldValue, const char* newValue)
{
    Delete(ndx, oldValue, true); // set isLast to avoid updating refs
    Insert(ndx, newValue, true); // set isLast to avoid updating refs
}

void StringIndex::Insert(size_t row_ndx, const char* value, bool isLast)
{
    // If it is last item in column, we don't have to update refs
    if (!isLast) UpdateRefs(row_ndx, 1);

    InsertWithOffset(row_ndx, 0, value);
}

void StringIndex::InsertWithOffset(size_t row_ndx, size_t offset, const char* value)
{
    // Create 4 byte index key
    const char* const v = value + offset;
    const int32_t key = CreateKey(v);

    TreeInsert(row_ndx, key, offset, value);
}

void StringIndex::InsertRowList(size_t ref, size_t offset, const char* value)
{
    TIGHTDB_ASSERT(!m_array->IsNode()); // only works in leafs

    // Create 4 byte index key
    const char* const v = value + offset;
    const int32_t key = CreateKey(v);

    // Get subnode table
    Array values = m_array->GetSubArray(0);
    Array refs = m_array->GetSubArray(1);

    const size_t ins_pos = values.FindPos2(key);

    if (ins_pos == not_found) {
        // When key is outside current range, we can just add it
        values.add(key);
        refs.add(ref);
        return;
    }

#ifdef TIGHTDB_DEBUG
    // Since we only use this for moving existing values to new
    // sub-indexes, there should never be an existing match.
    const int32_t k = (int32_t)values.Get(ins_pos);
    TIGHTDB_ASSERT(k != key);
#endif

    // If key is not present we add it at the correct location
    values.Insert(ins_pos, key);
    refs.Insert(ins_pos, ref);
}

void StringIndex::TreeInsert(size_t row_ndx, int32_t key, size_t offset, const char* value)
{
    const NodeChange nc = DoInsert(row_ndx, key, offset, value);
    switch (nc.type) {
        case NodeChange::none:
            return;
        case NodeChange::insert_before: {
            StringIndex newNode(coldef_Node, m_array->GetAllocator());
            newNode.NodeAddKey(nc.ref1);
            newNode.NodeAddKey(GetRef());
            UpdateRef(newNode.GetRef());
            return;
        }
        case NodeChange::insert_after: {
            StringIndex newNode(coldef_Node, m_array->GetAllocator());
            newNode.NodeAddKey(GetRef());
            newNode.NodeAddKey(nc.ref1);
            UpdateRef(newNode.GetRef());
            return;
        }
        case NodeChange::split: {
            StringIndex newNode(coldef_Node, m_array->GetAllocator());
            newNode.NodeAddKey(nc.ref1);
            newNode.NodeAddKey(nc.ref2);
            UpdateRef(newNode.GetRef());
            return;
        }
    }
    TIGHTDB_ASSERT(false);
}

Column::NodeChange StringIndex::DoInsert(size_t row_ndx, int32_t key, size_t offset, const char* value)
{
    if (IsNode()) {
        // Get subnode table
        Array offsets = NodeGetOffsets();
        Array refs = NodeGetRefs();

        // Find the subnode containing the item
        size_t node_ndx = offsets.FindPos2(key);
        if (node_ndx == (size_t)-1) {
            // node can never be empty, so try to fit in last item
            node_ndx = offsets.Size()-1;
        }

        // Get sublist
        const size_t ref = refs.GetAsRef(node_ndx);
        StringIndex target(ref, &refs, node_ndx, m_target_column, m_get_func, m_array->GetAllocator());

        // Insert item
        const NodeChange nc = target.DoInsert(row_ndx, key, offset, value);
        if (nc.type ==  NodeChange::none) {
            // update keys
            const int64_t lastKey = target.GetLastKey();
            offsets.Set(node_ndx, lastKey);
            return NodeChange::none; // no new nodes
        }

        if (nc.type == NodeChange::insert_after) ++node_ndx;

        // If there is room, just update node directly
        if (offsets.Size() < TIGHTDB_MAX_LIST_SIZE) {
            if (nc.type == NodeChange::split) NodeInsertSplit(node_ndx, nc.ref2);
            else NodeInsert(node_ndx, nc.ref1); // ::INSERT_BEFORE/AFTER
            return NodeChange::none;
        }

        // Else create new node
        StringIndex newNode(coldef_Node, m_array->GetAllocator());
        if (nc.type == NodeChange::split) {
            // update offset for left node
            const int32_t lastKey = target.GetLastKey();
            offsets.Set(node_ndx, lastKey);

            newNode.NodeAddKey(nc.ref2);
            ++node_ndx;
        }
        else newNode.NodeAddKey(nc.ref1);

        switch (node_ndx) {
            case 0:             // insert before
                return NodeChange(NodeChange::insert_before, newNode.GetRef());
            case TIGHTDB_MAX_LIST_SIZE: // insert after
                if (nc.type == NodeChange::split)
                    return NodeChange(NodeChange::split, GetRef(), newNode.GetRef());
                else return NodeChange(NodeChange::insert_after, newNode.GetRef());
            default:            // split
                // Move items after split to new node
                const size_t len = refs.Size();
                for (size_t i = node_ndx; i < len; ++i) {
                    const size_t ref = refs.GetAsRef(i);
                    newNode.NodeAddKey(ref);
                }
                offsets.Resize(node_ndx);
                refs.Resize(node_ndx);
                return NodeChange(NodeChange::split, GetRef(), newNode.GetRef());
        }
    }
    else {
        // Is there room in the list?
        Array old_offsets = m_array->GetSubArray(0);
        const size_t count = old_offsets.Size();
        const bool noextend = count >= TIGHTDB_MAX_LIST_SIZE;

        // See if we can fit entry into current leaf
        // Works if there is room or it can join existing entries
        if (LeafInsert(row_ndx, key, offset, value, noextend))
            return NodeChange::none;

        // Create new list for item
        StringIndex newList(m_target_column, m_get_func, m_array->GetAllocator());

        newList.LeafInsert(row_ndx, key, offset, value);

        const size_t ndx = old_offsets.FindPos2(key);

        switch (ndx) {
            case 0:             // insert before
                return NodeChange(NodeChange::insert_before, newList.GetRef());
            case -1: // insert after
                return NodeChange(NodeChange::insert_after, newList.GetRef());
            default: // split
            {
                Array old_refs = m_array->GetSubArray(1);
                Array new_offsets = newList.m_array->GetSubArray(0);
                Array new_refs = newList.m_array->GetSubArray(1);
                // Move items after split to new list
                for (size_t i = ndx; i < count; ++i) {
                    const int64_t v2 = old_offsets.Get(i);
                    const int64_t v3 = old_refs.Get(i);

                    new_offsets.add(v2);
                    new_refs.add(v3);
                }
                old_offsets.Resize(ndx);
                old_refs.Resize(ndx);

                return NodeChange(NodeChange::split, GetRef(), newList.GetRef());
            }
        }
    }

    TIGHTDB_ASSERT(false); // never reach here
    return NodeChange::none;
}

void StringIndex::NodeInsertSplit(size_t ndx, size_t new_ref)
{
    TIGHTDB_ASSERT(IsNode());
    TIGHTDB_ASSERT(new_ref);

    Array offsets = NodeGetOffsets();
    Array refs = NodeGetRefs();

    TIGHTDB_ASSERT(ndx < offsets.Size());
    TIGHTDB_ASSERT(offsets.Size() < TIGHTDB_MAX_LIST_SIZE);

    // Get sublists
    const size_t orig_ref = refs.Get(ndx);
    const StringIndex orig_col(orig_ref, &refs, ndx, m_target_column, m_get_func, m_array->GetAllocator());
    const StringIndex new_col(new_ref, NULL, 0, m_target_column, m_get_func, m_array->GetAllocator());

    // Update original key
    const int64_t lastKey = orig_col.GetLastKey();
    offsets.Set(ndx, lastKey);

    // Insert new ref
    const size_t newKey = new_col.GetLastKey();
    offsets.Insert(ndx+1, newKey);
    refs.Insert(ndx+1, new_ref);
}

void StringIndex::NodeInsert(size_t ndx, size_t ref)
{
    TIGHTDB_ASSERT(ref);
    TIGHTDB_ASSERT(IsNode());

    Array offsets = NodeGetOffsets();
    Array refs = NodeGetRefs();

    TIGHTDB_ASSERT(ndx <= offsets.Size());
    TIGHTDB_ASSERT(offsets.Size() < TIGHTDB_MAX_LIST_SIZE);

    const StringIndex col(ref, (Array*)NULL, 0, m_target_column, m_get_func, m_array->GetAllocator());
    const int64_t lastKey = col.GetLastKey();

    offsets.Insert(ndx, lastKey);
    refs.Insert(ndx, ref);
}

bool StringIndex::LeafInsert(size_t row_ndx, int32_t key, size_t offset, const char* value, bool noextend)
{
    TIGHTDB_ASSERT(!IsNode());

    // Get subnode table
    Array values = m_array->GetSubArray(0);
    Array refs = m_array->GetSubArray(1);

    const size_t ins_pos = values.FindPos2(key);

    if (ins_pos == not_found) {
        if (noextend) return false;

        // When key is outside current range, we can just add it
        values.add(key);
        const size_t shifted = (row_ndx << 1) + 1; // shift to indicate literal
        refs.add(shifted);
        return true;
    }

    const int32_t k = (int32_t)values.Get(ins_pos);

    // If key is not present we add it at the correct location
    if (k != key) {
        if (noextend) return false;

        values.Insert(ins_pos, key);
        const size_t shifted = (row_ndx << 1) + 1; // shift to indicate literal
        refs.Insert(ins_pos, shifted);
        return true;
    }

    const size_t ref = refs.GetAsRef(ins_pos);
    const size_t sub_offset = offset + 4;
    Allocator& alloc = m_array->GetAllocator();

    // Single match (lowest bit set indicates literal row_ndx)
    if (ref & 1) {
        const size_t row_ndx2 = ref >> 1;
        const char* const v2 = Get(row_ndx2);
        if (strcmp(v2, value) == 0) {
            // convert to list (in sorted order)
            Array row_list(coldef_Normal, NULL, 0, alloc);
            row_list.add(row_ndx < row_ndx2 ? row_ndx : row_ndx2);
            row_list.add(row_ndx < row_ndx2 ? row_ndx2 : row_ndx);
            refs.Set(ins_pos, row_list.GetRef());
        }
        else {
            // convert to sub-index
            StringIndex sub_index(m_target_column, m_get_func, alloc);
            sub_index.InsertWithOffset(row_ndx2, sub_offset, v2);
            sub_index.InsertWithOffset(row_ndx, sub_offset, value);
            refs.Set(ins_pos, sub_index.GetRef());
        }
        return true;
    }

    // If there alrady is a list of matches, we see if we fit there
    // or it has to be split into a sub-index
    if (!IsArrayIndexNode(ref, alloc)) {
        Column sub(ref, &refs, ins_pos, alloc);

        const size_t r1 = (size_t)sub.Get(0);
        const char* const v2 = Get(r1);
        if (strcmp(v2, value) == 0) {
            // find insert position (the list has to be kept in sorted order)
            // In most cases we refs will be added to the end. So we test for that
            // first to see if we can avoid the binary search for insert position
            const size_t lastRef = (size_t)sub.Back();
            if (row_ndx > lastRef)
                sub.add(row_ndx);
            else {
                const size_t pos = sub.find_pos2(row_ndx);
                if (pos == not_found)
                    sub.add(row_ndx);
                else
                    sub.Insert(pos, row_ndx);
            }
        }
        else {
            StringIndex sub_index(m_target_column, m_get_func, alloc);
            sub_index.InsertRowList(sub.GetRef(), sub_offset, v2);
            sub_index.InsertWithOffset(row_ndx, sub_offset, value);
            refs.Set(ins_pos, sub_index.GetRef());
        }
        return true;
    }

    // sub-index
    StringIndex sub_index(ref, &refs, ins_pos, m_target_column, m_get_func, alloc);
    sub_index.InsertWithOffset(row_ndx, sub_offset, value);

    return true;
}

size_t StringIndex::find_first(const char* value) const
{
    // Use direct access method
    return m_array->IndexStringFindFirst(value, m_target_column, m_get_func);
}

void StringIndex::find_all(Array& result, const char* value) const
{
    // Use direct access method
    return m_array->IndexStringFindAll(result, value, m_target_column, m_get_func);
}

size_t StringIndex::count(const char* value) const
{
    // Use direct access method
    return m_array->IndexStringCount(value, m_target_column, m_get_func);
}

void StringIndex::distinct(Array& result) const
{
    Array refs = m_array->GetSubArray(1);
    const size_t count = refs.Size();
    Allocator& alloc = m_array->GetAllocator();

    // Get first matching row for every key
    if (m_array->IsNode()) {
        for (size_t i = 0; i < count; ++i) {
            const size_t ref = (size_t)refs.Get(i);
            const StringIndex ndx(ref, NULL, 0, m_target_column, m_get_func, alloc);
            ndx.distinct(result);
        }
    }
    else {
        for (size_t i = 0; i < count; ++i) {
            const int64_t ref = refs.Get(i);

            // low bit set indicate literal ref (shifted)
            if (ref & 1) {
                const size_t r = (ref >> 1);
                result.add(r);
            }
            else {
                // A real ref either points to a list or a sub-index
                if (IsArrayIndexNode(ref, alloc)) {
                    const StringIndex ndx((size_t)ref, &refs, i, m_target_column, m_get_func, alloc);
                    ndx.distinct(result);
                }
                else {
                    const Column sub(ref, &refs, i, alloc);
                    const size_t r = sub.Get(0); // get first match
                    result.add(r);
                }
            }
        }
    }
}

void StringIndex::UpdateRefs(size_t pos, int diff)
{
    TIGHTDB_ASSERT(diff == 1 || diff == -1); // only used by insert and delete

    Array refs = m_array->GetSubArray(1);
    const size_t count = refs.Size();
    Allocator& alloc = m_array->GetAllocator();

    if (m_array->IsNode()) {
        for (size_t i = 0; i < count; ++i) {
            const size_t ref = (size_t)refs.Get(i);
            StringIndex ndx(ref, &refs, i, m_target_column, m_get_func, alloc);
            ndx.UpdateRefs(pos, diff);
        }
    }
    else {
        for (size_t i = 0; i < count; ++i) {
            const int64_t ref = refs.Get(i);

            // low bit set indicate literal ref (shifted)
            if (ref & 1) {
                const size_t r = (ref >> 1);
                if (r >= pos) {
                    const size_t adjusted_ref = ((r + diff) << 1)+1;
                    refs.Set(i, adjusted_ref);
                }
            }
            else {
                // A real ref either points to a list or a sub-index
                if (IsArrayIndexNode(ref, alloc)) {
                    StringIndex ndx((size_t)ref, &refs, i, m_target_column, m_get_func, alloc);
                    ndx.UpdateRefs(pos, diff);
                }
                else {
                    Column sub(ref, &refs, i, alloc);
                    sub.IncrementIf(pos, diff);
                }
            }
        }
    }
}

void StringIndex::Clear()
{
    Array values = m_array->GetSubArray(0);
    Array refs   = m_array->GetSubArray(1);
    values.Clear();
    refs.Clear();
}

void StringIndex::Delete(size_t row_ndx, const char* value, bool isLast)
{
    DoDelete(row_ndx, value, 0);

    // Collapse top nodes with single item
    while (IsNode()) {
        Array refs = m_array->GetSubArray(1);
        TIGHTDB_ASSERT(refs.Size() != 0); // node cannot be empty
        if (refs.Size() > 1) break;

        const size_t ref = (size_t)refs.Get(0);
        refs.Delete(0); // avoid deleting subtree
        m_array->Destroy();
        m_array->UpdateRef(ref);
    }

    // If it is last item in column, we don't have to update refs
    if (!isLast) UpdateRefs(row_ndx, -1);
}

void StringIndex::DoDelete(size_t row_ndx, const char* value, size_t offset)
{
    Array values = m_array->GetSubArray(0);
    Array refs = m_array->GetSubArray(1);
    Allocator& alloc = m_array->GetAllocator();

    // Create 4 byte index key
    const char* const v = value + offset;
    const int32_t key = CreateKey(v);

    const size_t pos = values.FindPos2(key);
    TIGHTDB_ASSERT(pos != not_found);

    if (m_array->IsNode()) {
        const size_t ref = refs.Get(pos);
        StringIndex node(ref, &refs, pos, m_target_column, m_get_func, alloc);
        node.DoDelete(row_ndx, value, offset);

        // Update the ref
        if (node.is_empty()) {
            values.Delete(pos);
            refs.Delete(pos);
            node.Destroy();
        }
        else {
            const int64_t maxval = node.GetLastKey();
            if (maxval != values.Get(pos))
                values.Set(pos, maxval);
        }
    }
    else {
        const int64_t ref = refs.Get(pos);
        if (ref & 1) {
            TIGHTDB_ASSERT((ref >> 1) == (int64_t)row_ndx);
            values.Delete(pos);
            refs.Delete(pos);
        }
        else {
            // A real ref either points to a list or a sub-index
            if (IsArrayIndexNode(ref, alloc)) {
                StringIndex subNdx((size_t)ref, &refs, pos, m_target_column, m_get_func, alloc);
                subNdx.DoDelete(row_ndx, value, offset+4);

                if (subNdx.is_empty()) {
                    values.Delete(pos);
                    refs.Delete(pos);
                    subNdx.Destroy();
                }
            }
            else {
                Column sub(ref, &refs, pos, alloc);
                const size_t r = sub.find_first(row_ndx);
                TIGHTDB_ASSERT(r != not_found);
                sub.Delete(r);

                if (sub.is_empty()) {
                    values.Delete(pos);
                    refs.Delete(pos);
                    sub.Destroy();
                }
            }
        }
    }
}

bool StringIndex::is_empty() const
{
    const Array values = m_array->GetSubArray(0);
    return values.is_empty();
}

#ifdef TIGHTDB_DEBUG

void StringIndex::verify_entries(const AdaptiveStringColumn& column) const
{
    Array results;

    const size_t count = column.Size();
    for (size_t i = 0; i < count; ++i) {
        const char* const value = column.Get(i);

        find_all(results, value);

        const size_t has_match = results.find_first(i);
        if (has_match == not_found) {
            TIGHTDB_ASSERT(false);
        }
        results.Clear();
    }
    results.Destroy(); // clean-up
}

void StringIndex::to_dot(std::ostream& out) const
{
    out << "digraph G {" << std::endl;

    ToDot(out);

    out << "}" << std::endl;
}


void StringIndex::ToDot(std::ostream& out, const char* title) const
{
    const size_t ref = GetRef();

    out << "subgraph cluster_stringindex" << ref << " {" << std::endl;
    out << " label = \"StringIndex";
    if (title) out << "\\n'" << title << "'";
    out << "\";" << std::endl;

    ArrayToDot(out, *m_array);

    out << "}" << std::endl;
}

void StringIndex::ArrayToDot(std::ostream& out, const Array& array) const
{
    if (array.HasRefs()) {
        const Array offsets = array.GetSubArray(0);
        const Array refs    = array.GetSubArray(1);
        const size_t ref    = array.GetRef();

        if (array.IsNode()) {
            out << "subgraph cluster_stringindex_node" << ref << " {" << std::endl;
            out << " label = \"Node\";" << std::endl;
        }
        else {
            out << "subgraph cluster_stringindex_leaf" << ref << " {" << std::endl;
            out << " label = \"Leaf\";" << std::endl;
        }

        array.ToDot(out);
        KeysToDot(out, offsets, "keys");

        out << "}" << std::endl;

        refs.ToDot(out, "refs");

        const size_t count = refs.Size();
        for (size_t i = 0; i < count; ++i) {
            const size_t ref = refs.GetAsRef(i);
            if (ref & 1) continue; // ignore literals

            const Array r = refs.GetSubArray(i);
            ArrayToDot(out, r);
        }
    }
    else {
        array.ToDot(out);
    }
}

void StringIndex::KeysToDot(std::ostream& out, const Array& array, const char* title) const
{
    const size_t ref = array.GetRef();

    if (title) {
        out << "subgraph cluster_" << ref << " {" << std::endl;
        out << " label = \"" << title << "\";" << std::endl;
        out << " color = white;" << std::endl;
    }

    out << "n" << std::hex << ref << std::dec << "[shape=none,label=<";
    out << "<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\"><TR>" << std::endl;

    // Header
    out << "<TD BGCOLOR=\"lightgrey\"><FONT POINT-SIZE=\"7\"> ";
    out << "0x" << std::hex << ref << std::dec << "<BR/>";
    if (array.IsNode()) out << "IsNode<BR/>";
    if (array.HasRefs()) out << "HasRefs<BR/>";
    out << "</FONT></TD>" << std::endl;

    // Values
    const size_t count = array.Size();
    for (size_t i = 0; i < count; ++i) {
        const int64_t v =  array.Get(i);

        char str[5] = "\0\0\0\0";
        str[3] = (char)(v & 0xFF);
        str[2] = (char)((v >> 8) & 0xFF);
        str[1] = (char)((v >> 16) & 0xFF);
        str[0] = (char)((v >> 24) & 0xFF);
        const char* s = str;

        out << "<TD>" << s << "</TD>" << std::endl;
    }

    out << "</TR></TABLE>>];" << std::endl;
    if (title) out << "}" << std::endl;

    out << std::endl;
}


#endif // TIGHTDB_DEBUG
