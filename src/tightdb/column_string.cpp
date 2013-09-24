#include <cstdlib>
#include <cstring>
#include <cstdio> // debug
#include <iostream>
#include <iomanip>

#ifdef _WIN32
#  include <win32\types.h>
#endif

#include <tightdb/query_conditions.hpp>
#include <tightdb/column_string.hpp>
#include <tightdb/index_string.hpp>

using namespace std;
using namespace tightdb;


namespace {

const size_t small_string_max_size  = 15; // ArrayString
const size_t medium_string_max_size = 63; // ArrayStringLong

// Getter function for string index
StringData get_string(void* column, size_t ndx)
{
    return static_cast<AdaptiveStringColumn*>(column)->get(ndx);
}

void copy_leaf(const ArrayString& from, ArrayStringLong& to)
{
    size_t n = from.size();
    for (size_t i = 0; i != n; ++i) {
        StringData str = from.get(i);
        to.add(from.get(i)); // Throws
    }
}

void copy_leaf(const ArrayString& from, ArrayBigBlobs& to)
{
    size_t n = from.size();
    for (size_t i = 0; i != n; ++i) {
        StringData str = from.get(i);
        to.add_string(str); // Throws
    }
}

void copy_leaf(const ArrayStringLong& from, ArrayBigBlobs& to)
{
    size_t n = from.size();
    for (size_t i = 0; i != n; ++i) {
        StringData str = from.get(i);
        to.add_string(str); // Throws
    }
}

} // anonymous namespace



AdaptiveStringColumn::AdaptiveStringColumn(Allocator& alloc): m_index(0)
{
    m_array = new ArrayString(0, 0, alloc);
}


AdaptiveStringColumn::AdaptiveStringColumn(ref_type ref, ArrayParent* parent, size_t ndx_in_parent,
                                           Allocator& alloc): m_index(0)
{
    char* header = alloc.translate(ref);
    MemRef mem(header, ref);

    // Within an AdaptiveStringColumn the leafs can be of different types
    // optimized for the lengths of the strings contained therein.
    // The type is indicated by the combination of the is_node(N), has_refs(R)
    // and context_bit(C):
    //
    //   N R C
    //   1 0 0   InnerNode (not leaf)
    //   0 0 0   ArrayString
    //   0 1 0   ArrayStringLong
    //   0 1 1   ArrayBigBlobs
    Array::Type type = Array::get_type_from_header(header);
    switch (type) {
        case Array::type_Normal: {
            // Small strings root leaf
            m_array = new ArrayString(mem, parent, ndx_in_parent, alloc);
            return;
        }
        case Array::type_HasRefs: {
            bool is_big = Array::get_context_bit_from_header(header);
            if (!is_big) {
                // Medium strings root leaf
                m_array = new ArrayStringLong(mem, parent, ndx_in_parent, alloc);
                return;
            }
            // Big strings root leaf
            m_array = new ArrayBigBlobs(mem, parent, ndx_in_parent, alloc);
            return;
        }
        case Array::type_InnerColumnNode: {
            // Non-leaf root
            m_array = new Array(mem, parent, ndx_in_parent, alloc);
            return;
        }
    }
    TIGHTDB_ASSERT(false);
}


AdaptiveStringColumn::~AdaptiveStringColumn() TIGHTDB_NOEXCEPT
{
    delete m_array;
    delete m_index;
}


void AdaptiveStringColumn::destroy() TIGHTDB_NOEXCEPT
{
    ColumnBase::destroy();
    if (m_index)
        m_index->destroy();
}


StringData AdaptiveStringColumn::get(size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < size());

    if (root_is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            return leaf->get(ndx);
        }
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Medium strings root leaf
            ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
            return leaf->get(ndx);
        }
        // Big strings root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        return leaf->get_string(ndx);
    }

    // Non-leaf root
    pair<MemRef, size_t> p = m_array->get_bptree_leaf(ndx);
    const char* leaf_header = p.first.m_addr;
    size_t ndx_in_leaf = p.second;
    bool long_strings = Array::get_hasrefs_from_header(leaf_header);
    if (!long_strings) {
        // Small strings
        return ArrayString::get(leaf_header, ndx_in_leaf);
    }
    Allocator& alloc = m_array->get_alloc();
    bool is_big = Array::get_context_bit_from_header(leaf_header);
    if (!is_big) {
        // Medimum strings
        return ArrayStringLong::get(leaf_header, ndx_in_leaf, alloc);
    }
    // Big strings
    return ArrayBigBlobs::get_string(leaf_header, ndx_in_leaf, alloc);
}


StringIndex& AdaptiveStringColumn::create_index()
{
    TIGHTDB_ASSERT(!m_index);

    // Create new index
    m_index = new StringIndex(this, &get_string, m_array->get_alloc());

    // Populate the index
    size_t n = size();
    for (size_t i = 0; i != n; ++i) {
        StringData value = get(i);
        bool is_last = true;
        m_index->insert(i, value, is_last);
    }

    return *m_index;
}


