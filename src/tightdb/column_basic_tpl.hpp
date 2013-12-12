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
#ifndef TIGHTDB_COLUMN_BASIC_TPL_HPP
#define TIGHTDB_COLUMN_BASIC_TPL_HPP

// Todo: It's bad design (headers are entangled) that a Column uses query_engine.hpp which again uses Column.
// It's the aggregate() method that calls query_engine, and a quick fix (still not optimal) could be to create
// the call and include inside float and double's .cpp files.
#include <tightdb/query_engine.hpp>

namespace tightdb {

// Predeclarations from query_engine.hpp
class ParentNode;
template<class T, class F> class FloatDoubleNode;
template<class T> class SequentialGetter;


template<class T>
BasicColumn<T>::BasicColumn(Allocator& alloc)
{
    m_array = new BasicArray<T>(0, 0, alloc);
}

template<class T>
BasicColumn<T>::BasicColumn(ref_type ref, ArrayParent* parent, std::size_t pndx, Allocator& alloc)
{
    bool root_is_leaf = root_is_leaf_from_ref(ref, alloc);
    if (root_is_leaf) {
        m_array = new BasicArray<T>(ref, parent, pndx, alloc);
    }
    else {
        m_array = new Array(ref, parent, pndx, alloc);
    }
}

template<class T>
BasicColumn<T>::~BasicColumn() TIGHTDB_NOEXCEPT
{
    if (root_is_leaf()) {
        delete static_cast<BasicArray<T>*>(m_array);
    }
    else {
        delete m_array;
    }
}

template<class T>
inline std::size_t BasicColumn<T>::size() const TIGHTDB_NOEXCEPT
{
    if (root_is_leaf())
        return m_array->size();
    return m_array->get_bptree_size();
}

template<class T>
void BasicColumn<T>::clear()
{
    if (m_array->is_leaf()) {
        static_cast<BasicArray<T>*>(m_array)->clear(); // Throws
        return;
    }

    ArrayParent* parent = m_array->get_parent();
    std::size_t pndx = m_array->get_ndx_in_parent();

    // FIXME: ExceptionSafety: Array accessor as well as underlying
    // array node is leaked if ArrayParent::update_child_ref() throws.

    // Revert to generic array
    BasicArray<T>* array = new BasicArray<T>(parent, pndx, m_array->get_alloc()); // Throws
    if (parent)
        parent->update_child_ref(pndx, array->get_ref()); // Throws

    // Remove original node
    m_array->destroy();
    delete m_array;

    m_array = array;
}

template<class T>
void BasicColumn<T>::resize(std::size_t ndx)
{
    TIGHTDB_ASSERT(root_is_leaf()); // currently only available on leaf level (used by b-tree code)
    TIGHTDB_ASSERT(ndx < size());
    static_cast<BasicArray<T>*>(m_array)->resize(ndx);
}

template<class T>
void BasicColumn<T>::move_last_over(std::size_t ndx)
{
    TIGHTDB_ASSERT(ndx+1 < size());

    std::size_t last_ndx = size() - 1;
    T v = get(last_ndx);

    set(ndx, v);

    bool is_last = true;
    erase(last_ndx, is_last);
}


template<class T>
T BasicColumn<T>::get(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < size());
    if (root_is_leaf())
        return static_cast<const BasicArray<T>*>(m_array)->get(ndx);

    std::pair<MemRef, std::size_t> p = m_array->get_bptree_leaf(ndx);
    const char* leaf_header = p.first.m_addr;
    std::size_t ndx_in_leaf = p.second;
    return BasicArray<T>::get(leaf_header, ndx_in_leaf);
}


template<class T>
class BasicColumn<T>::SetLeafElem: public Array::UpdateHandler {
public:
    Allocator& m_alloc;
    const T m_value;
    SetLeafElem(Allocator& alloc, T value) TIGHTDB_NOEXCEPT: m_alloc(alloc), m_value(value) {}
    void update(MemRef mem, ArrayParent* parent, std::size_t ndx_in_parent,
                std::size_t elem_ndx_in_leaf) TIGHTDB_OVERRIDE
    {
        BasicArray<T> leaf(mem, parent, ndx_in_parent, m_alloc);
        leaf.set(elem_ndx_in_leaf, m_value); // Throws
    }
};

