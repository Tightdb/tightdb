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
#ifndef TIGHTDB_COLUMN_STRING_HPP
#define TIGHTDB_COLUMN_STRING_HPP

#include <tightdb/unique_ptr.hpp>
#include <tightdb/array_string.hpp>
#include <tightdb/array_string_long.hpp>
#include <tightdb/column.hpp>

namespace tightdb {

// Pre-declarations
class StringIndex;

class AdaptiveStringColumn: public ColumnBase {
public:
    explicit AdaptiveStringColumn(Allocator& = Allocator::get_default());
    explicit AdaptiveStringColumn(ref_type, ArrayParent* = 0, std::size_t ndx_in_parent = 0,
                                  Allocator& = Allocator::get_default());
    ~AdaptiveStringColumn();

    void destroy() TIGHTDB_OVERRIDE;

    std::size_t size() const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;
    bool is_empty() const TIGHTDB_NOEXCEPT;

    StringData get(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    void add() TIGHTDB_OVERRIDE {return add(StringData());}
    void add(StringData);
    void set(std::size_t ndx, StringData);
    void insert(std::size_t ndx) TIGHTDB_OVERRIDE { insert(ndx, StringData()); }
    void insert(std::size_t ndx, StringData);
    void erase(std::size_t ndx) TIGHTDB_OVERRIDE;
    void clear() TIGHTDB_OVERRIDE;
    void resize(std::size_t ndx);
    void fill(std::size_t count);
    void move_last_over(size_t ndx) TIGHTDB_OVERRIDE;

    std::size_t count(StringData value) const;
    std::size_t find_first(StringData value, std::size_t begin = 0 , std::size_t end = -1) const;
    void find_all(Array& result, StringData value, std::size_t start = 0,
                  std::size_t end = -1) const;

    /// Find the lower bound for the specified value assuming that the
    /// elements are already sorted according to
    /// StringData::operator<(). This operation is functionally
    /// identical to std::lower_bound().
    std::size_t lower_bound(StringData value) const TIGHTDB_NOEXCEPT;
    FindRes find_all_indexref(StringData value, size_t& dst) const;

    // Index
    bool HasIndex() const { return m_index != 0; }
    const StringIndex& GetIndex() const { return *m_index; }
    StringIndex& PullIndex() {StringIndex& ndx = *m_index; m_index = 0; return ndx;}
    StringIndex& CreateIndex();
    void SetIndexRef(size_t ref, ArrayParent* parent, size_t pndx);
    void RemoveIndex() { m_index = 0; }

    ref_type get_ref() const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE { return m_array->get_ref(); }
    Allocator& get_alloc() const TIGHTDB_NOEXCEPT { return m_array->get_alloc(); }
    void set_parent(ArrayParent* parent, std::size_t pndx) { m_array->set_parent(parent, pndx); }

    // Optimizing data layout
    bool AutoEnumerate(size_t& ref_keys, size_t& ref_values) const;

    /// Compare two string columns for equality.
    bool compare(const AdaptiveStringColumn&) const;

    bool GetBlock(size_t ndx, ArrayParent** ap, size_t& off) const
    {
        Allocator& alloc = m_array->get_alloc();
        if (!root_is_leaf()) {
            std::pair<size_t, size_t> p = m_array->find_leaf_ref(m_array, ndx);
            bool longstr = m_array->get_hasrefs_from_header(alloc.translate(p.first));
            if (longstr) {
                ArrayStringLong* asl2 = new ArrayStringLong(p.first, 0, 0, alloc);
                *ap = asl2;
            }
            else {
                ArrayString* as2 = new ArrayString(p.first, 0, 0, alloc);
                *ap = as2;
            }
            off = ndx - p.second;
            return longstr;
        }
        else {
            off = 0;
            if (IsLongStrings()) {
                ArrayStringLong* asl2 = new ArrayStringLong(m_array->get_ref(), 0, 0, alloc);
                *ap = asl2;
                return true;
            }
            else {
                ArrayString* as2 = new ArrayString(m_array->get_ref(), 0, 0, alloc);
                *ap = as2;
                return false;
            }
        }

        TIGHTDB_ASSERT(false);
    }

    void foreach(Array::ForEachOp<StringData>*) const TIGHTDB_NOEXCEPT;

#ifdef TIGHTDB_DEBUG
    void Verify() const; // Must be upper case to avoid conflict with macro in ObjC
#endif // TIGHTDB_DEBUG

    // Assumes that this column has only a single leaf node, no
    // internal nodes. In this case has_refs() indicates a long string
    // array.
    bool IsLongStrings() const TIGHTDB_NOEXCEPT { return m_array->has_refs(); }

protected:
    friend class ColumnBase;
    void update_ref(ref_type ref);

    StringData LeafGet(size_t ndx) const TIGHTDB_NOEXCEPT;
    void LeafSet(size_t ndx, StringData value);
    void LeafInsert(size_t ndx, StringData value);
    template<class F> size_t LeafFind(StringData value, size_t begin, size_t end) const;
    void LeafFindAll(Array& result, StringData value, size_t add_offset = 0,
                     size_t begin = 0, size_t end = -1) const;

    void LeafDelete(size_t ndx);

#ifdef TIGHTDB_DEBUG
    virtual void LeafToDot(std::ostream& out, const Array& array) const;
#endif // TIGHTDB_DEBUG

private:
    static const size_t short_string_max_size = 15;

    StringIndex* m_index;

    static void foreach(const Array* parent, Array::ForEachOp<StringData>*) TIGHTDB_NOEXCEPT;
};





// Implementation:

inline StringData AdaptiveStringColumn::get(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < size());
    return m_array->string_column_get(ndx);
}

inline std::size_t AdaptiveStringColumn::lower_bound(StringData value) const TIGHTDB_NOEXCEPT
{
    std::size_t i = 0;
    std::size_t size = this->size();

    while (0 < size) {
        std::size_t half = size / 2;
        std::size_t mid = i + half;
        StringData probe = get(mid);
        if (probe < value) {
            i = mid + 1;
            size -= half + 1;
        }
        else {
            size = half;
        }
    }
    return i;
}

inline void AdaptiveStringColumn::foreach(Array::ForEachOp<StringData>* op) const TIGHTDB_NOEXCEPT
{
    if (TIGHTDB_LIKELY(m_array->is_leaf())) {
        if (m_array->has_refs()) {
            static_cast<const ArrayStringLong*>(m_array)->foreach(op);
        }
        else {
            static_cast<const ArrayString*>(m_array)->foreach(op);
        }
        return;
    }

    foreach(m_array, op);
}


} // namespace tightdb

#endif // TIGHTDB_COLUMN_STRING_HPP
