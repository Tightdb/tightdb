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
#ifndef TIGHTDB_ALLOC_SLAB_HPP
#define TIGHTDB_ALLOC_SLAB_HPP

#include <stdint.h> // unint8_t etc
#include <string>

#include <tightdb/file.hpp>
#include <tightdb/table_macros.hpp>

namespace tightdb {


// Pre-declarations
class Group;
class GroupWriter;


/// Thrown by Group and SharedGroup constructors if the specified file
/// (or memory buffer) does not appear to contain a valid TightDB
/// database.
struct InvalidDatabase: File::OpenError {
    InvalidDatabase(): File::OpenError("Invalid database") {}
};


class SlabAlloc: public Allocator {
public:
    /// Construct a slab allocator in the unattached state.
    SlabAlloc();

    ~SlabAlloc();

    /// Attach this allocator to the specified file.
    ///
    /// This function is used by free-standing Group instances as well
    /// as by groups that a managed by SharedGroup instances. When
    /// used by free-standing Group instances, no concurrency is
    /// allowed. When used by SharedGroup, concurrency is allowed, but
    /// read_only and no_create must both be false in this case.
    ///
    /// \param is_shared Must be true iff we are called on behalf of SharedGroup.
    ///
    /// \param read_only Open the file in read-only mode. This implies \a no_create.
    ///
    /// \param no_create Fail if the file does not already exist.
    ///
    /// \throw File::OpenError
    void attach_file(const std::string& path, bool is_shared, bool read_only, bool no_create);

    /// Attach this allocator to the specified memory buffer.
    ///
    /// \throw InvalidDatabase
    void attach_buffer(char* data, size_t size, bool take_ownership);

    bool is_attached() const TIGHTDB_NOEXCEPT;

    MemRef Alloc(size_t size);
    MemRef ReAlloc(size_t ref, void* p, size_t size);
    void   Free(size_t ref, void* p); // FIXME: It would be very nice if we could detect an invalid free operation in debug mode
    void*  Translate(size_t ref) const TIGHTDB_NOEXCEPT;

    bool   IsReadOnly(size_t ref) const;
    size_t GetTopRef() const TIGHTDB_NOEXCEPT;
    size_t GetTotalSize() const;

    bool   CanPersist() const;
    size_t GetFileLen() const {return m_baseline;}
    void   FreeAll(size_t filesize=(size_t)-1);
    bool   ReMap(size_t filesize); // Returns false if remapping was not necessary

#ifdef TIGHTDB_DEBUG
    void EnableDebug(bool enable) {m_debugOut = enable;}
    void Verify() const;
    bool IsAllFree() const;
    void Print() const;
#endif // TIGHTDB_DEBUG

private:
    friend class Group;
    friend class GroupWriter;

    enum FreeMode { free_Noop, free_Unalloc, free_Unmap };

    // Define internal tables
    TIGHTDB_TABLE_2(Slabs,
                    offset,     Int,
                    pointer,    Int)
    TIGHTDB_TABLE_2(FreeSpace,
                    ref,    Int,
                    size,   Int)

    static const char default_header[24];

    File      m_file;
    char*     m_data;
    FreeMode  m_free_mode;
    size_t    m_baseline; // Also size of memory mapped portion of database file
    Slabs     m_slabs;
    FreeSpace m_freeSpace;
    FreeSpace m_freeReadOnly;

#ifdef TIGHTDB_DEBUG
    bool      m_debugOut;
#endif

    const FreeSpace& GetFreespace() const {return m_freeReadOnly;}
    bool validate_buffer(const char* data, size_t len) const;

#ifdef TIGHTDB_ENABLE_REPLICATION
    void set_replication(Replication* r) { m_replication = r; }
#endif
};




// Implementation:

inline SlabAlloc::SlabAlloc()
{
    m_data     = 0;
    m_baseline = 8;

#ifdef TIGHTDB_DEBUG
    m_debugOut = false;
#endif
}

inline bool SlabAlloc::is_attached() const  TIGHTDB_NOEXCEPT
{
    return (m_data != NULL);
}

} // namespace tightdb

#endif // TIGHTDB_ALLOC_SLAB_HPP