void AdaptiveStringColumn::set_index_ref(ref_type ref, ArrayParent* parent, size_t ndx_in_parent)
{
    TIGHTDB_ASSERT(!m_index);
    m_index = new StringIndex(ref, parent, ndx_in_parent, this, &get_string, m_array->get_alloc());
}


void AdaptiveStringColumn::clear()
{
    if (root_is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            leaf->clear(); // Throws
        }
        else {
            bool is_big = m_array->context_bit();
            if (!is_big) {
                // Medium strings root leaf
                ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
                leaf->clear(); // Throws
            }
            else {
                // Big strings root leaf
                ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
                leaf->clear(); // Throws
            }
        }
    }
    else {
        // Non-leaf root - revert to small strings leaf
        ArrayParent* parent = m_array->get_parent();
        size_t ndx_in_parent = m_array->get_ndx_in_parent();
        Allocator& alloc = m_array->get_alloc();
        Array* array = new ArrayString(parent, ndx_in_parent, alloc); // Throws

        // Remove original node
        m_array->destroy();
        delete m_array;

        m_array = array;
    }

    if (m_index)
        m_index->clear(); // Throws
}


void AdaptiveStringColumn::resize(size_t n)
{
    TIGHTDB_ASSERT(root_is_leaf()); // currently only available on leaf level (used by b-tree code)

    bool long_strings = m_array->has_refs();
    if (!long_strings) {
        // Small strings
        ArrayString* leaf = static_cast<ArrayString*>(m_array);
        leaf->resize(n); // Throws
        return;
    }
    bool is_big = m_array->context_bit();
    if (!is_big) {
        // Medium strings
        ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
        leaf->resize(n); // Throws
        return;
    }
    // Big strings
    ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
    leaf->resize(n); // Throws
}


namespace {

class SetLeafElem: public Array::UpdateHandler {
public:
    Allocator& m_alloc;
    const StringData m_value;

    SetLeafElem(Allocator& alloc, StringData value) TIGHTDB_NOEXCEPT:
        m_alloc(alloc), m_value(value) {}

    void update(MemRef mem, ArrayParent* parent, size_t ndx_in_parent,
                size_t elem_ndx_in_leaf) TIGHTDB_OVERRIDE
    {
        bool long_strings = Array::get_hasrefs_from_header(mem.m_addr);
        if (long_strings) {
            bool is_big = Array::get_context_bit_from_header(mem.m_addr);
            if (is_big) {
                ArrayBigBlobs leaf(mem, parent, ndx_in_parent, m_alloc);
                leaf.set_string(elem_ndx_in_leaf, m_value); // Throws
                return;
            }
            ArrayStringLong leaf(mem, parent, ndx_in_parent, m_alloc);
            if (m_value.size() <= medium_string_max_size) {
                leaf.set(elem_ndx_in_leaf, m_value); // Throws
                return;
            }
            // Upgrade leaf from medium to big strings
            ArrayBigBlobs new_leaf(parent, ndx_in_parent, m_alloc); // Throws
            copy_leaf(leaf, new_leaf); // Throws
            leaf.destroy();
            new_leaf.set_string(elem_ndx_in_leaf, m_value); // Throws
            return;
        }
        ArrayString leaf(mem, parent, ndx_in_parent, m_alloc);
        if (m_value.size() <= small_string_max_size) {
            leaf.set(elem_ndx_in_leaf, m_value); // Throws
            return;
        }
        if (m_value.size() <= medium_string_max_size) {
            // Upgrade leaf from small to medium strings
            ArrayStringLong new_leaf(parent, ndx_in_parent, m_alloc); // Throws
            copy_leaf(leaf, new_leaf); // Throws
            leaf.destroy();
            new_leaf.set(elem_ndx_in_leaf, m_value); // Throws
            return;
        }
        // Upgrade leaf from small to big strings
        ArrayBigBlobs new_leaf(parent, ndx_in_parent, m_alloc); // Throws
        copy_leaf(leaf, new_leaf); // Throws
        leaf.destroy();
        new_leaf.set_string(elem_ndx_in_leaf, m_value); // Throws
    }
};

} // anonymous namespace

void AdaptiveStringColumn::set(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx < size());

    // Update index
    // (it is important here that we do it before actually setting
    //  the value, or the index would not be able to find the correct
    //  position to update (as it looks for the old value))
    if (m_index) {
        StringData old_val = get(ndx);
        m_index->set(ndx, old_val, value); // Throws
    }

    if (m_array->is_leaf()) {
        LeafType leaf_type = upgrade_root_leaf(value.size()); // Throws
        switch (leaf_type) {
            case leaf_type_Small: {
                ArrayString* leaf = static_cast<ArrayString*>(m_array);
                leaf->set(ndx, value); // Throws
                return;
            }
            case leaf_type_Medium: {
                ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
                leaf->set(ndx, value); // Throws
                return;
            }
            case leaf_type_Big: {
                ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
                leaf->set_string(ndx, value); // Throws
                return;
            }
        }
        TIGHTDB_ASSERT(false);
    }

    SetLeafElem set_leaf_elem(m_array->get_alloc(), value);
    m_array->update_bptree_elem(ndx, set_leaf_elem); // Throws
}