template<class T>
void BasicColumn<T>::set(std::size_t ndx, T value)
{
    if (m_array->is_leaf()) {
        static_cast<BasicArray<T>*>(m_array)->set(ndx, value); // Throws
        return;
    }

    SetLeafElem set_leaf_elem(m_array->get_alloc(), value);
    m_array->update_bptree_elem(ndx, set_leaf_elem); // Throws
}

template<class T>
void BasicColumn<T>::add(T value)
{
    do_insert(npos, value);
}

template<class T>
void BasicColumn<T>::insert(std::size_t ndx, T value)
{
    TIGHTDB_ASSERT(ndx <= size());
    if (size() <= ndx) ndx = npos;
    do_insert(ndx, value);
}

template<class T>
void BasicColumn<T>::fill(std::size_t count)
{
    TIGHTDB_ASSERT(is_empty());

    // Fill column with default values
    // TODO: this is a very naive approach
    // we could speedup by creating full nodes directly
    for (std::size_t i = 0; i < count; ++i)
        add(T());
}

template<class T>
bool BasicColumn<T>::compare(const BasicColumn& c) const
{
    std::size_t n = size();
    if (c.size() != n)
        return false;
    for (std::size_t i=0; i<n; ++i) {
        T v1 = get(i);
        T v2 = c.get(i);
        if (v1 == v2)
            return false;
    }
    return true;
}


template<class T>
class BasicColumn<T>::EraseLeafElem: public ColumnBase::EraseHandlerBase {
public:
    EraseLeafElem(BasicColumn<T>& column) TIGHTDB_NOEXCEPT:
        EraseHandlerBase(column) {}
    bool erase_leaf_elem(MemRef leaf_mem, ArrayParent* parent,
                         std::size_t leaf_ndx_in_parent,
                         std::size_t elem_ndx_in_leaf) TIGHTDB_OVERRIDE
    {
        BasicArray<T> leaf(leaf_mem, parent, leaf_ndx_in_parent, get_alloc());
        TIGHTDB_ASSERT(leaf.size() >= 1);
        std::size_t last_ndx = leaf.size() - 1;
        if (last_ndx == 0)
            return true;
        std::size_t ndx = elem_ndx_in_leaf;
        if (ndx == npos)
            ndx = last_ndx;
        leaf.erase(ndx); // Throws
        return false;
    }
    void destroy_leaf(MemRef leaf_mem) TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE
    {
        get_alloc().free_(leaf_mem);
    }
    void replace_root_by_leaf(MemRef leaf_mem) TIGHTDB_OVERRIDE
    {
        ArrayParent* parent = 0;
        std::size_t ndx_in_parent = 0;
        util::UniquePtr<Array> leaf(new BasicArray<T>(leaf_mem, parent, ndx_in_parent,
                                                      get_alloc())); // Throws
        replace_root(leaf); // Throws
    }
    void replace_root_by_empty_leaf() TIGHTDB_OVERRIDE
    {
        ArrayParent* parent = 0;
        std::size_t ndx_in_parent = 0;
        util::UniquePtr<Array> leaf(new BasicArray<T>(parent, ndx_in_parent,
                                                      get_alloc())); // Throws
        replace_root(leaf); // Throws
    }
};

template<class T>
void BasicColumn<T>::erase(std::size_t ndx, bool is_last)
{
    TIGHTDB_ASSERT(ndx < size());
    TIGHTDB_ASSERT(is_last == (ndx == size()-1));

    if (m_array->is_leaf()) {
        static_cast<BasicArray<T>*>(m_array)->erase(ndx); // Throws
        return;
    }

    size_t ndx_2 = is_last ? npos : ndx;
    EraseLeafElem erase_leaf_elem(*this);
    Array::erase_bptree_elem(m_array, ndx_2, erase_leaf_elem); // Throws
}


#ifdef TIGHTDB_DEBUG

