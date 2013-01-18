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

#ifndef TIGHTDB_GROUP_HPP
#define TIGHTDB_GROUP_HPP

#include <string>

#include <tightdb/exceptions.hpp>
#include <tightdb/table.hpp>
#include <tightdb/table_basic_fwd.hpp>
#include <tightdb/alloc_slab.hpp>

namespace tightdb {


// Pre-declarations
class SharedGroup;


class Group: private Table::Parent {
public:
    Group();

    enum OpenMode {
        mode_Normal,   ///< Open in read/write mode, create the file if it does not already exist.
        mode_ReadOnly, ///< Open in read-only mode, fail if the file does not already exist.
        mode_NoCreate  ///< Open in read/write mode, fail if the file does not already exist.
    };

    /// Construct a group from a file.
    ///
    /// If the specified file exists in the file system, it must
    /// contain a valid TightDB database. If the file does not exist,
    /// it will be created. A group constructed this way, is the
    /// primary means of accesing and manipulating a TightDB database.
    ///
    /// Changes made to the database via the group are not
    /// automatically committed to the specified file. You may,
    /// however, at any time, explicitely commit your changes by
    /// calling the commit() method. Alternatively you may call
    /// write() to write the entire database to a new file.
    ///
    /// Throws NoSuchFile if the file did not exist and \a mode was
    /// not mode_Normal. Throws PermissionDenied if the file could not
    /// be opened or created due to a permission constraint. Throws
    /// InvalidDatabase if the specified file does not appear to
    /// contain a valid database.
    explicit Group(const std::string& path, OpenMode mode = mode_Normal);

    /// Specification of a memory buffer. The purpose of this class is
    /// neither to allocate nor to deallocate memory. Its only purpose
    /// is to specify the buffer.
    struct BufferSpec {
        char*       m_data;
        std::size_t m_size;
        BufferSpec() {}
        BufferSpec(char* d, std::size_t s): m_data(d), m_size(s) {}
    };

    /// Construct a group from a memory buffer.
    ///
    /// This is similar to constructing a group from a file except
    /// that in this case the database is assumed to be stored in the
    /// specified memory buffer.
    ///
    /// If \a take_ownership is <tt>true</tt>, you pass the ownership
    /// of the specified buffer to the group. In this case the buffer
    /// will eventually be freed using std::free(), so the buffer you
    /// pass, must have been allocated using std::malloc().
    ///
    /// On the other hand, if \a take_ownership is set to
    /// <tt>false</tt>, it is your responsibility to keep the memory
    /// buffer alive during the lifetime of the group, and in case the
    /// buffer needs to be deallocated afterwards, that is your
    /// responsibility too.
    ///
    /// Throws InvalidDatabase if the specified buffer does not appear
    /// to contain a valid database.
    Group(BufferSpec buffer, bool take_ownership = true);

    ~Group();

    bool is_empty() const;

    size_t get_table_count() const;
    const char* get_table_name(size_t table_ndx) const;
    bool has_table(const char* name) const;

    /// Check whether this group has a table with the specified name
    /// and type.
    template<class T> bool has_table(const char* name) const;

    TableRef      get_table(const char* name);
    ConstTableRef get_table(const char* name) const;
    template<class T> typename T::Ref      get_table(const char* name);
    template<class T> typename T::ConstRef get_table(const char* name) const;

    // Serialization

    /// Throws PermissionDenied if the file could not be opened or
    /// created due to a permission constraint.
    void write(const std::string& path);

    /// Ownership of the returned memory buffer is transferred to the
    /// caller. The memory will have been allocated using
    /// std::malloc().
    BufferSpec write_to_mem();

    bool commit(); // FIXME: Should not return a bool - throw exceptions instead

    // Conversion
    template<class S> void to_json(S& out) const;
    void to_string(std::ostream& out) const;

