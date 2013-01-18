#include <tightdb/string_buffer.hpp>
#include <tightdb/group_shared.hpp>

// Does not work for windows yet
#ifndef _MSC_VER

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/file.h>

#include "../test/testsettings.hpp"

// Wrap pthread function calls with the pthread bug finding tool (program execution will be slower). 
// Works both in debug and release mode. Define the flag in testsettings.h
#ifdef TIGHTDB_PTHREADS_TEST
#include "../test/pthread_test.hpp"
#endif

using namespace std;
using namespace tightdb;


struct SharedGroup::ReadCount {
    uint32_t version;
    uint32_t count;
};

struct SharedGroup::SharedInfo {
    uint16_t version;
    uint16_t flags;

    pthread_mutex_t readmutex;
    pthread_mutex_t writemutex;
    uint64_t filesize;

    uint64_t current_top;
    volatile uint32_t current_version;

    uint32_t infosize;
    uint32_t capacity; // -1 so it can also be used as mask
    uint32_t put_pos;
    uint32_t get_pos;
    ReadCount readers[32]; // has to be power of two
};


#ifdef TIGHTDB_ENABLE_REPLICATION

SharedGroup::SharedGroup(const string& path_to_database_file, bool no_create,
                         DurabiltyLevel dlevel):
    m_group(Group::shared_tag()), m_info(NULL), m_version(-1),
    m_replication(Replication::degenerate_tag())
{
    init(path_to_database_file, no_create, dlevel);
}

SharedGroup::SharedGroup(replication_tag, const string& path_to_database_file,
                         DurabiltyLevel dlevel):
    m_group(Group::shared_tag()), m_info(NULL), m_version(-1),
    m_replication(path_to_database_file)
{
    m_group.set_replication(&m_replication);

    init(!path_to_database_file.empty() ? path_to_database_file :
         Replication::get_path_to_database_file(), false, dlevel);
}

#else // ! TIGHTDB_ENABLE_REPLICATION

SharedGroup::SharedGroup(const string& path_to_database_file, bool no_create,
                         DurabilityLevel dlevel):
    m_group(Group::shared_tag()), m_info(NULL), m_version(-1)
{
    init(path_to_database_file, no_create, dlevel);
}

#endif // ! TIGHTDB_ENABLE_REPLICATION


// NOTES ON CREATION AND DESTRUCTION OF SHARED MUTEXES:
//
// According to the 'process sharing example' in the POSIX man page
// for pthread_mutexattr_init() other processes may continue to use a
// shared mutex after exit of the process that initialized it. Also,
// the example does not contain any call to pthread_mutex_destroy(),
// so apparently a shared mutex need not be destroyed at all, nor can
// it be that a shared mutex is associated with any resources that are
// local to the initializing process.
//
// While it is not explicitely stated in the man page, we shall also
// assume that is is valid to initialize a shared mutex twice without
// an intervending call to pthread_mutex_destroy(). We need to be able
// to reinitialize a shared mutex if the first initializing process
// craches and leaves the shared memory in an undefined state.

// Issues with current implementation:
//
// - Possible reinitialization due to temporary unlocking during downgrade of file lock