template<class T>
std::size_t BasicColumn<T>::verify_leaf(MemRef mem, Allocator& alloc)
{
    BasicArray<T> leaf(mem, 0, 0, alloc);
    leaf.Verify();
    return leaf.size();
}

template<class T>
void BasicColumn<T>::Verify() const
{
    if (root_is_leaf()) {
        static_cast<BasicArray<T>*>(m_array)->Verify();
        return;
    }

    m_array->verify_bptree(&BasicColumn<T>::verify_leaf);
}


template<class T>
void BasicColumn<T>::to_dot(std::ostream& out, StringData title) const
{
    ref_type ref = m_array->get_ref();
    out << "subgraph cluster_basic_column" << ref << " {\n";
    out << " label = \"Basic column";
    if (title.size() != 0)
        out << "\\n'" << title << "'";
    out << "\";\n";
    tree_to_dot(out);
    out << "}\n";
}

template<class T>
void BasicColumn<T>::leaf_to_dot(MemRef leaf_mem, ArrayParent* parent, std::size_t ndx_in_parent,
                                 std::ostream& out) const
{
    BasicArray<T> leaf(leaf_mem.m_ref, parent, ndx_in_parent, m_array->get_alloc());
    leaf.to_dot(out);
}

template<class T>
inline void BasicColumn<T>::leaf_dumper(MemRef mem, Allocator& alloc, std::ostream& out, int level)
{
    BasicArray<T> leaf(mem, 0, 0, alloc);
    int indent = level * 2;
    out << std::setw(indent) << "" << "Basic leaf (size: "<<leaf.size()<<")\n";
}

template<class T>
inline void BasicColumn<T>::dump_node_structure(std::ostream& out, int level) const
{
    m_array->dump_bptree_structure(out, level, &leaf_dumper);
}

#endif // TIGHTDB_DEBUG


template<class T>
std::size_t BasicColumn<T>::find_first(T value, std::size_t begin, std::size_t end) const
{
    TIGHTDB_ASSERT(begin <= size());
    TIGHTDB_ASSERT(end == npos || (begin <= end && end <= size()));

    if (root_is_leaf())
        return static_cast<BasicArray<T>*>(m_array)->
            find_first(value, begin, end); // Throws (maybe)

    // FIXME: It would be better to always require that 'end' is
    // specified explicitely, since Table has the size readily
    // available, and Array::get_bptree_size() is deprecated.
    if (end == npos)
        end = m_array->get_bptree_size();

    std::size_t ndx_in_tree = begin;
    while (ndx_in_tree < end) {
        std::pair<MemRef, std::size_t> p = m_array->get_bptree_leaf(ndx_in_tree);
        BasicArray<T> leaf(p.first, 0, 0, m_array->get_alloc());
        std::size_t ndx_in_leaf = p.second;
        std::size_t leaf_offset = ndx_in_tree - ndx_in_leaf;
        std::size_t end_in_leaf = std::min(leaf.size(), end - leaf_offset);
        std::size_t ndx = leaf.find_first(value, ndx_in_leaf, end_in_leaf); // Throws (maybe)
        if (ndx != not_found)
            return leaf_offset + ndx;
        ndx_in_tree = leaf_offset + end_in_leaf;
    }

    return not_found;
}

template<class T>
void BasicColumn<T>::find_all(Array &result, T value, std::size_t begin, std::size_t end) const
{
    TIGHTDB_ASSERT(begin <= size());
    TIGHTDB_ASSERT(end == npos || (begin <= end && end <= size()));

    if (root_is_leaf()) {
        std::size_t leaf_offset = 0;
        static_cast<BasicArray<T>*>(m_array)->
            find_all(result, value, leaf_offset, begin, end); // Throws
        return;
    }

    // FIXME: It would be better to always require that 'end' is
    // specified explicitely, since Table has the size readily
    // available, and Array::get_bptree_size() is deprecated.
    if (end == npos)
        end = m_array->get_bptree_size();

    std::size_t ndx_in_tree = begin;
    while (ndx_in_tree < end) {
        std::pair<MemRef, std::size_t> p = m_array->get_bptree_leaf(ndx_in_tree);
        BasicArray<T> leaf(p.first, 0, 0, m_array->get_alloc());
        std::size_t ndx_in_leaf = p.second;
        std::size_t leaf_offset = ndx_in_tree - ndx_in_leaf;
        std::size_t end_in_leaf = std::min(leaf.size(), end - leaf_offset);
        leaf.find_all(result, value, leaf_offset, ndx_in_leaf, end_in_leaf); // Throws
        ndx_in_tree = leaf_offset + end_in_leaf;
    }
}

