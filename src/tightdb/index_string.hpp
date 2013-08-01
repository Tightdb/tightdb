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
#ifndef TIGHTDB_INDEX_STRING_HPP
#define TIGHTDB_INDEX_STRING_HPP

#include <iostream>

#include <tightdb/column.hpp>
#include <tightdb/column_string.hpp>

namespace tightdb {

typedef StringData (*StringGetter)(void*, std::size_t);

class StringIndex: public Column {
public:
    StringIndex(void* target_column, StringGetter get_func, Allocator&);
    StringIndex(Array::Type, Allocator&);
    StringIndex(ref_type, ArrayParent*, std::size_t ndx_in_parent, void* target_column,
                StringGetter get_func, Allocator&);
    void set_target(void* target_column, StringGetter get_func) TIGHTDB_NOEXCEPT;

    bool is_empty() const;

    void insert(size_t row_ndx, StringData value, bool is_last = false);
    void set(size_t row_ndx, StringData oldValue, StringData new_value);
    void erase(size_t row_ndx, StringData value, bool is_last = false);
    void clear() TIGHTDB_OVERRIDE;

    using Column::insert;
    using Column::erase;

    size_t count(StringData value) const;
    size_t find_first(StringData value) const;
    void   find_all(Array& result, StringData value) const;
    void   distinct(Array& result) const;
    FindRes find_all(StringData value, size_t& ref) const;

    void update_ref(StringData value, size_t old_row_ndx, size_t new_row_ndx);
    using Column::update_ref;

#ifdef TIGHTDB_DEBUG
    void verify_entries(const AdaptiveStringColumn& column) const;
    void to_dot() const { to_dot(std::cerr); }
    void to_dot(std::ostream&) const;
#endif

private:
    void Create();

    void InsertWithOffset(size_t row_ndx, size_t offset, StringData value);
    void InsertRowList(size_t ref, size_t offset, StringData value);
    int32_t GetLastKey() const;
    void UpdateRefs(size_t pos, int diff);

    struct NodeChange {
        size_t ref1;
        size_t ref2;
        enum ChangeType { none, insert_before, insert_after, split } type;
        NodeChange(ChangeType t, size_t r1=0, size_t r2=0) : ref1(r1), ref2(r2), type(t) {}
        NodeChange() : ref1(0), ref2(0), type(none) {}
    };

    // B-Tree functions
    void TreeInsert(size_t row_ndx, int32_t key, size_t offset, StringData value);
    NodeChange DoInsert(size_t ndx, int32_t key, size_t offset, StringData value);
    /// Returns true if there is room or it can join existing entries
    bool LeafInsert(size_t row_ndx, int32_t key, size_t offset, StringData value, bool noextend=false);
    void NodeInsertSplit(size_t ndx, size_t new_ref);
    void NodeInsert(size_t ndx, size_t ref);
    void DoDelete(size_t ndx, StringData, size_t offset);
    void do_update_ref(StringData value, size_t row_ndx, size_t new_row_ndx, size_t offset);

    StringData get(size_t ndx) {return (*m_get_func)(m_target_column, ndx);}

    // Member variables
    void* m_target_column;
    StringGetter m_get_func;

    void NodeAddKey(ref_type ref);

#ifdef TIGHTDB_DEBUG
    void to_dot_2(std::ostream&, StringData title = StringData()) const;
    void array_to_dot(std::ostream&, const Array&) const;
    void keys_to_dot(std::ostream&, const Array&, StringData title = StringData()) const;
#endif
};

} //namespace tightdb

#endif // TIGHTDB_INDEX_STRING_HPP