void AdaptiveStringColumn::fill(size_t n)
{
    TIGHTDB_ASSERT(is_empty());
    TIGHTDB_ASSERT(!m_index);

    // Fill column with default values
    // TODO: this is a very naive approach
    // we could speedup by creating full nodes directly
    for (size_t i = 0; i != n; ++i)
        add(StringData()); // Throws

#ifdef TIGHTDB_DEBUG
    Verify();
#endif
}


class AdaptiveStringColumn::EraseLeafElem: public ColumnBase::EraseHandlerBase {
public:
    EraseLeafElem(AdaptiveStringColumn& column) TIGHTDB_NOEXCEPT:
        EraseHandlerBase(column) {}
    bool erase_leaf_elem(MemRef leaf_mem, ArrayParent* parent,
                         size_t leaf_ndx_in_parent,
                         size_t elem_ndx_in_leaf) TIGHTDB_OVERRIDE
    {
        bool long_strings = Array::get_hasrefs_from_header(leaf_mem.m_addr);
        if (!long_strings) {
            // Small strings
            ArrayString leaf(leaf_mem, parent, leaf_ndx_in_parent, get_alloc());
            TIGHTDB_ASSERT(leaf.size() >= 1);
            size_t last_ndx = leaf.size() - 1;
            if (last_ndx == 0)
                return true;
            size_t ndx = elem_ndx_in_leaf;
            if (ndx == npos)
                ndx = last_ndx;
            leaf.erase(ndx); // Throws
            return false;
        }
        bool is_big = Array::get_context_bit_from_header(leaf_mem.m_addr);
        if (!is_big) {
            // Medium strings
            ArrayStringLong leaf(leaf_mem, parent, leaf_ndx_in_parent, get_alloc());
            TIGHTDB_ASSERT(leaf.size() >= 1);
            size_t last_ndx = leaf.size() - 1;
            if (last_ndx == 0)
                return true;
            size_t ndx = elem_ndx_in_leaf;
            if (ndx == npos)
                ndx = last_ndx;
            leaf.erase(ndx); // Throws
            return false;
        }
        // Big strings
        ArrayBigBlobs leaf(leaf_mem, parent, leaf_ndx_in_parent, get_alloc());
        TIGHTDB_ASSERT(leaf.size() >= 1);
        size_t last_ndx = leaf.size() - 1;
        if (last_ndx == 0)
            return true;
        size_t ndx = elem_ndx_in_leaf;
        if (ndx == npos)
            ndx = last_ndx;
        leaf.erase(ndx); // Throws
        return false;
    }
    void destroy_leaf(MemRef leaf_mem) TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE
    {
        ArrayParent* parent = 0;
        size_t ndx_in_parent = 0;
        Array leaf(leaf_mem, parent, ndx_in_parent, get_alloc());
        leaf.destroy();
    }
    void replace_root_by_leaf(MemRef leaf_mem) TIGHTDB_OVERRIDE
    {
        UniquePtr<Array> leaf;
        ArrayParent* parent = 0;
        size_t ndx_in_parent = 0;
        bool long_strings = Array::get_hasrefs_from_header(leaf_mem.m_addr);
        if (!long_strings) {
            // Small strings
            leaf.reset(new ArrayString(leaf_mem, parent, ndx_in_parent, get_alloc())); // Throws
        }
        else {
            bool is_big = Array::get_context_bit_from_header(leaf_mem.m_addr);
            if (!is_big) {
                // Medium strings
                leaf.reset(new ArrayStringLong(leaf_mem, parent, ndx_in_parent,
                                               get_alloc())); // Throws
            }
            else {
                // Big strings
                leaf.reset(new ArrayBigBlobs(leaf_mem, parent, ndx_in_parent,
                                             get_alloc())); // Throws
            }
        }
        replace_root(leaf); // Throws
    }
    void replace_root_by_empty_leaf() TIGHTDB_OVERRIDE
    {
        UniquePtr<Array> leaf;
        ArrayParent* parent = 0;
        size_t ndx_in_parent = 0;
        leaf.reset(new ArrayString(parent, ndx_in_parent, get_alloc())); // Throws
        replace_root(leaf); // Throws
    }
};

void AdaptiveStringColumn::erase(size_t ndx, bool is_last)
{
    TIGHTDB_ASSERT(ndx < size());
    TIGHTDB_ASSERT(is_last == (ndx == size()-1));

    // Update index
    // (it is important here that we do it before actually setting
    //  the value, or the index would not be able to find the correct
    //  position to update (as it looks for the old value))
    if (m_index) {
        StringData old_val = get(ndx);
        // FIXME: This always evaluates to false. Alexander, what was
        // the intention? See also ColumnStringEnum::erase().
        bool is_last_2 = ndx == size();
        m_index->erase(ndx, old_val, is_last_2);
    }

    if (m_array->is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            leaf->erase(ndx); // Throws
            return;
        }
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Medium strings root leaf
            ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
            leaf->erase(ndx); // Throws
            return;
        }
        // Big strings root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        leaf->erase(ndx); // Throws
        return;
    }

    // Non-leaf root
    size_t ndx_2 = is_last ? npos : ndx;
    EraseLeafElem erase_leaf_elem(*this);
    Array::erase_bptree_elem(m_array, ndx_2, erase_leaf_elem); // Throws
}


