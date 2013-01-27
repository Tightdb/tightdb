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
#ifndef TIGHTDB_ARRAY_STRING_HPP
#define TIGHTDB_ARRAY_STRING_HPP

#include <cstring>

#include <tightdb/array.hpp>

namespace tightdb {

class ArrayString : public Array {
public:
    ArrayString(ArrayParent *parent=NULL, size_t pndx=0,
                Allocator& alloc = Allocator::get_default());
    ArrayString(size_t ref, const ArrayParent *parent, size_t pndx,
                Allocator& alloc = Allocator::get_default());
    ArrayString(Allocator& alloc);

    const char* Get(size_t ndx) const TIGHTDB_NOEXCEPT;
    bool add();
    bool add(const char* value);
    bool Set(size_t ndx, const char* c_str);
    bool Set(size_t ndx, const char* data, size_t size);
    bool Insert(size_t ndx, const char* c_str);
    bool Insert(size_t ndx, const char* data, size_t size);
    void Delete(size_t ndx);

    size_t count(const char* value, size_t start=0, size_t end=-1) const;
    size_t find_first(const char* value, size_t start=0 , size_t end=-1) const;
    void find_all(Array& result, const char* value, size_t add_offset = 0, size_t start = 0, size_t end = -1);

    /// Construct an empty string array and return just the reference
    /// to the underlying memory.
    ///
    /// \return Zero if allocation fails.
    ///
    static size_t create_empty_string_array(Allocator&);

    /// Compare two string arrays for equality.
    bool Compare(const ArrayString&) const;

#ifdef TIGHTDB_DEBUG
    void StringStats() const;
    //void ToDot(FILE* f) const;
    void ToDot(std::ostream& out, const char* title=NULL) const;
#endif // TIGHTDB_DEBUG

private:
    size_t FindWithLen(const char* value, size_t len, size_t start , size_t end) const;
    virtual size_t CalcByteLen(size_t count, size_t width) const;
    virtual size_t CalcItemCount(size_t bytes, size_t width) const TIGHTDB_NOEXCEPT;
    virtual WidthType GetWidthType() const {return TDB_MULTIPLY;}
};





// Implementation:

inline size_t ArrayString::create_empty_string_array(Allocator& alloc)
{
    return create_empty_array(COLUMN_NORMAL, TDB_MULTIPLY, alloc);
}

inline ArrayString::ArrayString(ArrayParent *parent, size_t ndx_in_parent,
                                Allocator& alloc): Array(alloc)
{
    const size_t ref = create_empty_string_array(alloc);
    if (!ref) throw_error(ERROR_OUT_OF_MEMORY); // FIXME: Check that this exception is handled properly in callers
    init_from_ref(ref);
    SetParent(parent, ndx_in_parent);
    update_ref_in_parent();
}

inline ArrayString::ArrayString(size_t ref, const ArrayParent *parent, size_t ndx_in_parent,
                                Allocator& alloc): Array(alloc)
{
    // Manually create array as doing it in initializer list
    // will not be able to call correct virtual functions
    init_from_ref(ref);
    SetParent(const_cast<ArrayParent *>(parent), ndx_in_parent);
}

// Creates new array (but invalid, call UpdateRef to init)
inline ArrayString::ArrayString(Allocator& alloc): Array(alloc) {}

inline const char* ArrayString::Get(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < m_len);

    if (m_width == 0) return "";
    else return reinterpret_cast<char*>(m_data + (ndx * m_width));
}

inline bool ArrayString::Set(std::size_t ndx, const char* value)
{
    TIGHTDB_ASSERT(ndx < m_len);
    TIGHTDB_ASSERT(value);

    return Set(ndx, value, std::strlen(value));
}


} // namespace tightdb

#endif // TIGHTDB_ARRAY_STRING_HPP
