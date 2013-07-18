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
#ifndef TIGHTDB_COLUMN_TPL_HPP
#define TIGHTDB_COLUMN_TPL_HPP

#include <cstdlib>

#include <tightdb/config.h>
#include <tightdb/array.hpp>
#include <tightdb/column.hpp>
#include <tightdb/column_fwd.hpp>

namespace tightdb {

template<class T, class cond> class BasicNode;
template<class T, class cond> class IntegerNode;
template<class T> class SequentialGetter;

template<class cond, class T> struct ColumnTypeTraits2;

template<class cond> struct ColumnTypeTraits2<cond, int64_t> {
    typedef Column column_type;
    typedef IntegerNode<int64_t,cond> node_type;
};
template<class cond> struct ColumnTypeTraits2<cond, bool> {
    typedef Column column_type;
    typedef IntegerNode<bool,cond> node_type;
};
template<class cond> struct ColumnTypeTraits2<cond, float> {
    typedef ColumnFloat column_type;
    typedef BasicNode<float,cond> node_type;
};
template<class cond> struct ColumnTypeTraits2<cond, double> {
    typedef ColumnDouble column_type;
    typedef BasicNode<double,cond> node_type;
};


template <typename T, typename R, Action action, class condition>
R ColumnBase::aggregate(T target, size_t start, size_t end, size_t *matchcount) const
{
    typedef typename ColumnTypeTraits2<condition,T>::column_type ColType;
    typedef typename ColumnTypeTraits2<condition,T>::node_type NodeType;

    if (end == size_t(-1))
        end = size();

    NodeType node(target, 0);

    node.QuickInit((ColType*)this, target);
    QueryState<R> state;
    state.init(action, NULL, size_t(-1));

    ColType* column = (ColType*)this;
    SequentialGetter<T> sg( column );
    node.template aggregate_local<action, R, T>(&state, start, end, size_t(-1), &sg, matchcount);

    return state.m_state;
}


template<class T> T GetColumnFromRef(Array& parent, size_t ndx) // Throws
{
    //TIGHTDB_ASSERT(parent.has_refs());
    //TIGHTDB_ASSERT(ndx < parent.size());
    return T(size_t(parent.get(ndx)), &parent, ndx, parent.get_alloc()); // Throws
}

template<typename T, class C> T ColumnBase::TreeGet(size_t ndx) const
{
    if (!root_is_leaf()) {
        // Get subnode table
        const Array offsets = NodeGetOffsets();
        Array refs = NodeGetRefs();

        // Find the subnode containing the item
        const size_t node_ndx = offsets.FindPos(ndx);

        // Calc index in subnode
        const size_t offset = node_ndx ? to_ref(offsets.get(node_ndx-1)) : 0;
        const size_t local_ndx = ndx - offset;

        // Get item
        const C target = GetColumnFromRef<C>(refs, node_ndx); // Throws
        return target.template TreeGet<T,C>(local_ndx);
    }
    else {
        return static_cast<const C*>(this)->LeafGet(ndx);
    }
}

template<typename T, class C> void ColumnBase::TreeSet(size_t ndx, T value)
{
    //const T oldVal = m_index ? get(ndx) : 0; // cache oldval for index

    if (!root_is_leaf()) {
        // Get subnode table
        const Array offsets = NodeGetOffsets();
        Array refs = NodeGetRefs();

        // Find the subnode containing the item
        const size_t node_ndx = offsets.FindPos(ndx);

        // Calc index in subnode
        const size_t offset = node_ndx ? to_ref(offsets.get(node_ndx-1)) : 0;
        const size_t local_ndx = ndx - offset;

        // Set item
        C target = GetColumnFromRef<C>(refs, node_ndx);
        target.set(local_ndx, value);
    }
    else {
        static_cast<C*>(this)->LeafSet(ndx, value);
    }

    // Update index
    //if (m_index) m_index->set(ndx, oldVal, value);
}

template<typename T, class C> void ColumnBase::TreeInsert(size_t ndx, T value)
{
    const NodeChange nc = DoInsert<T,C>(ndx, value);
    switch (nc.type) {
        case NodeChange::none:
            return;
        case NodeChange::insert_before: {
            Column newNode(Array::type_InnerColumnNode, m_array->get_alloc());
            newNode.NodeAdd<C>(nc.ref1);
            newNode.NodeAdd<C>(get_ref());
            static_cast<C*>(this)->update_ref(newNode.get_ref());
            return;
        }
        case NodeChange::insert_after: {
            Column newNode(Array::type_InnerColumnNode, m_array->get_alloc());
            newNode.NodeAdd<C>(get_ref());
            newNode.NodeAdd<C>(nc.ref1);
            static_cast<C*>(this)->update_ref(newNode.get_ref());
            return;
        }
        case NodeChange::split: {
            Column newNode(Array::type_InnerColumnNode, m_array->get_alloc());
            newNode.NodeAdd<C>(nc.ref1);
            newNode.NodeAdd<C>(nc.ref2);
            static_cast<C*>(this)->update_ref(newNode.get_ref());
            return;
        }
    }
    TIGHTDB_ASSERT(false);
}

template<typename T, class C> Column::NodeChange ColumnBase::DoInsert(size_t ndx, T value)
{
    if (!root_is_leaf()) {
        // Get subnode table
        Array offsets = NodeGetOffsets();
        Array refs = NodeGetRefs();

        // Find the subnode containing the item
        size_t node_ndx = offsets.FindPos(ndx);
        if (node_ndx == size_t(-1)) {
            // node can never be empty, so try to fit in last item
            node_ndx = offsets.size()-1;
        }

        // Calc index in subnode
        const size_t offset = node_ndx ? to_ref(offsets.get(node_ndx-1)) : 0;
        const size_t local_ndx = ndx - offset;

        // Get sublist
        C target = GetColumnFromRef<C>(refs, node_ndx);

        // Insert item
        const NodeChange nc = target.template DoInsert<T, C>(local_ndx, value);
        if (nc.type ==  NodeChange::none) {
            offsets.Increment(1, node_ndx);  // update offsets
            return NodeChange::none; // no new nodes
        }

        if (nc.type == NodeChange::insert_after) ++node_ndx;

        // If there is room, just update node directly
        if (offsets.size() < TIGHTDB_MAX_LIST_SIZE) {
            if (nc.type == NodeChange::split) NodeInsertSplit<C>(node_ndx, nc.ref2);
            else NodeInsert<C>(node_ndx, nc.ref1); // ::INSERT_BEFORE/AFTER
            return NodeChange::none;
        }

        // Else create new node
        Column newNode(Array::type_InnerColumnNode, m_array->get_alloc());
        if (nc.type == NodeChange::split) {
            // update offset for left node
            const size_t newsize = target.size();
            const size_t preoffset = node_ndx ? to_ref(offsets.get(node_ndx-1)) : 0;
            offsets.set(node_ndx, preoffset + newsize);

            newNode.NodeAdd<C>(nc.ref2);
            ++node_ndx;
        }
        else newNode.NodeAdd<C>(nc.ref1);

        switch (node_ndx) {
        case 0:             // insert before
            return NodeChange(NodeChange::insert_before, newNode.get_ref());
        case TIGHTDB_MAX_LIST_SIZE: // insert after
            if (nc.type == NodeChange::split)
                return NodeChange(NodeChange::split, get_ref(), newNode.get_ref());
            else return NodeChange(NodeChange::insert_after, newNode.get_ref());
        default:            // split
            // Move items after split to new node
            const size_t len = refs.size();
            for (size_t i = node_ndx; i < len; ++i) {
                const size_t ref = refs.get_as_ref(i);
                newNode.NodeAdd<C>(ref);
            }
            offsets.resize(node_ndx);
            refs.resize(node_ndx);
            return NodeChange(NodeChange::split, get_ref(), newNode.get_ref());
        }
    }
    else {
        // Is there room in the list?
        const size_t count = static_cast<C*>(this)->size();
        if (count < TIGHTDB_MAX_LIST_SIZE) {
            static_cast<C*>(this)->LeafInsert(ndx, value);
            return NodeChange::none;
        }

        // Create new list for item
        C newList(m_array->get_alloc()); // Throws
        if (m_array->has_refs()) newList.SetHasRefs(); // all leafs should have same type

        newList.add(value);

        switch (ndx) {
        case 0:             // insert before
            return NodeChange(NodeChange::insert_before, newList.get_ref());
        case TIGHTDB_MAX_LIST_SIZE: // insert below
            return NodeChange(NodeChange::insert_after, newList.get_ref());
        default:            // split
            // Move items after split to new list
            for (size_t i = ndx; i < count; ++i) {
                newList.add(static_cast<C*>(this)->LeafGet(i));
            }
            static_cast<C*>(this)->resize(ndx);

            return NodeChange(NodeChange::split, get_ref(), newList.get_ref());
        }
    }
}

template<class C> void ColumnBase::NodeInsertSplit(size_t ndx, size_t new_ref)
{
    TIGHTDB_ASSERT(!root_is_leaf());
    TIGHTDB_ASSERT(new_ref);

    Array offsets = NodeGetOffsets();
    Array refs = NodeGetRefs();

    TIGHTDB_ASSERT(ndx < offsets.size());
    TIGHTDB_ASSERT(offsets.size() < TIGHTDB_MAX_LIST_SIZE);

    // Get sublists
    const C orig_col = GetColumnFromRef<C>(refs, ndx);
    const C new_col(new_ref, NULL, 0, m_array->get_alloc());

    // Update original size
    const size_t offset = ndx ? to_ref(offsets.get(ndx-1)) : 0;
    const size_t newSize = orig_col.size();
    const size_t newOffset = offset + newSize;
#ifdef TIGHTDB_DEBUG
    const size_t oldSize = to_ref(offsets.get(ndx)) - offset;
#endif
    offsets.set(ndx, newOffset);

    // Insert new ref
    const size_t refSize = new_col.size();
    offsets.insert(ndx+1, newOffset + refSize);
    refs.insert(ndx+1, new_ref);

#ifdef TIGHTDB_DEBUG
    TIGHTDB_ASSERT((newSize + refSize) - oldSize == 1); // insert should only add one item
#endif

    // Update following offsets
    if (offsets.size() > ndx+2)
        offsets.Increment(1, ndx+2);
}

template<class C> void ColumnBase::NodeInsert(size_t ndx, size_t ref)
{
    TIGHTDB_ASSERT(ref);
    TIGHTDB_ASSERT(!root_is_leaf());

    Array offsets = NodeGetOffsets();
    Array refs = NodeGetRefs();

    TIGHTDB_ASSERT(ndx <= offsets.size());
    TIGHTDB_ASSERT(offsets.size() < TIGHTDB_MAX_LIST_SIZE);

    const C col(ref, NULL, 0, m_array->get_alloc());
    const size_t refSize = col.size();
    const int64_t newOffset = (ndx ? offsets.get(ndx-1) : 0) + refSize;

    offsets.insert(ndx, newOffset);
    if (ndx+1 < offsets.size()) {
        offsets.Increment(refSize, ndx+1);
    }
    refs.insert(ndx, ref);
}

template<class C> void ColumnBase::NodeAdd(size_t ref)
{
    TIGHTDB_ASSERT(ref);
    TIGHTDB_ASSERT(!root_is_leaf());

    Array offsets = NodeGetOffsets();
    Array refs = NodeGetRefs();
    const C col(ref, NULL, 0, m_array->get_alloc());

    TIGHTDB_ASSERT(offsets.size() < TIGHTDB_MAX_LIST_SIZE);

    const int64_t newOffset = (offsets.is_empty() ? 0 : offsets.back()) + col.size();
    offsets.add(newOffset);
    refs.add(ref);
}

template<typename T, class C> void ColumnBase::TreeDelete(size_t ndx)
{
    if (root_is_leaf()) {
        static_cast<C*>(this)->LeafDelete(ndx);
    }
    else {
        // Get subnode table
        Array offsets = NodeGetOffsets();
        Array refs = NodeGetRefs();

        // Find the subnode containing the item
        const size_t node_ndx = offsets.FindPos(ndx);
        TIGHTDB_ASSERT(node_ndx != size_t(-1));

        // Calc index in subnode
        const size_t offset = node_ndx ? to_ref(offsets.get(node_ndx-1)) : 0;
        const size_t local_ndx = ndx - offset;

        // Get sublist
        C target = GetColumnFromRef<C>(refs, node_ndx);
        target.template TreeDelete<T,C>(local_ndx);

        // Remove ref in node
        if (target.is_empty()) {
            offsets.erase(node_ndx);
            refs.erase(node_ndx);
            target.destroy();
        }

        if (offsets.is_empty()) {
            // All items deleted, we can revert to being array
            static_cast<C*>(this)->clear();
        }
        else {
            // Update lower offsets
            if (node_ndx < offsets.size()) offsets.Increment(-1, node_ndx);
        }
    }
}


template<typename T, class C, class F>
size_t ColumnBase::TreeFind(T value, size_t start, size_t end) const
{
    // Use index if possible
    /*if (m_index && start == 0 && end == -1) {
     return FindWithIndex(value);
     }*/
//  F function;
    if (root_is_leaf()) {
        const C* c = static_cast<const C*>(this);
        return c->template LeafFind<F>(value, start, end);
    }
    else {
        // Get subnode table
        const Array offsets = NodeGetOffsets();
        const Array refs = NodeGetRefs();
        const size_t count = refs.size();

        if (start == 0 && end == size_t(-1)) {
            for (size_t i = 0; i < count; ++i) {
                const C col(size_t(refs.get(i)), NULL, 0, m_array->get_alloc());
                const size_t ndx = col.template TreeFind<T, C, F>(value, 0, size_t(-1));
                if (ndx != size_t(-1)) {
                    const size_t offset = i ? to_ref(offsets.get(i-1)) : 0;
                    return offset + ndx;
                }
            }
        }
        else {
            // partial search
            size_t i = offsets.FindPos(start);
            size_t offset = i ? to_ref(offsets.get(i-1)) : 0;
            size_t s = start - offset;
            size_t e = (end == size_t(-1) || int(end) >= offsets.get(i)) ? size_t(-1) : end - offset;

            for (;;) {
                const C col(size_t(refs.get(i)), NULL, 0, m_array->get_alloc());

                const size_t ndx = col.template TreeFind<T, C, F>(value, s, e);
                if (ndx != not_found) {
                    const size_t offset = i ? to_ref(offsets.get(i-1)) : 0;
                    return offset + ndx;
                }

                ++i;
                if (i >= count) break;

                s = 0;
                if (end != size_t(-1)) {
                    if (end >= to_ref(offsets.get(i)))
                        e = size_t(-1);
                    else {
                        offset = to_ref(offsets.get(i-1));
                        if (offset >= end)
                            break;
                        e = end - offset;
                    }
                }
            }
        }

        return size_t(-1); // not found
    }
}

template<typename T, class C> void ColumnBase::TreeFindAll(Array &result, T value, size_t add_offset, size_t start, size_t end) const
{
    if (root_is_leaf()) {
        return static_cast<const C*>(this)->LeafFindAll(result, value, add_offset, start, end);
    }
    else {
        // Get subnode table
        const Array offsets = NodeGetOffsets();
        const Array refs = NodeGetRefs();
        const size_t count = refs.size();
        size_t i = offsets.FindPos(start);
        size_t offset = i ? to_ref(offsets.get(i-1)) : 0;
        size_t s = start - offset;
        size_t e = (end == size_t(-1) || int(end) >= offsets.get(i)) ? size_t(-1) : end - offset;

        for (;;) {
            const size_t ref = refs.get_as_ref(i);
            const C col(ref, NULL, 0, m_array->get_alloc());

            size_t add = i ? to_ref(offsets.get(i-1)) : 0;
            add += add_offset;
            col.template TreeFindAll<T, C>(result, value, add, s, e);
            ++i;
            if (i >= count) break;

            s = 0;
            if (end != size_t(-1)) {
                if (end >= to_ref(offsets.get(i))) e = size_t(-1);
                else {
                    offset = to_ref(offsets.get(i-1));
                    if (offset >= end)
                        return;
                    e = end - offset;
                }
            }
        }
    }
}



template<typename T, class C>
void ColumnBase::TreeVisitLeafs(size_t start, size_t end, size_t caller_offset,
                                bool (*call)(T *arr, size_t start, size_t end,
                                             size_t caller_offset, void *state),
                                void *state) const
{
    if (root_is_leaf()) {
        if (end == size_t(-1))
            end = m_array->size();
        if (m_array->size() > 0)
            call(m_array, start, end, caller_offset, state);
    }
    else {
        const Array offsets = NodeGetOffsets();
        const Array refs = NodeGetRefs();
        const size_t count = refs.size();
        size_t i = offsets.FindPos(start);
        size_t offset = i ? to_ref(offsets.get(i-1)) : 0;
        size_t s = start - offset;
        size_t e = (end == size_t(-1) || int(end) >= offsets.get(i)) ? size_t(-1) : end - offset;

        for (;;) {
            const size_t ref = refs.get_as_ref(i);
            const C col(ref, NULL, 0, m_array->get_alloc());

            size_t add = i ? to_ref(offsets.get(i-1)) : 0;
            add += caller_offset;
            col.template TreeVisitLeafs<T, C>(s, e, add, call, state);
            ++i;
            if (i >= count) break;

            s = 0;
            if (end != size_t(-1)) {
                if (end >= to_ref(offsets.get(i))) e = size_t(-1);
                else {
                    offset = to_ref(offsets.get(i-1));
                    if (offset >= end)
                        return;
                    e = end - offset;
                }
            }
        }
    }
}


} // namespace tightdb

#endif // TIGHTDB_COLUMN_TPL_HPP