void AdaptiveStringColumn::move_last_over(size_t ndx)
{
    // FIXME: ExceptionSafety: The current implementation of this
    // function is not exception-safe, and it is hard to see how to
    // repair it.

    // FIXME: Consider doing two nested calls to
    // update_bptree_elem(). If the two leafs are not the same, no
    // copying is needed. If they are the same, call
    // Array::move_last_over() (does not yet
    // exist). Array::move_last_over() could be implemented in a way
    // that avoids the intermediate copy. This approach is also likely
    // to be necesseray for exception safety.

    TIGHTDB_ASSERT(ndx+1 < size());

    size_t last_ndx = size() - 1;
    StringData value = get(last_ndx);

    // Copying string data from a column to itself requires an
    // intermediate copy of the data (constr:bptree-copy-to-self).
    UniquePtr<char[]> buffer(new char[value.size()]);
    copy(value.data(), value.data()+value.size(), buffer.get());
    StringData copy_of_value(buffer.get(), value.size());

    if (m_index) {
        // remove the value to be overwritten from index
        StringData old_target_val = get(ndx);
        m_index->erase(ndx, old_target_val, true);

        // update index to point to new location
        m_index->update_ref(copy_of_value, last_ndx, ndx);
    }

    if (m_array->is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            leaf->set(ndx, copy_of_value); // Throws
            leaf->erase(last_ndx); // Throws
            return;
        }
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Medium strings root leaf
            ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
            leaf->set(ndx, copy_of_value); // Throws
            leaf->erase(last_ndx); // Throws
            return;
        }
        // Big strings root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        leaf->set_string(ndx, copy_of_value); // Throws
        leaf->erase(last_ndx); // Throws
        return;
    }

    // Non-leaf root
    SetLeafElem set_leaf_elem(m_array->get_alloc(), copy_of_value);
    m_array->update_bptree_elem(ndx, set_leaf_elem); // Throws
    EraseLeafElem erase_leaf_elem(*this);
    Array::erase_bptree_elem(m_array, npos, erase_leaf_elem); // Throws
}


size_t AdaptiveStringColumn::count(StringData value) const
{
    if (m_index)
        return m_index->count(value); // Throws

    if (root_is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            return leaf->count(value);
        }
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Medium strings root leaf
            ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
            return leaf->count(value);
        }
        // Big strings root leaf
        BinaryData bin(value.data(), value.size());
        bool is_string = true;
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        return leaf->count(bin, is_string);
    }

    // Non-leaf root
    size_t num_matches = 0;

    // FIXME: It would be better to always require that 'end' is
    // specified explicitely, since Table has the size readily
    // available, and Array::get_bptree_size() is deprecated.
    size_t begin = 0, end = m_array->get_bptree_size();
    while (begin < end) {
        pair<MemRef, size_t> p = m_array->get_bptree_leaf(begin);
        MemRef leaf_mem = p.first;
        TIGHTDB_ASSERT(p.second == 0);
        bool long_strings = Array::get_hasrefs_from_header(leaf_mem.m_addr);
        if (!long_strings) {
            // Small strings
            ArrayString leaf(leaf_mem, 0, 0, m_array->get_alloc());
            num_matches += leaf.count(value);
            begin += leaf.size();
            continue;
        }
        bool is_big = Array::get_context_bit_from_header(leaf_mem.m_addr);
        if (!is_big) {
            // Medium strings
            ArrayStringLong leaf(leaf_mem, 0, 0, m_array->get_alloc());
            num_matches += leaf.count(value);
            begin += leaf.size();
            continue;
        }
        // Big strings
        ArrayBigBlobs leaf(leaf_mem, 0, 0, m_array->get_alloc());
        BinaryData bin(value.data(), value.size());
        bool is_string = true;
        num_matches += leaf.count(bin, is_string);
        begin += leaf.size();
    }

    return num_matches;
}