    /// Compare two groups for equality. Two groups are equal if, and
    /// only if, they contain the same tables in the same order, that
    /// is, for each table T at index I in one of the groups, there is
    /// a table at index I in the other group that is equal to T.
    bool operator==(const Group&) const;

    /// Compare two groups for inequality. See operator==().
    bool operator!=(const Group& g) const { return !(*this == g); }

#ifdef TIGHTDB_DEBUG
    void Verify() const; // Must be upper case to avoid conflict with macro in ObjC
    void print() const;
    void print_free() const;
    MemStats stats();
    void enable_mem_diagnostics(bool enable=true) {m_alloc.EnableDebug(enable);}
    void to_dot(std::ostream& out) const;
    void to_dot() const; // For GDB
    void zero_free_space(size_t file_size, size_t readlock_version);
#endif // TIGHTDB_DEBUG

protected:
    friend class GroupWriter;
    friend class SharedGroup;

    void create_from_file(const std::string& filename, OpenMode, bool do_init);

    void invalidate();
    bool in_initial_state() const;
    void init_shared();
    size_t commit(size_t current_version, size_t readlock_version, bool doPersist);
    void rollback();

#ifdef TIGHTDB_ENABLE_REPLICATION
    void set_replication(Replication* r) { m_alloc.set_replication(r); }
#endif

    SlabAlloc& get_allocator() {return m_alloc;}
    Array& get_top_array() {return m_top;}

    // Recursively update all internal refs after commit
    void update_refs(size_t top_ref);

    void update_from_shared(size_t top_ref, size_t len);
    void reset_to_new();

    // Overriding virtual method in ArrayParent
    void update_child_ref(size_t subtable_ndx, size_t new_ref)
    {
        m_tables.Set(subtable_ndx, new_ref);
    }

    // Overriding virtual method in Table::Parent
    void child_destroyed(std::size_t) {} // Ignore

    // Overriding virtual method in ArrayParent
    size_t get_child_ref(size_t subtable_ndx) const
    {
        return m_tables.GetAsRef(subtable_ndx);
    }

    void create(); // FIXME: Could be private
    void create_from_ref(size_t top_ref);

    // May throw WriteError
    template<class S> size_t write_to_stream(S& out);

    // Member variables
    SlabAlloc m_alloc;
    Array m_top;
    Array m_tables;
    ArrayString m_tableNames;
    Array m_freePositions;
    Array m_freeLengths;
    Array m_freeVersions;
    mutable Array m_cachedtables;
    const bool m_is_shared;
    size_t m_readlock_version;

private:
    struct shared_tag {};
    Group(shared_tag);

    Table* get_table_ptr(const char* name);
    Table* get_table_ptr(const char* name, bool& was_created);
    const Table* get_table_ptr(const char* name) const;
    template<class T> T* get_table_ptr(const char* name);
    template<class T> const T* get_table_ptr(const char* name) const;

    Table* get_table_ptr(size_t ndx); // Throws
    const Table* get_table_ptr(size_t ndx) const; // Throws
    Table* create_new_table(const char* name); // Throws

    void clear_cache();

