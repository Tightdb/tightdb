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
#ifndef TIGHTDB_ARRAY_BINARY_HPP
#define TIGHTDB_ARRAY_BINARY_HPP

#include <tightdb/array_blob.hpp>

namespace tightdb {


class ArrayBinary : public Array {
public:
    ArrayBinary(ArrayParent* parent=NULL, size_t pndx=0,
                Allocator& alloc = Allocator::get_default());
    ArrayBinary(size_t ref, ArrayParent* parent, size_t pndx,
                Allocator& alloc = Allocator::get_default());
    //ArrayBinary(Allocator& alloc);

    bool is_empty() const TIGHTDB_NOEXCEPT;
    size_t Size() const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;

    const char* Get(size_t ndx) const TIGHTDB_NOEXCEPT;
    size_t GetLen(size_t ndx) const TIGHTDB_NOEXCEPT;

    void add(const char* value, size_t len);
    void Set(size_t ndx, const char* value, size_t len);
    void Insert(size_t ndx, const char* value, size_t len);
    void Delete(size_t ndx);
    void Resize(size_t ndx);
    void Clear();

#ifdef TIGHTDB_DEBUG
    void ToDot(std::ostream& out, const char* title=NULL) const;
#endif // TIGHTDB_DEBUG

private:
    Array m_offsets;
    ArrayBlob m_blob;
};


} // namespace tightdb

#endif // TIGHTDB_ARRAY_BINARY_HPP