size_t AdaptiveStringColumn::find_first(StringData value, size_t begin, size_t end) const
{
    TIGHTDB_ASSERT(begin <= size());
    TIGHTDB_ASSERT(end == npos || (begin <= end && end <= size()));

    if (m_index && begin == 0 && end == npos)
        return m_index->find_first(value); // Throws

    if (root_is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            return leaf->find_first(value, begin, end);
        }
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Medium strings root leaf
            ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
            return leaf->find_first(value, begin, end);
        }
        // Big strings root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        BinaryData bin(value.data(), value.size());
        bool is_string = true;
        return leaf->find_first(bin, is_string, begin, end);
    }

    // Non-leaf root
    //
    // FIXME: It would be better to always require that 'end' is
    // specified explicitely, since Table has the size readily
    // available, and Array::get_bptree_size() is deprecated.
    if (end == npos)
        end = m_array->get_bptree_size();

    size_t ndx_in_tree = begin;
    while (ndx_in_tree < end) {
        pair<MemRef, size_t> p = m_array->get_bptree_leaf(ndx_in_tree);
        MemRef leaf_mem = p.first;
        size_t ndx_in_leaf = p.second, end_in_leaf;
        size_t leaf_offset = ndx_in_tree - ndx_in_leaf;
        bool long_strings = Array::get_hasrefs_from_header(leaf_mem.m_addr);
        if (!long_strings) {
            // Small strings
            ArrayString leaf(leaf_mem, 0, 0, m_array->get_alloc());
            end_in_leaf = min(leaf.size(), end - leaf_offset);
            size_t ndx = leaf.find_first(value, ndx_in_leaf, end_in_leaf);
            if (ndx != not_found)
                return leaf_offset + ndx;
        }
        else {
            bool is_big = Array::get_context_bit_from_header(leaf_mem.m_addr);
            if (!is_big) {
                // Medium strings
                ArrayStringLong leaf(leaf_mem, 0, 0, m_array->get_alloc());
                end_in_leaf = min(leaf.size(), end - leaf_offset);
                size_t ndx = leaf.find_first(value, ndx_in_leaf, end_in_leaf);
                if (ndx != not_found)
                    return leaf_offset + ndx;
            }
            else {
                // Big strings
                ArrayBigBlobs leaf(leaf_mem, 0, 0, m_array->get_alloc());
                end_in_leaf = min(leaf.size(), end - leaf_offset);
                BinaryData bin(value.data(), value.size());
                bool is_string = true;
                size_t ndx = leaf.find_first(bin, is_string, ndx_in_leaf, end_in_leaf);
                if (ndx != not_found)
                    return leaf_offset + ndx;
            }
        }
        ndx_in_tree = leaf_offset + end_in_leaf;
    }

    return not_found;
}


void AdaptiveStringColumn::find_all(Array& result, StringData value, size_t begin, size_t end) const
{
    TIGHTDB_ASSERT(begin <= size());
    TIGHTDB_ASSERT(end == npos || (begin <= end && end <= size()));

    if (m_index && begin == 0 && end == npos)
        m_index->find_all(result, value); // Throws

    if (root_is_leaf()) {
        size_t leaf_offset = 0;
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            leaf->find_all(result, value, leaf_offset, begin, end); // Throws
            return;
        }
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Medium strings root leaf
            ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
            leaf->find_all(result, value, leaf_offset, begin, end); // Throws
            return;
        }
        // Big strings root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        BinaryData bin(value.data(), value.size());
        bool is_string = true;
        leaf->find_all(result, bin, is_string, leaf_offset, begin, end); // Throws
        return;
    }

    // Non-leaf root
    //
    // FIXME: It would be better to always require that 'end' is
    // specified explicitely, since Table has the size readily
    // available, and Array::get_bptree_size() is deprecated.
    if (end == npos)
        end = m_array->get_bptree_size();

    size_t ndx_in_tree = begin;
    while (ndx_in_tree < end) {
        pair<MemRef, size_t> p = m_array->get_bptree_leaf(ndx_in_tree);
        MemRef leaf_mem = p.first;
        size_t ndx_in_leaf = p.second, end_in_leaf;
        size_t leaf_offset = ndx_in_tree - ndx_in_leaf;
        bool long_strings = Array::get_hasrefs_from_header(leaf_mem.m_addr);
        if (!long_strings) {
            // Small strings
            ArrayString leaf(leaf_mem, 0, 0, m_array->get_alloc());
            end_in_leaf = min(leaf.size(), end - leaf_offset);
            leaf.find_all(result, value, leaf_offset, ndx_in_leaf, end_in_leaf); // Throws
        }
        else {
            bool is_big = Array::get_context_bit_from_header(leaf_mem.m_addr);
            if (!is_big) {
                // Medium strings
                ArrayStringLong leaf(leaf_mem, 0, 0, m_array->get_alloc());
                end_in_leaf = min(leaf.size(), end - leaf_offset);
                leaf.find_all(result, value, leaf_offset, ndx_in_leaf, end_in_leaf); // Throws
            }
            else {
                // Big strings
                ArrayBigBlobs leaf(leaf_mem, 0, 0, m_array->get_alloc());
                end_in_leaf = min(leaf.size(), end - leaf_offset);
                BinaryData bin(value.data(), value.size());
                bool is_string = true;
                leaf.find_all(result, bin, is_string, leaf_offset, ndx_in_leaf,
                              end_in_leaf); // Throws
            }
        }
        ndx_in_tree = leaf_offset + end_in_leaf;
    }
}


namespace {

struct BinToStrAdaptor {
    typedef StringData value_type;
    const ArrayBigBlobs& m_big_blobs;
    BinToStrAdaptor(const ArrayBigBlobs& big_blobs) TIGHTDB_NOEXCEPT: m_big_blobs(big_blobs) {}
    ~BinToStrAdaptor() TIGHTDB_NOEXCEPT {}
    size_t size() const TIGHTDB_NOEXCEPT
    {
        return m_big_blobs.size();
    }
    StringData get(size_t ndx) const TIGHTDB_NOEXCEPT
    {
        return m_big_blobs.get_string(ndx);
    }
};

} // anonymous namespace