    friend class LangBindHelper;

#ifdef TIGHTDB_ENABLE_REPLICATION
    friend class Replication;
#endif
};



// Implementation

inline const Table* Group::get_table_ptr(size_t ndx) const
{
    return const_cast<Group*>(this)->get_table_ptr(ndx);
}

inline bool Group::has_table(const char* name) const
{
    if (!m_top.IsValid()) return false;

    const size_t i = m_tableNames.find_first(name);
    return i != size_t(-1);
}

template<class T> inline bool Group::has_table(const char* name) const
{
    if (!m_top.IsValid()) return false;
    const size_t i = m_tableNames.find_first(name);
    if (i == size_t(-1)) return false;
    const Table* const table = get_table_ptr(i);
    return T::matches_dynamic_spec(&table->get_spec());
}

inline Table* Group::get_table_ptr(const char* name)
{
    TIGHTDB_ASSERT(m_top.IsValid());
    const size_t ndx = m_tableNames.find_first(name);
    if (ndx != size_t(-1)) {
        // Get table from cache
        return get_table_ptr(ndx);
    }

    return create_new_table(name);
}

inline Table* Group::get_table_ptr(const char* name, bool& was_created)
{
    TIGHTDB_ASSERT(m_top.IsValid());
    const size_t ndx = m_tableNames.find_first(name);
    if (ndx != size_t(-1)) {
        was_created = false;
        // Get table from cache
        return get_table_ptr(ndx);
    }

    was_created = true;
    return create_new_table(name);
}

inline const Table* Group::get_table_ptr(const char* name) const
{
    TIGHTDB_ASSERT(has_table(name));
    return const_cast<Group*>(this)->get_table_ptr(name);
}

template<class T> inline T* Group::get_table_ptr(const char* name)
{
    TIGHTDB_STATIC_ASSERT(IsBasicTable<T>::value, "Invalid table type");
    TIGHTDB_ASSERT(!has_table(name) || has_table<T>(name));

    TIGHTDB_ASSERT(m_top.IsValid());
    const size_t ndx = m_tableNames.find_first(name);
    if (ndx != size_t(-1)) {
        // Get table from cache
        return static_cast<T*>(get_table_ptr(ndx));
    }

    T* const table = static_cast<T*>(create_new_table(name));
    table->set_dynamic_spec(); // FIXME: May fail
    return table;
}

template<class T> inline const T* Group::get_table_ptr(const char* name) const
{
    TIGHTDB_ASSERT(has_table(name));
    return const_cast<Group*>(this)->get_table_ptr<T>(name);
}

inline TableRef Group::get_table(const char* name)
{
    return get_table_ptr(name)->get_table_ref();
}

inline ConstTableRef Group::get_table(const char* name) const
{
    return get_table_ptr(name)->get_table_ref();
}

template<class T> inline typename T::Ref Group::get_table(const char* name)
{
    return get_table_ptr<T>(name)->get_table_ref();
}

template<class T> inline typename T::ConstRef Group::get_table(const char* name) const
{
    return get_table_ptr<T>(name)->get_table_ref();
}

template<class S> size_t Group::write_to_stream(S& out)
{
    // Space for file header
    out.write(default_header, header_len);

    // When serializing to disk we dont want
    // to include free space tracking as serialized
    // files are written without any free space.
    Array top(COLUMN_HASREFS, NULL, 0, m_alloc);
    top.add(m_top.Get(0));
    top.add(m_top.Get(1));

    // Recursively write all arrays
    const uint64_t topPos = top.Write(out); // FIXME: Why does this not return char*?
    const size_t byte_size = out.getpos();

    // Write top ref
    // (since we initially set the last bit in the file header to
    //  zero, it is the first ref block that is valid)
    out.seek(0);
    out.write((const char*)&topPos, 8);

    // Clean up temporary top
    top.Set(0, 0); // reset to avoid recursive delete
    top.Set(1, 0); // reset to avoid recursive delete
    top.Destroy();

    // return bytes written
    return byte_size;
}

template<class S>
void Group::to_json(S& out) const
{
    if (!m_top.IsValid()) {
        out << "{}";
        return;
    }

    out << "{";

    for (size_t i = 0; i < m_tables.Size(); ++i) {
        const char* const name = m_tableNames.Get(i);
        const Table* const table = get_table_ptr(i);

        if (i) out << ",";
        out << "\"" << name << "\"";
        out << ":";
        table->to_json(out);
    }

    out << "}";
}


inline void Group::clear_cache()
{
    const size_t count = m_cachedtables.Size();
    for (size_t i = 0; i < count; ++i) {
        if (Table* const t = reinterpret_cast<Table*>(m_cachedtables.Get(i))) {
            t->invalidate();
            t->unbind_ref();
        }
    }
    m_cachedtables.Clear();
}


} // namespace tightdb

#endif // TIGHTDB_GROUP_HPP
