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
#ifndef TIGHTDB_ARRAY_BLOB_HPP
#define TIGHTDB_ARRAY_BLOB_HPP

#include <tightdb/array.hpp>

namespace tightdb {


class ArrayBlob: public Array {
public:
    explicit ArrayBlob(ArrayParent* = null_ptr, std::size_t ndx_in_parent = 0,
                       Allocator& = Allocator::get_default());
    ArrayBlob(ref_type, ArrayParent*, std::size_t ndx_in_parent,
              Allocator& = Allocator::get_default()) TIGHTDB_NOEXCEPT;
    explicit ArrayBlob(Allocator&) TIGHTDB_NOEXCEPT;
    ~ArrayBlob() TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE {}

    const char* get(std::size_t pos) const TIGHTDB_NOEXCEPT;

    void add(const char* data, std::size_t size, bool add_zero_term = false);
    void insert(std::size_t pos, const char* data, std::size_t size, bool add_zero_term = false);
    void replace(std::size_t begin, std::size_t end, const char* data, std::size_t size,
                 bool add_zero_term = false);
    void erase(std::size_t begin, std::size_t end);
    void resize(std::size_t size);
    void clear();

    /// Get the specified element without the cost of constructing an
    /// array instance. If an array instance is already available, or
    /// you need to get multiple values, then this method will be
    /// slower.
    static const char* get(const char* header, std::size_t pos) TIGHTDB_NOEXCEPT;

    /// Create a new empty blob (binary) array and attach to it. This
    /// does not modify the parent reference information.
    ///
    /// Note that the caller assumes ownership of the allocated
    /// underlying node. It is not owned by the accessor.
    void create();

#ifdef TIGHTDB_DEBUG
    void to_dot(std::ostream&, StringData title = StringData()) const;
#endif

private:
    std::size_t CalcByteLen(std::size_t count, std::size_t width) const TIGHTDB_OVERRIDE;
    std::size_t CalcItemCount(std::size_t bytes,
                              std::size_t width) const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;
    WidthType GetWidthType() const TIGHTDB_OVERRIDE { return wtype_Ignore; }
};




// Implementation:

inline ArrayBlob::ArrayBlob(ArrayParent* parent, std::size_t ndx_in_parent, Allocator& alloc):
    Array(type_Normal, parent, ndx_in_parent, alloc)
{
    // Manually set wtype as array constructor in initiatializer list
    // will not be able to call correct virtual function
    set_header_wtype(wtype_Ignore);
}

inline ArrayBlob::ArrayBlob(ref_type ref, ArrayParent* parent, std::size_t ndx_in_parent,
                            Allocator& alloc) TIGHTDB_NOEXCEPT: Array(alloc)
{
    // Manually create array as doing it in initializer list
    // will not be able to call correct virtual functions
    init_from_ref(ref);
    set_parent(parent, ndx_in_parent);
}

// Creates new array (but invalid, call init_from_ref() to init)
inline ArrayBlob::ArrayBlob(Allocator& alloc) TIGHTDB_NOEXCEPT: Array(alloc) {}

inline const char* ArrayBlob::get(std::size_t pos) const TIGHTDB_NOEXCEPT
{
    return m_data + pos;
}

inline void ArrayBlob::add(const char* data, std::size_t size, bool add_zero_term)
{
    replace(m_size, m_size, data, size, add_zero_term);
}

inline void ArrayBlob::insert(std::size_t pos, const char* data, std::size_t size,
                              bool add_zero_term)
{
    replace(pos, pos, data, size, add_zero_term);
}

inline void ArrayBlob::erase(std::size_t start, std::size_t end)
{
    replace(start, end, null_ptr, 0);
}

inline void ArrayBlob::resize(std::size_t size)
{
    TIGHTDB_ASSERT(size <= m_size);
    replace(size, m_size, null_ptr, 0);
}

inline void ArrayBlob::clear()
{
    replace(0, m_size, null_ptr, 0);
}

inline const char* ArrayBlob::get(const char* header, std::size_t pos) TIGHTDB_NOEXCEPT
{
    const char* data = get_data_from_header(header);
    return data + pos;
}

inline void ArrayBlob::create()
{
    ref_type ref = create_empty_array(type_Normal, wtype_Ignore, get_alloc()); // Throws
    init_from_ref(ref);
}

inline std::size_t ArrayBlob::CalcByteLen(std::size_t count, std::size_t) const
{
    return header_size + count;
}

inline std::size_t ArrayBlob::CalcItemCount(std::size_t bytes, std::size_t) const TIGHTDB_NOEXCEPT
{
    return bytes - header_size;
}


} // namespace tightdb

#endif // TIGHTDB_ARRAY_BLOB_HPP