size_t AdaptiveStringColumn::lower_bound_string(StringData value) const TIGHTDB_NOEXCEPT
{
    if (root_is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            return ColumnBase::lower_bound(*leaf, value);
        }
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Medium strings root leaf
            ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
            return ColumnBase::lower_bound(*leaf, value);
        }
        // Big strings root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        BinToStrAdaptor adapt(*leaf);
        return ColumnBase::lower_bound(adapt, value);
    }
    // Non-leaf root
    return ColumnBase::lower_bound(*this, value);
}

size_t AdaptiveStringColumn::upper_bound_string(StringData value) const TIGHTDB_NOEXCEPT
{
    if (root_is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            return ColumnBase::upper_bound(*leaf, value);
        }
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Medium strings root leaf
            ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
            return ColumnBase::upper_bound(*leaf, value);
        }
        // Big strings root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        BinToStrAdaptor adapt(*leaf);
        return ColumnBase::upper_bound(adapt, value);
    }
    // Non-leaf root
    return ColumnBase::upper_bound(*this, value);
}


FindRes AdaptiveStringColumn::find_all_indexref(StringData value, size_t& dst) const
{
    TIGHTDB_ASSERT(value.data());
    TIGHTDB_ASSERT(m_index);

    return m_index->find_all(value, dst);
}


bool AdaptiveStringColumn::auto_enumerate(ref_type& keys_ref, ref_type& values_ref) const
{
    AdaptiveStringColumn keys(m_array->get_alloc());

    // Generate list of unique values (keys)
    size_t n = size();
    for (size_t i = 0; i != n; ++i) {
        StringData v = get(i);

        // Insert keys in sorted order, ignoring duplicates
        size_t pos = keys.lower_bound_string(v);
        if (pos != keys.size() && keys.get(pos) == v)
            continue;

        // Don't bother auto enumerating if there are too few duplicates
        if (n/2 < keys.size()) {
            keys.destroy(); // cleanup
            return false;
        }

        keys.insert(pos, v);
    }

    // Generate enumerated list of entries
    Column values(m_array->get_alloc());
    for (size_t i = 0; i != n; ++i) {
        StringData v = get(i);
        size_t pos = keys.lower_bound_string(v);
        TIGHTDB_ASSERT(pos != keys.size());
        values.add(pos);
    }

    keys_ref   = keys.get_ref();
    values_ref = values.get_ref();
    return true;
}


bool AdaptiveStringColumn::compare_string(const AdaptiveStringColumn& c) const
{
    size_t n = size();
    if (c.size() != n)
        return false;
    for (size_t i = 0; i != n; ++i) {
        if (get(i) != c.get(i))
            return false;
    }
    return true;
}


void AdaptiveStringColumn::do_insert(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx == npos || ndx < size());
    ref_type new_sibling_ref;
    Array::TreeInsert<AdaptiveStringColumn> state;
    if (root_is_leaf()) {
        TIGHTDB_ASSERT(ndx == npos || ndx < TIGHTDB_MAX_LIST_SIZE);
        LeafType leaf_type = upgrade_root_leaf(value.size()); // Throws
        switch (leaf_type) {
            case leaf_type_Small: {
                // Small strings root leaf
                ArrayString* leaf = static_cast<ArrayString*>(m_array);
                new_sibling_ref = leaf->bptree_leaf_insert(ndx, value, state); // Throws
                goto insert_done;
            }
            case leaf_type_Medium: {
                // Medium strings root leaf
                ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
                new_sibling_ref = leaf->bptree_leaf_insert(ndx, value, state); // Throws
                goto insert_done;
            }
            case leaf_type_Big: {
                // Big strings root leaf
                ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
                new_sibling_ref = leaf->bptree_leaf_insert_string(ndx, value, state); // Throws
                goto insert_done;
            }
        }
        TIGHTDB_ASSERT(false);
    }

    // Non-leaf root
    state.m_value = value;
    if (ndx == npos) {
        new_sibling_ref = m_array->bptree_append(state); // Throws
    }
    else {
        new_sibling_ref = m_array->bptree_insert(ndx, state); // Throws
    }

  insert_done:
    if (TIGHTDB_UNLIKELY(new_sibling_ref)) {
        bool is_append = ndx == npos;
        introduce_new_root(new_sibling_ref, state, is_append); // Throws
    }

    // Update index
    if (m_index) {
        bool is_last = ndx == npos;
        size_t real_ndx = is_last ? size()-1 : ndx;
        m_index->insert(real_ndx, value, is_last); // Throws
    }

#ifdef TIGHTDB_DEBUG
    Verify();
#endif
}


