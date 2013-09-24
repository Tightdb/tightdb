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
#ifndef TIGHTDB_COLUMN_BINARY_HPP
#define TIGHTDB_COLUMN_BINARY_HPP

#include <tightdb/column.hpp>
#include <tightdb/array_binary.hpp>
#include <tightdb/array_blobs_big.hpp>

namespace tightdb {


class ColumnBinary: public ColumnBase {
public:
    typedef BinaryData value_type;

    explicit ColumnBinary(Allocator& = Allocator::get_default());
    explicit ColumnBinary(ref_type, ArrayParent* = 0, std::size_t ndx_in_parent = 0,
                          Allocator& = Allocator::get_default());
    ~ColumnBinary() TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;

    std::size_t size() const TIGHTDB_NOEXCEPT;
    bool is_empty() const TIGHTDB_NOEXCEPT { return size() == 0; }

    BinaryData get(std::size_t ndx) const TIGHTDB_NOEXCEPT;

    void add() TIGHTDB_OVERRIDE { add(BinaryData()); }
    void add(BinaryData value);
    void set(std::size_t ndx, BinaryData value, bool add_zero_term = false);
    void insert(std::size_t ndx) TIGHTDB_OVERRIDE { insert(ndx, BinaryData()); }
    void insert(std::size_t ndx, BinaryData value);
    void erase(std::size_t ndx, bool is_last) TIGHTDB_OVERRIDE;
    void resize(std::size_t ndx);
    void clear() TIGHTDB_OVERRIDE;
    void fill(std::size_t count);
    void move_last_over(std::size_t ndx) TIGHTDB_OVERRIDE;

    // Requires that the specified entry was inserted as StringData.
    StringData get_string(std::size_t ndx) const TIGHTDB_NOEXCEPT;

    void add_string(StringData value);
    void set_string(std::size_t ndx, StringData value);
    void insert_string(std::size_t ndx, StringData value);

    /// Compare two binary columns for equality.
    bool compare_binary(const ColumnBinary&) const;

#ifdef TIGHTDB_DEBUG
    void Verify() const TIGHTDB_OVERRIDE;
    void to_dot(std::ostream&, StringData title) const TIGHTDB_OVERRIDE;
    void dump_node_structure(std::ostream&, int level) const TIGHTDB_OVERRIDE;
    using ColumnBase::dump_node_structure;
#endif

private:
    std::size_t do_get_size() const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE { return size(); }

//    void add(StringData value) { add_string(value); }
//    void set(std::size_t ndx, StringData value) { set_string(ndx, value); }

    void do_insert(std::size_t ndx, BinaryData value, bool add_zero_term);

    // Called by Array::bptree_insert().
    static ref_type leaf_insert(MemRef leaf_mem, ArrayParent&, std::size_t ndx_in_parent,
                                Allocator&, std::size_t insert_ndx,
                                Array::TreeInsert<ColumnBinary>& state);

    struct InsertState: Array::TreeInsert<ColumnBinary> {
        bool m_add_zero_term;
    };

    class EraseLeafElem;

    /// Root must be a leaf. Upgrades the root leaf if
    /// necessary. Returns true if, and only if the root is a 'big
    /// blobs' leaf upon return.
    bool upgrade_root_leaf(std::size_t value_size);

#ifdef TIGHTDB_DEBUG
    void leaf_to_dot(MemRef, ArrayParent*, std::size_t ndx_in_parent,
                     std::ostream&) const TIGHTDB_OVERRIDE;
#endif

    friend class Array;
    friend class ColumnBase;
};




// Implementation

inline std::size_t ColumnBinary::size() const  TIGHTDB_NOEXCEPT
{
    if (root_is_leaf()) {
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Small blobs root leaf
            ArrayBinary* leaf = static_cast<ArrayBinary*>(m_array);
            return leaf->size();
        }
        // Big blobs root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        return leaf->size();
    }
    // Non-leaf root
    return m_array->get_bptree_size();
}

inline BinaryData ColumnBinary::get(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < size());
    if (root_is_leaf()) {
        bool is_big = m_array->context_bit();
        if (!is_big) {
            // Small blobs root leaf
            ArrayBinary* leaf = static_cast<ArrayBinary*>(m_array);
            return leaf->get(ndx);
        }
        // Big blobs root leaf
        ArrayBigBlobs* leaf = static_cast<ArrayBigBlobs*>(m_array);
        return leaf->get(ndx);
    }

    // Non-leaf root
    std::pair<MemRef, std::size_t> p = m_array->get_bptree_leaf(ndx);
    const char* leaf_header = p.first.m_addr;
    std::size_t ndx_in_leaf = p.second;
    Allocator& alloc = m_array->get_alloc();
    bool is_big = Array::get_context_bit_from_header(leaf_header);
    if (!is_big) {
        // Small blobs
        return ArrayBinary::get(leaf_header, ndx_in_leaf, alloc);
    }
    // Big blobs
    return ArrayBigBlobs::get(leaf_header, ndx_in_leaf, alloc);
}

inline StringData ColumnBinary::get_string(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    BinaryData bin = get(ndx);
    TIGHTDB_ASSERT(0 < bin.size());
    return StringData(bin.data(), bin.size()-1);
}

inline void ColumnBinary::set_string(std::size_t ndx, StringData value)
{
    BinaryData bin(value.data(), value.size());
    bool add_zero_term = true;
    set(ndx, bin, add_zero_term);
}

inline void ColumnBinary::add(BinaryData value)
{
    bool add_zero_term = false;
    do_insert(npos, value, add_zero_term);
}

inline void ColumnBinary::insert(std::size_t ndx, BinaryData value)
{
    TIGHTDB_ASSERT(ndx <= size());
    if (size() <= ndx) ndx = npos;
    bool add_zero_term = false;
    do_insert(ndx, value, add_zero_term);
}

inline void ColumnBinary::add_string(StringData value)
{
    BinaryData bin(value.data(), value.size());
    bool add_zero_term = true;
    do_insert(npos, bin, add_zero_term);
}

inline void ColumnBinary::insert_string(std::size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx <= size());
    if (size() <= ndx) ndx = npos;
    BinaryData bin(value.data(), value.size());
    bool add_zero_term = true;
    do_insert(ndx, bin, add_zero_term);
}

} // namespace tightdb

#endif // TIGHTDB_COLUMN_BINARY_HPP
