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
#ifndef TIGHTDB_ARRAY_STRING_LONG_HPP
#define TIGHTDB_ARRAY_STRING_LONG_HPP

#include <tightdb/array_blob.hpp>

namespace tightdb {


class ArrayStringLong: public Array {
public:
    ArrayStringLong(ArrayParent* = 0, std::size_t ndx_in_parent = 0,
                    Allocator& = Allocator::get_default());
    ArrayStringLong(std::size_t ref, ArrayParent*, std::size_t ndx_in_parent,
                    Allocator& = Allocator::get_default());

    bool is_empty() const TIGHTDB_NOEXCEPT;
    std::size_t size() const TIGHTDB_NOEXCEPT;

    StringData get(std::size_t ndx) const TIGHTDB_NOEXCEPT;

    void add(StringData value);
    void set(std::size_t ndx, StringData value);
    void insert(std::size_t ndx, StringData value);
    void erase(std::size_t ndx);
    void Resize(std::size_t ndx);
    void Clear();

    std::size_t count(StringData value, std::size_t begin = 0, std::size_t end = -1) const;
    std::size_t find_first(StringData value, std::size_t begin = 0 , std::size_t end = -1) const;
    void find_all(Array &result, StringData value, std::size_t add_offset = 0,
                  std::size_t begin = 0, std::size_t end = -1) const;

#ifdef TIGHTDB_DEBUG
    void ToDot(std::ostream& out, StringData title = StringData()) const;
#endif // TIGHTDB_DEBUG

private:
    Array m_offsets;
    ArrayBlob m_blob;
};




// Implementation:

inline bool ArrayStringLong::is_empty() const TIGHTDB_NOEXCEPT
{
    return m_offsets.is_empty();
}

inline std::size_t ArrayStringLong::size() const TIGHTDB_NOEXCEPT
{
    return m_offsets.size();
}

inline StringData ArrayStringLong::get(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < m_offsets.size());
    std::size_t begin, end;
    if (0 < ndx) {
        // FIXME: Consider how much of a performance problem it is,
        // that we have to issue two separate calls to read two
        // consecutive values from an array.
        begin = m_offsets.GetAsSizeT(ndx-1);
        end   = m_offsets.GetAsSizeT(ndx);
    }
    else {
        begin = 0;
        end   = m_offsets.GetAsSizeT(0);
    }
    --end; // Discount the terminating zero
    return StringData(m_blob.get(begin), end-begin);
}


} // namespace tightdb

#endif // TIGHTDB_ARRAY_STRING_LONG_HPP