ref_type AdaptiveStringColumn::leaf_insert(MemRef leaf_mem, ArrayParent& parent,
                                           size_t ndx_in_parent, Allocator& alloc,
                                           size_t insert_ndx,
                                           Array::TreeInsert<AdaptiveStringColumn>& state)
{
    bool long_strings = Array::get_hasrefs_from_header(leaf_mem.m_addr);
    if (long_strings) {
        bool is_big = Array::get_context_bit_from_header(leaf_mem.m_addr);
        if (is_big) {
            ArrayBigBlobs leaf(leaf_mem, &parent, ndx_in_parent, alloc);
            return leaf.bptree_leaf_insert_string(insert_ndx, state.m_value, state); // Throws
        }
        ArrayStringLong leaf(leaf_mem, &parent, ndx_in_parent, alloc);
        if (state.m_value.size() <= medium_string_max_size)
            return leaf.bptree_leaf_insert(insert_ndx, state.m_value, state); // Throws
        // Upgrade leaf from medium to big strings
        ArrayBigBlobs new_leaf(&parent, ndx_in_parent, alloc); // Throws
        copy_leaf(leaf, new_leaf); // Throws
        leaf.destroy();
        return new_leaf.bptree_leaf_insert_string(insert_ndx, state.m_value, state); // Throws
    }
    ArrayString leaf(leaf_mem, &parent, ndx_in_parent, alloc);
    if (state.m_value.size() <= small_string_max_size)
        return leaf.bptree_leaf_insert(insert_ndx, state.m_value, state); // Throws
    if (state.m_value.size() <= medium_string_max_size) {
        // Upgrade leaf from small to medium strings
        ArrayStringLong new_leaf(&parent, ndx_in_parent, alloc); // Throws
        copy_leaf(leaf, new_leaf); // Throws
        leaf.destroy();
        return new_leaf.bptree_leaf_insert(insert_ndx, state.m_value, state); // Throws
    }
    // Upgrade leaf from small to big strings
    ArrayBigBlobs new_leaf(&parent, ndx_in_parent, alloc); // Throws
    copy_leaf(leaf, new_leaf); // Throws
    leaf.destroy();
    return new_leaf.bptree_leaf_insert_string(insert_ndx, state.m_value, state); // Throws
}


AdaptiveStringColumn::LeafType AdaptiveStringColumn::upgrade_root_leaf(size_t value_size)
{
    TIGHTDB_ASSERT(root_is_leaf());

    bool long_strings = m_array->has_refs();
    if (long_strings) {
        bool is_big = m_array->context_bit();
        if (is_big)
            return leaf_type_Big;
        if (value_size <= medium_string_max_size)
            return leaf_type_Medium;
        // Upgrade root leaf from medium to big strings
        ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
        UniquePtr<ArrayBigBlobs> new_leaf;
        ArrayParent* parent = leaf->get_parent();
        size_t ndx_in_parent = leaf->get_ndx_in_parent();
        Allocator& alloc = leaf->get_alloc();
        new_leaf.reset(new ArrayBigBlobs(parent, ndx_in_parent, alloc)); // Throws
        copy_leaf(*leaf, *new_leaf); // Throws
        leaf->destroy();
        delete leaf;
        m_array = new_leaf.release();
        return leaf_type_Big;
    }
    if (value_size <= small_string_max_size)
        return leaf_type_Small;
    ArrayString* leaf = static_cast<ArrayString*>(m_array);
    ArrayParent* parent = leaf->get_parent();
    size_t ndx_in_parent = leaf->get_ndx_in_parent();
    Allocator& alloc = leaf->get_alloc();
    if (value_size <= medium_string_max_size) {
        // Upgrade root leaf from small to medium strings
        UniquePtr<ArrayStringLong> new_leaf;
        new_leaf.reset(new ArrayStringLong(parent, ndx_in_parent, alloc)); // Throws
        copy_leaf(*leaf, *new_leaf); // Throws
        leaf->destroy();
        delete leaf;
        m_array = new_leaf.release();
        return leaf_type_Medium;
    }
    // Upgrade root leaf from small to big strings
    UniquePtr<ArrayBigBlobs> new_leaf;
    new_leaf.reset(new ArrayBigBlobs(parent, ndx_in_parent, alloc)); // Throws
    copy_leaf(*leaf, *new_leaf); // Throws
    leaf->destroy();
    delete leaf;
    m_array = new_leaf.release();
    return leaf_type_Big;
}


