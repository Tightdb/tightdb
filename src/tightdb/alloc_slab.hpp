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
struct InvalidDatabase: File::AccessError {
    InvalidDatabase(): File::AccessError("Invalid database") {}
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
    /// \param is_shared Must be true if, and only if we are called on
    /// behalf of SharedGroup.
    ///
    /// \param read_only Open the file in read-only mode. This implies
    /// \a no_create.
    ///
    /// \param no_create Fail if the file does not already exist.
    ///
    /// \throw File::AccessError
    void attach_file(const std::string& path, bool is_shared, bool read_only, bool no_create);

    /// Attach this allocator to the specified memory buffer.
    ///
    /// \throw InvalidDatabase
    void attach_buffer(char* data, std::size_t size, bool take_ownership);

    bool is_attached() const TIGHTDB_NOEXCEPT;

    MemRef alloc(std::size_t size) TIGHTDB_OVERRIDE;
    MemRef realloc(ref_type, const char*, std::size_t size) TIGHTDB_OVERRIDE;
    void   free(ref_type, const char*) TIGHTDB_OVERRIDE; // FIXME: It would be very nice if we could detect an invalid free operation in debug mode
    char*  translate(ref_type) const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;

    bool   is_read_only(ref_type) const TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE;
    ref_type get_top_ref() const TIGHTDB_NOEXCEPT;
    std::size_t get_total_size() const;

    /// Get the size of the attached file or buffer. This size is not
    /// affected by new allocations. After attachment it can be
    /// modified only by a call to remap().
    std::size_t get_base_size() const { return m_baseline; }

    void   free_all(std::size_t file_size = std::size_t(-1));
    bool   remap(std::size_t file_size); // Returns false if remapping was not necessary

#ifdef TIGHTDB_DEBUG
    void enable_debug(bool enable) { m_debug_out = enable; }
    void Verify() const;
    bool is_all_free() const;
    void print() const;
#endif // TIGHTDB_DEBUG

private:
    friend class Group;
    friend class GroupWriter;

    enum FreeMode { free_Noop, free_Unalloc, free_Unmap };

    // Define internal tables
    TIGHTDB_TABLE_2(Slabs,
                    ref_end, Int, // One plus last ref targeting this slab
                    addr,    Int) // Memory pointer to this slab
    TIGHTDB_TABLE_2(FreeSpace,
                    ref,    Int,
                    size,   Int)

    static const char default_header[24];

    File        m_file;
    char*       m_data;
    FreeMode    m_free_mode;
    std::size_t m_baseline; // Also size of memory mapped portion of database file
    Slabs       m_slabs;
    FreeSpace   m_free_space;
    FreeSpace   m_free_read_only;

#ifdef TIGHTDB_DEBUG
    bool        m_debug_out;
#endif

    const FreeSpace& GetFreespace() const { return m_free_read_only; }
    bool validate_buffer(const char* data, std::size_t len) const;

#ifdef TIGHTDB_ENABLE_REPLICATION
    Replication* get_replication() const TIGHTDB_NOEXCEPT { return m_replication; }
    void set_replication(Replication* r) TIGHTDB_NOEXCEPT { m_replication = r; }
#endif
};




// Implementation:

inline SlabAlloc::SlabAlloc()
{
    m_data     = 0;
    m_baseline = 8;

#ifdef TIGHTDB_DEBUG
    m_debug_out = false;
#endif
}

inline bool SlabAlloc::is_attached() const  TIGHTDB_NOEXCEPT
{
    return m_data != 0;
}

} // namespace tightdb

#endif // TIGHTDB_ALLOC_SLAB_HPP