template<class T> std::size_t BasicColumn<T>::count(T target) const
{
    return std::size_t(ColumnBase::aggregate<T, int64_t, act_Count, Equal>(target, 0, size()));
}

template<class T>
typename BasicColumn<T>::SumType BasicColumn<T>::sum(std::size_t begin, std::size_t end,
                                                     std::size_t limit) const
{
    return ColumnBase::aggregate<T, SumType, act_Sum, None>(0, begin, end, limit);
}
template<class T> T BasicColumn<T>::minimum(std::size_t begin, std::size_t end, std::size_t limit) const
{
    return ColumnBase::aggregate<T, T, act_Min, None>(0, begin, end, limit);
}

template<class T> T BasicColumn<T>::maximum(std::size_t begin, std::size_t end, std::size_t limit) const
{
    return ColumnBase::aggregate<T, T, act_Max, None>(0, begin, end, limit);
}

template<class T> double BasicColumn<T>::average(std::size_t begin, std::size_t end, std::size_t limit) const
{
    if (end == npos)
        end = size();

    if(limit != npos && begin + limit < end)
        end = begin + limit;

    std::size_t size = end - begin;
    double sum1 = sum(begin, end);
    double avg = sum1 / ( size == 0 ? 1 : size );
    return avg;
}

template<class T> inline void BasicColumn<T>::do_insert(std::size_t ndx, T value)
{
    TIGHTDB_ASSERT(ndx == npos || ndx < size());
    ref_type new_sibling_ref;
    Array::TreeInsert<BasicColumn<T> > state;
    if (root_is_leaf()) {
        TIGHTDB_ASSERT(ndx == npos || ndx < TIGHTDB_MAX_LIST_SIZE);
        BasicArray<T>* leaf = static_cast<BasicArray<T>*>(m_array);
        new_sibling_ref = leaf->bptree_leaf_insert(ndx, value, state);
    }
    else {
        state.m_value = value;
        if (ndx == npos) {
            new_sibling_ref = m_array->bptree_append(state);
        }
        else {
            new_sibling_ref = m_array->bptree_insert(ndx, state);
        }
    }

    if (TIGHTDB_UNLIKELY(new_sibling_ref)) {
        bool is_append = ndx == npos;
        introduce_new_root(new_sibling_ref, state, is_append);
    }
}

template<class T> TIGHTDB_FORCEINLINE
ref_type BasicColumn<T>::leaf_insert(MemRef leaf_mem, ArrayParent& parent,
                                     std::size_t ndx_in_parent,
                                     Allocator& alloc, std::size_t insert_ndx,
                                     Array::TreeInsert<BasicColumn<T> >& state)
{
    BasicArray<T> leaf(leaf_mem, &parent, ndx_in_parent, alloc);
    return leaf.bptree_leaf_insert(insert_ndx, state.m_value, state);
}


template<class T> inline std::size_t BasicColumn<T>::lower_bound(T value) const TIGHTDB_NOEXCEPT
{
    if (root_is_leaf()) {
        return static_cast<const BasicArray<T>*>(m_array)->lower_bound(value);
    }
    return ColumnBase::lower_bound(*this, value);
}

template<class T> inline std::size_t BasicColumn<T>::upper_bound(T value) const TIGHTDB_NOEXCEPT
{
    if (root_is_leaf()) {
        return static_cast<const BasicArray<T>*>(m_array)->upper_bound(value);
    }
    return ColumnBase::upper_bound(*this, value);
}


} // namespace tightdb

#endif // TIGHTDB_COLUMN_BASIC_TPL_HPP