AdaptiveStringColumn::LeafType
AdaptiveStringColumn::GetBlock(size_t ndx, ArrayParent** ap, size_t& off) const
{
    Allocator& alloc = m_array->get_alloc();
    if (root_is_leaf()) {
        off = 0;
        bool long_strings = m_array->has_refs();
        if (long_strings) {
            if (m_array->context_bit()) {
                ArrayBigBlobs* asb2 = new ArrayBigBlobs(m_array->get_ref(), 0, 0, alloc);
                *ap = asb2;
                return leaf_type_Big;
            }
            ArrayStringLong* asl2 = new ArrayStringLong(m_array->get_ref(), 0, 0, alloc);
            *ap = asl2;
            return leaf_type_Medium;
        }
        ArrayString* as2 = new ArrayString(m_array->get_ref(), 0, 0, alloc);
        *ap = as2;
        return leaf_type_Small;
    }

    pair<MemRef, size_t> p = m_array->get_bptree_leaf(ndx);
    off = ndx - p.second;
    bool long_strings = Array::get_hasrefs_from_header(p.first.m_addr);
    if (long_strings) {
        if (Array::get_context_bit_from_header(p.first.m_addr)) {
            ArrayBigBlobs* asb2 = new ArrayBigBlobs(p.first, 0, 0, alloc);
            *ap = asb2;
            return leaf_type_Big;
        }
        ArrayStringLong* asl2 = new ArrayStringLong(p.first, 0, 0, alloc);
        *ap = asl2;
        return leaf_type_Medium;
    }
    ArrayString* as2 = new ArrayString(p.first, 0, 0, alloc);
    *ap = as2;
    return leaf_type_Small;
}


#ifdef TIGHTDB_DEBUG

namespace {

size_t verify_leaf(MemRef mem, Allocator& alloc)
{
    bool long_strings = Array::get_hasrefs_from_header(mem.m_addr);
    if (!long_strings) {
        // Small strings
        ArrayString leaf(mem, 0, 0, alloc);
        leaf.Verify();
        return leaf.size();
    }
    bool is_big = Array::get_context_bit_from_header(mem.m_addr);
    if (!is_big) {
        // Medium strings
        ArrayStringLong leaf(mem, 0, 0, alloc);
        leaf.Verify();
        return leaf.size();
    }
    // Big strings
    ArrayBigBlobs leaf(mem, 0, 0, alloc);
    leaf.Verify();
    return leaf.size();
}

} // anonymous namespace

void AdaptiveStringColumn::Verify() const
{
    if (root_is_leaf()) {
        bool long_strings = m_array->has_refs();
        if (!long_strings) {
            // Small strings root leaf
            ArrayString* leaf = static_cast<ArrayString*>(m_array);
            leaf->Verify();
        }
        else {
            bool is_big = m_array->context_bit();
            if (!is_big) {
                // Medium strings root leaf
                ArrayStringLong* leaf = static_cast<ArrayStringLong*>(m_array);
                leaf->Verify();
            }
            else {
                // Big strings root leaf
                ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
                leaf->Verify();
            }
        }
    }
    else {
        // Non-leaf root
        m_array->verify_bptree(&verify_leaf);
    }

    if (m_index)
        m_index->verify_entries(*this);
}


void AdaptiveStringColumn::to_dot(ostream& out, StringData title) const
{
    ref_type ref = m_array->get_ref();
    out << "subgraph cluster_string_column" << ref << " {" << endl;
    out << " label = \"String column";
    if (title.size() != 0)
        out << "\\n'" << title << "'";
    out << "\";" << endl;
    tree_to_dot(out);
    out << "}" << endl;
}

void AdaptiveStringColumn::leaf_to_dot(MemRef leaf_mem, ArrayParent* parent, size_t ndx_in_parent,
                                       ostream& out) const
{
    bool long_strings = Array::get_hasrefs_from_header(leaf_mem.m_addr);
    if (!long_strings) {
        // Small strings
        ArrayString leaf(leaf_mem, parent, ndx_in_parent, m_array->get_alloc());
        leaf.to_dot(out);
        return;
    }
    bool is_big = Array::get_context_bit_from_header(leaf_mem.m_addr);
    if (!is_big) {
        // Medium strings
        ArrayStringLong leaf(leaf_mem, parent, ndx_in_parent, m_array->get_alloc());
        leaf.to_dot(out);
        return;
    }
    // Big strings
    ArrayBigBlobs leaf(leaf_mem, parent, ndx_in_parent, m_array->get_alloc());
    bool is_strings = true;
    leaf.to_dot(out, is_strings);
}


namespace {

void leaf_dumper(MemRef mem, Allocator& alloc, ostream& out, int level)
{
    size_t leaf_size;
    const char* leaf_type;
    bool long_strings = Array::get_hasrefs_from_header(mem.m_addr);
    if (!long_strings) {
        // Small strings
        ArrayString leaf(mem, 0, 0, alloc);
        leaf_size = leaf.size();
        leaf_type = "Small strings leaf";
    }
    else {
        bool is_big = Array::get_context_bit_from_header(mem.m_addr);
        if (!is_big) {
            // Medium strings
            ArrayStringLong leaf(mem, 0, 0, alloc);
            leaf_size = leaf.size();
            leaf_type = "Medimum strings leaf";
        }
        else {
            // Big strings
            ArrayBigBlobs leaf(mem, 0, 0, alloc);
            leaf_size = leaf.size();
            leaf_type = "Big strings leaf";
        }
    }
    int indent = level * 2;
    out << setw(indent) << "" << leaf_type << " (size: "<<leaf_size<<")\n";
}

} // anonymous namespace

void AdaptiveStringColumn::dump_node_structure(ostream& out, int level) const
{
    m_array->dump_bptree_structure(out, level, &leaf_dumper);
}

#endif // TIGHTDB_DEBUG
