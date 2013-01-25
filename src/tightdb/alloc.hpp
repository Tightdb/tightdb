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
#ifndef TIGHTDB_ALLOC_HPP
#define TIGHTDB_ALLOC_HPP

#include <cstdlib>

#include <stdint.h> // unint8_t etc

namespace tightdb {

#ifdef TIGHTDB_ENABLE_REPLICATION
struct Replication;
#endif

struct MemRef {
    MemRef(): pointer(NULL), ref(0) {}
    MemRef(void* p, size_t r): pointer(p), ref(r) {}
    void* pointer;
    size_t ref;
};

class Allocator {
public:
    virtual MemRef Alloc(size_t size) {void* p = new char[size]; return MemRef(p,(size_t)p);}
    virtual MemRef ReAlloc(size_t /*ref*/, void* p, size_t size) {void* p2 = realloc(p, size); return MemRef(p2,(size_t)p2);}
    virtual void Free(size_t, void* p) {return delete[] static_cast<char*>(p);}

    virtual void* Translate(size_t ref) const {return (void*)ref;}
    virtual bool IsReadOnly(size_t) const {return false;}

#ifdef TIGHTDB_ENABLE_REPLICATION
    Allocator(): m_replication(0) {}
#endif
    virtual ~Allocator() {}

#ifdef TIGHTDB_ENABLE_REPLICATION
    Replication* get_replication() { return m_replication; }
#endif

#ifdef TIGHTDB_DEBUG
    virtual void Verify() const {};
#endif // TIGHTDB_DEBUG

#ifdef TIGHTDB_ENABLE_REPLICATION
protected:
    Replication* m_replication;
#endif
};

Allocator& GetDefaultAllocator();


} // namespace tightdb

#endif // TIGHTDB_ALLOC_HPP
