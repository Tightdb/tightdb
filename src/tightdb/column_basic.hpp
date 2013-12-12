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
#ifndef TIGHTDB_COLUMN_BASIC_HPP
#define TIGHTDB_COLUMN_BASIC_HPP

#include <tightdb/column.hpp>
#include <tightdb/array_basic.hpp>

//
// A BasicColumn can currently only be used for simple unstructured types like float, double.
//

namespace tightdb {

template<class T> struct AggReturnType {
    typedef T sum_type;
};
template<> struct AggReturnType<float> {
    typedef double sum_type;
};


template<class T>
class BasicColumn: public ColumnBase {
public:
    typedef T value_type;

    explicit BasicColumn(Allocator& = Allocator::get_default());
    explicit BasicColumn(ref_type, ArrayParent* = null_ptr, std::size_t ndx_in_parent = 0,
                         Allocator& = Allocator::get_default());
    ~BasicColumn() TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;

    std::size_t size() const TIGHTDB_NOEXCEPT;
    bool is_empty() const TIGHTDB_NOEXCEPT { return size() == 0; }

    T get(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    void add() TIGHTDB_OVERRIDE { add(0); }
    void add(T value);
    void set(std::size_t ndx, T value);
    void insert(std::size_t ndx) TIGHTDB_OVERRIDE { insert(ndx, 0); }
    void insert(std::size_t ndx, T value);
    void erase(std::size_t ndx, bool is_last) TIGHTDB_OVERRIDE;
    void clear() TIGHTDB_OVERRIDE;
    void resize(std::size_t ndx);
    void fill(std::size_t count);
    // Experimental. Overwrites the row at ndx with the last row and removes the last row. For unordered tables.
    void move_last_over(std::size_t ndx) TIGHTDB_OVERRIDE;

    std::size_t count(T value) const;

    typedef typename AggReturnType<T>::sum_type SumType;
    SumType sum(std::size_t begin = 0, std::size_t end = npos,
                std::size_t limit = std::size_t(-1)) const;
    double average(std::size_t begin = 0, std::size_t end = npos,
                   std::size_t limit = std::size_t(-1)) const;
    T maximum(std::size_t begin = 0, std::size_t end = npos,
              std::size_t limit = std::size_t(-1)) const;
    T minimum(std::size_t begin = 0, std::size_t end = npos,
              std::size_t limit = std::size_t(-1)) const;
    std::size_t find_first(T value, std::size_t begin = 0 , std::size_t end = npos) const;
    void find_all(Array& result, T value, std::size_t begin = 0, std::size_t end = npos) const;

    //@{
    /// Find the lower/upper bound for the specified value assuming
    /// that the elements are already sorted in ascending order.
    std::size_t lower_bound(T value) const TIGHTDB_NOEXCEPT;
    std::size_t upper_bound(T value) const TIGHTDB_NOEXCEPT;
    //@{

    /// Compare two columns for equality.
    bool compare(const BasicColumn&) const;

#ifdef TIGHTDB_DEBUG
    void Verify() const TIGHTDB_OVERRIDE;
    void to_dot(std::ostream&, StringData title) const TIGHTDB_OVERRIDE;
    void dump_node_structure(std::ostream&, int level) const TIGHTDB_OVERRIDE;
    using ColumnBase::dump_node_structure;
#endif

private:
    std::size_t do_get_size() const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE { return size(); }

    void do_insert(std::size_t ndx, T value);

    // Called by Array::bptree_insert().
    static ref_type leaf_insert(MemRef leaf_mem, ArrayParent&, std::size_t ndx_in_parent,
                                Allocator&, std::size_t insert_ndx,
                                Array::TreeInsert<BasicColumn<T> >&);

    template <typename R, Action action, class cond>
    R aggregate(T target, std::size_t start, std::size_t end) const;

    class SetLeafElem;
    class EraseLeafElem;

#ifdef TIGHTDB_DEBUG
    static std::size_t verify_leaf(MemRef, Allocator&);
    void leaf_to_dot(MemRef, ArrayParent*, std::size_t ndx_in_parent,
                     std::ostream&) const TIGHTDB_OVERRIDE;
    static void leaf_dumper(MemRef, Allocator&, std::ostream&, int level);
#endif

    friend class Array;
    friend class ColumnBase;
};


} // namespace tightdb


// template implementation
#include <tightdb/column_basic_tpl.hpp>


#endif // TIGHTDB_COLUMN_BASIC_HPP