void SharedGroup::init(const string& path_to_database_file, bool no_create_file,
                       DurabilityLevel dlevel)
{
    m_lockfile_path = path_to_database_file + ".lock";

open_start:
    // Open shared coordination buffer
    m_fd = open(m_lockfile_path.c_str(), O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (m_fd < 0) return;

    // FIXME: Handle lock file removal in case of failure below

    bool need_init = false;
    size_t len    = 0;
    struct stat statbuf;

    // If we can get an exclusive lock we know that the file is
    // either new (empty) or a leftover from a previously
    // crashed process (needing re-initialization)
    if (flock(m_fd, LOCK_EX|LOCK_NB) == 0) {
        // There is a slight window between opening the file and getting the
        // lock where another process could have deleted the file
        if (fstat(m_fd, &statbuf) < 0 || statbuf.st_nlink == 0) {
            close(m_fd);
            goto open_start; // retry
        }
        // Get size
        len = statbuf.st_size;

        // Handle empty files (first user)
        if (len == 0) {
            // Create new file
            len = sizeof(SharedInfo);
            const int r = ftruncate(m_fd, len);
            if (r != 0) {
                close(m_fd);
                return;
            }
        }
        need_init = true;
    }
    else if (flock(m_fd, LOCK_SH) == 0) {
        // There is a slight window between opening the file and getting the
        // lock where another process could have deleted the file
        if (fstat(m_fd, &statbuf) < 0 || statbuf.st_nlink == 0 || statbuf.st_size == 0) {
            close(m_fd);
            goto open_start; // retry
        }

        len = statbuf.st_size;
    }
    else {
        // We needed a shared lock so that the file would not
        // get deleted by other processes
        close(m_fd);
        return;
    }

    // Map to memory
    void* const p = mmap(0, len, PROT_READ|PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (p == (void*)-1) {
        close(m_fd);
        return;
    }
    m_info = (SharedInfo*)p;

    if (need_init) {
        // If we are the first we may have to create the database file
        // but we invalidate the internals right after to avoid conflicting
        // with old state when starting transactions
        const Group::OpenMode group_open_mode =
            no_create_file ? Group::mode_NoCreate : Group::mode_Normal;
        m_group.create_from_file(path_to_database_file, group_open_mode, true);
        m_group.invalidate();

        // Initialize mutexes so that they can be shared between processes
        pthread_mutexattr_t mattr;
        pthread_mutexattr_init(&mattr);
        // FIXME: Must verify availability of optional feature: #ifdef _POSIX_THREAD_PROCESS_SHARED
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
        // FIXME: Should also do pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST). Check for availability with: #if _POSIX_THREADS >= 200809L
        pthread_mutex_init(&m_info->readmutex, &mattr);
        pthread_mutex_init(&m_info->writemutex, &mattr);
        pthread_mutexattr_destroy(&mattr);

        SlabAlloc& alloc = m_group.get_allocator();

        // Set initial values
        m_info->version  = 0;
        m_info->flags    = dlevel; // durability level is fixed from creation
        m_info->filesize = alloc.GetFileLen();
        m_info->infosize = (uint32_t)len;
        m_info->current_top = alloc.GetTopRef();
        m_info->current_version = 0;
        m_info->capacity = 32-1;
        m_info->put_pos  = 0;
        m_info->get_pos  = 0;

        // Set initial version so we can track if other instances
        // change the db
        m_version = 0;

        // Downgrade lock to shared now that it is initialized,
        // so other processes can share it as well
// FIXME: This downgrading of the lock is not guaranteed to be atomic
        flock(m_fd, LOCK_SH);
    }
    else {
        if (m_info->version != 0)
            return; // unsupported version

        // Durability level cannot be changed at runtime
        if (m_info->flags != dlevel)
            return;

        // Setup the group, but leave it in invalid state
        m_group.create_from_file(path_to_database_file, Group::mode_NoCreate, false);
    }

//    m_isValid = true;

#ifdef TIGHTDB_DEBUG
    m_state = SHARED_STATE_READY;
#endif
}


SharedGroup::~SharedGroup()
{
    TIGHTDB_ASSERT(m_state == SHARED_STATE_READY);

    if (m_info) {
        // If we can get an exclusive lock on the file
        // we know that we are the only user (since all
        // users take at least shared locks on the file.
        // So that means that we have to delete it when done
        // (to avoid someone later opening a stale file
        // with uinitialized mutexes)
// FIXME: This upgrading of the lock is not guaranteed to be atomic
        if (flock(m_fd, LOCK_EX|LOCK_NB) == 0) {
            // If the db file is just backing for a transient
            // data structure, we can delete it when done.
            if (m_info->flags == durability_MemOnly) {
                const size_t path_len = m_lockfile_path.size()-5; // remove ".lock"
                const std::string db_path = m_lockfile_path.substr(0, path_len);
                remove(db_path.c_str());
            }

            pthread_mutex_destroy(&m_info->readmutex);
            pthread_mutex_destroy(&m_info->writemutex);

            munmap((void*)m_info, m_info->infosize);

            remove(m_lockfile_path.c_str());
        }
        else {
            munmap((void*)m_info, m_info->infosize);
        }

        close(m_fd); // also releases lock
    }
}

bool SharedGroup::has_changed() const
{
    // Have we changed since last transaction?
    // Visibility of changes can be delayed when using has_changed() because m_info->current_version is tested
    // outside mutexes. However, the delay is finite on architectures that have hardware cache coherency (ARM, x64, x86, 
    // POWER, UltraSPARC, etc) because it guarantees write propagation (writes to m_info->current_version occur on 
    // system bus and make cache controllers invalidate caches of reader). Some excotic architectures may need
    // explicit synchronization which isn't implemented yet.
    TIGHTDB_SYNC_IF_NO_CACHE_COHERENCE
    const bool is_changed = (m_version != m_info->current_version);
    return is_changed;
}

const Group& SharedGroup::begin_read()
{
    TIGHTDB_ASSERT(m_state == SHARED_STATE_READY);
    TIGHTDB_ASSERT(m_group.get_allocator().IsAllFree());

    size_t new_topref = 0;
    size_t new_filesize = 0;

    pthread_mutex_lock(&m_info->readmutex);
    {
        // Get the current top ref
        new_topref   = m_info->current_top;
        new_filesize = m_info->filesize;
        m_version    = m_info->current_version;

        // Update reader list
        if (ringbuf_is_empty()) {
            const ReadCount r2 = {m_info->current_version, 1};
            ringbuf_put(r2);
        }
        else {
            ReadCount& r = ringbuf_get_last();
            if (r.version == m_info->current_version)
                ++(r.count);
            else {
                const ReadCount r2 = {m_info->current_version, 1};
                ringbuf_put(r2);
            }
        }
    }
    pthread_mutex_unlock(&m_info->readmutex);

    // Make sure the group is up-to-date
    // zero ref means that the file has just been created
    m_group.update_from_shared(new_topref, new_filesize);

#ifdef TIGHTDB_DEBUG
    m_state = SHARED_STATE_READING;
    m_group.Verify();
#endif

    return m_group;
}

void SharedGroup::end_read()
{
    TIGHTDB_ASSERT(m_state == SHARED_STATE_READING);
    TIGHTDB_ASSERT(m_version != (uint32_t)-1);

    pthread_mutex_lock(&m_info->readmutex);
    {
        // Find entry for current version
        const size_t ndx = ringbuf_find((uint32_t)m_version);
        TIGHTDB_ASSERT(ndx != (size_t)-1);
        ReadCount& r = ringbuf_get(ndx);

        // Decrement count and remove as many entries as possible
        if (r.count == 1 && ringbuf_is_first(ndx)) {
            ringbuf_remove_first();
            while (!ringbuf_is_empty() && ringbuf_get_first().count == 0) {
                ringbuf_remove_first();
            }
        }
        else {
            TIGHTDB_ASSERT(r.count > 0);
            --r.count;
        }
    }
    pthread_mutex_unlock(&m_info->readmutex);

    // The read may have allocated some temporary state
    m_group.invalidate();

#ifdef TIGHTDB_DEBUG
    m_state = SHARED_STATE_READY;
#endif
}

Group& SharedGroup::begin_write()
{
    TIGHTDB_ASSERT(m_state == SHARED_STATE_READY);
    TIGHTDB_ASSERT(m_group.get_allocator().IsAllFree());

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (m_replication) {
        error_code err = m_replication.begin_write_transact();
        if (err) throw_error(err);
    }
#endif

    // Get write lock
    // Note that this will not get released until we call
    // end_write().
    pthread_mutex_lock(&m_info->writemutex);

    // Get the current top ref
    const size_t new_topref   = m_info->current_top;
    const size_t new_filesize = m_info->filesize;

    // Make sure the group is up-to-date
    // zero ref means that the file has just been created
    m_group.update_from_shared(new_topref, new_filesize);

#ifdef TIGHTDB_DEBUG
    m_state = SHARED_STATE_WRITING;
    m_group.Verify();
#endif

    return m_group;
}

void SharedGroup::commit()
{
    TIGHTDB_ASSERT(m_state == SHARED_STATE_WRITING);

    // Get version info
    size_t current_version;
    size_t readlock_version;
    pthread_mutex_lock(&m_info->readmutex);
    {
        current_version = m_info->current_version + 1;

        if (ringbuf_is_empty())
            readlock_version = current_version;
        else {
            const ReadCount& r = ringbuf_get_first();
            readlock_version = r.version;
        }
    }
    pthread_mutex_unlock(&m_info->readmutex);

    // Reset version tracking in group if we are
    // starting from a new lock file
    if (current_version == 1) {
        m_group.init_shared();
    }

    // Do the actual commit
    const bool doPersist = (m_info->flags == durability_Full);
    const size_t new_topref = m_group.commit(current_version, readlock_version, doPersist);

    // Get the new top ref
    const SlabAlloc& alloc = m_group.get_allocator();
    const size_t new_filesize = alloc.GetFileLen();

    // Update reader info
    pthread_mutex_lock(&m_info->readmutex);
    {
        m_info->current_top = new_topref;
        m_info->filesize    = new_filesize;
        ++m_info->current_version;
    }
    pthread_mutex_unlock(&m_info->readmutex);

    // Release write lock
    pthread_mutex_unlock(&m_info->writemutex);

    // Save last version for has_changed()
    m_version = current_version;

    m_group.invalidate();

#ifdef TIGHTDB_DEBUG
    m_state = SHARED_STATE_READY;
#endif

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (m_replication) {
        if (!m_replication.commit_write_transact()) throw_error(ERROR_INTERRUPTED);
    }
#endif
}

void SharedGroup::rollback()
{
    TIGHTDB_ASSERT(m_state == SHARED_STATE_WRITING);

    // Clear all changes made during transaction
    m_group.rollback();

    // Release write lock
    pthread_mutex_unlock(&m_info->writemutex);

    m_group.invalidate();

#ifdef TIGHTDB_DEBUG
    m_state = SHARED_STATE_READY;
#endif

#ifdef TIGHTDB_ENABLE_REPLICATION
    if (m_replication) m_replication.rollback_write_transact();
#endif
}

bool SharedGroup::ringbuf_is_empty() const
{
    return (ringbuf_size() == 0);
}

size_t SharedGroup::ringbuf_size() const
{
    return ((m_info->put_pos - m_info->get_pos) & m_info->capacity);
}

size_t SharedGroup::ringbuf_capacity() const
{
    return m_info->capacity+1;
}

bool SharedGroup::ringbuf_is_first(size_t ndx) const {
    return (ndx == m_info->get_pos);
}

SharedGroup::ReadCount& SharedGroup::ringbuf_get(size_t ndx)
{
    return m_info->readers[ndx];
}

SharedGroup::ReadCount& SharedGroup::ringbuf_get_first()
{
    return m_info->readers[m_info->get_pos];
}

SharedGroup::ReadCount& SharedGroup::ringbuf_get_last()
{
    const uint32_t lastPos = (m_info->put_pos - 1) & m_info->capacity;
    return m_info->readers[lastPos];
}

void SharedGroup::ringbuf_remove_first() {
    m_info->get_pos = (m_info->get_pos + 1) & m_info->capacity;
}

void SharedGroup::ringbuf_put(const ReadCount& v)
{
    const bool isFull = (ringbuf_size() == (m_info->capacity+1));

    if (isFull) {
        //TODO: expand buffer
        TIGHTDB_ASSERT(false);
    }

    m_info->readers[m_info->put_pos] = v;
    m_info->put_pos = (m_info->put_pos + 1) & m_info->capacity;
}

size_t SharedGroup::ringbuf_find(uint32_t version) const
{
    uint32_t pos = m_info->get_pos;
    while (pos != m_info->put_pos) {
        const ReadCount& r = m_info->readers[pos];
        if (r.version == version)
            return pos;

        pos = (pos + 1) & m_info->capacity;
    }

    return (size_t)-1;
}

#ifdef TIGHTDB_DEBUG

void SharedGroup::test_ringbuf()
{
    TIGHTDB_ASSERT(ringbuf_is_empty());

    const ReadCount rc = {1, 1};
    ringbuf_put(rc);
    TIGHTDB_ASSERT(ringbuf_size() == 1);

    ringbuf_remove_first();
    TIGHTDB_ASSERT(ringbuf_is_empty());

    // Fill buffer
    const size_t capacity = ringbuf_capacity();
    for (size_t i = 0; i < capacity; ++i) {
        const ReadCount r = {1, (uint32_t)i};
        ringbuf_put(r);
        TIGHTDB_ASSERT(ringbuf_get_last().count == i);
    }
    for (size_t i = 0; i < 32; ++i) {
        const ReadCount& r = ringbuf_get_first();
        TIGHTDB_ASSERT(r.count == i);

        ringbuf_remove_first();
    }
    TIGHTDB_ASSERT(ringbuf_is_empty());

}

void SharedGroup::zero_free_space()
{
    // Get version info
    size_t current_version;
    size_t readlock_version;
    size_t file_size;
    pthread_mutex_lock(&m_info->readmutex);
    {
        current_version = m_info->current_version + 1;
        file_size = m_info->filesize;

        if (ringbuf_is_empty())
            readlock_version = current_version;
        else {
            const ReadCount& r = ringbuf_get_first();
            readlock_version = r.version;
        }
    }
    pthread_mutex_unlock(&m_info->readmutex);

    m_group.zero_free_space(file_size, readlock_version);
}

#endif // TIGHTDB_DEBUG

#endif //_MSV_VER
