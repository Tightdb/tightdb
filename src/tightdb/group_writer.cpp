#include <algorithm>

#include <tightdb/safe_int_ops.hpp>
#include <tightdb/group_writer.hpp>
#include <tightdb/group.hpp>
#include <tightdb/alloc_slab.hpp>

using namespace std;
using namespace tightdb;


GroupWriter::GroupWriter(Group& group) :
    m_group(group), m_alloc(group.m_alloc), m_current_version(0)
{
    m_file_map.map(m_alloc.m_file, File::access_ReadWrite, m_alloc.get_baseline()); // Throws
}


void GroupWriter::set_versions(uint64_t current, uint64_t read_lock)
{
    TIGHTDB_ASSERT(read_lock <= current);
    m_current_version  = current;
    m_readlock_version = read_lock;
}


size_t GroupWriter::write_group()
{
    merge_free_space(); // Throws

    Array& top        = m_group.m_top;
    Array& fpositions = m_group.m_free_positions;
    Array& flengths   = m_group.m_free_lengths;
    Array& fversions  = m_group.m_free_versions;
    bool is_shared = m_group.m_is_shared;
    TIGHTDB_ASSERT(fpositions.size() == flengths.size());
    TIGHTDB_ASSERT(!is_shared || fversions.size() == flengths.size());

    // Recursively write all changed arrays (but not 'top' and
    // free-lists yet, as they a going to change along the way.) If
    // free space is available in the attached database file, we use
    // it, but this does not include space that has been release
    // during the current transaction (or since the last commit), as
    // that would lead to clobbering of the previous database version.
    bool recurse = true, persist = true;
    size_t names_pos  = m_group.m_table_names.write(*this, recurse, persist); // Throws
    size_t tables_pos = m_group.m_tables.write(*this, recurse, persist); // Throws

    // We now have a bit of a chicken-and-egg problem. We need to
    // write the free-lists to the file, but the act of writing them
    // will consume free space, and thereby change the free-lists. To
    // solve this problem, we calculate an upper bound on the amount
    // af space required for all of the remaining arrays and allocate
    // the space as one big chunk. This way we can finalize the
    // free-lists before writing them to the file.
    size_t max_free_list_size = fpositions.size();

    // We need to add to the free-list any space that was freed during
    // the current transaction, but to avoid clobering the previous
    // version, we cannot add it yet. Instead we simply account for
    // the space required. Since we will modify the free-lists
    // themselves, we must ensure that the original arrays used by the
    // free-lists are counted as part of the space that was freed
    // during the current transaction.
    fpositions.copy_on_write(); // Throws
    flengths.copy_on_write(); // Throws
    if (is_shared)
        fversions.copy_on_write(); // Throws
    const SlabAlloc::FreeSpace& new_free_space = m_group.m_alloc.get_free_read_only(); // Throws
    max_free_list_size += new_free_space.size();

    // The final allocation of free space (i.e., the call to
    // reserve_free_space() below) may add an extra entry to the
    // free-lists.
    ++max_free_list_size;

    int num_free_lists = is_shared ? 3 : 2;
    int max_top_size = 2 + num_free_lists;
    size_t max_free_space_needed = Array::get_max_byte_size(max_top_size) +
        num_free_lists * Array::get_max_byte_size(max_free_list_size);

    // Reserve space for remaining arrays. We ask for one extra byte
    // beyond the maxumum number that is required. This ensures that
    // even if we end up using the maximum size possible, we still do
    // not end up with a zero size free-space chunk as we deduct the
    // actually used size from it.
    pair<size_t, size_t> reserve = reserve_free_space(max_free_space_needed + 1); // Throws
    size_t reserve_ndx  = reserve.first;
    size_t reserve_size = reserve.second;

    // At this point we have allocated all the space we need, so we
    // can add to the free-lists any free space created during the
    // current transaction (or since last commit). Had we added it
    // earlier, we would have risked clobering the previous database
    // version. Note, however, that this risk would only have been
    // present in the non-transactionl case where there is no version
    // tracking on the free-space chunks.
    {
        size_t n = new_free_space.size();
        for (size_t i = 0; i < n; ++i) {
            SlabAlloc::FreeSpace::ConstCursor r = new_free_space[i];
            size_t pos  = to_size_t(r.ref);
            size_t size = to_size_t(r.size);
            // We always want to keep the list of free space in sorted
            // order (by ascending position) to facilitate merge of
            // adjacent segments. We can find the correct insert
            // postion by binary search
            size_t ndx = fpositions.lower_bound_int(pos);
            fpositions.insert(ndx, pos); // Throws
            flengths.insert(ndx, size); // Throws
            if (is_shared)
                fversions.insert(ndx, m_current_version); // Throws
            // Adjust reserve_ndx to keep in valid
            if (ndx <= reserve_ndx)
                ++reserve_ndx;
        }
    }

    // Before we calculate the actual sizes of the free-list arrays,
    // we must make sure that the final adjustments of the free lists
    // (i.e., the deduction of the actually used space from the
    // reserved chunk,) will not change the byte-size of those arrays.
    size_t reserve_pos = to_size_t(fpositions.get(reserve_ndx));
    TIGHTDB_ASSERT(reserve_size > max_free_space_needed);
    fpositions.ensure_minimum_width(reserve_pos + max_free_space_needed); // Throws

    // Get final sizes of free-list arrays
    size_t free_positions_size = fpositions.get_byte_size();
    size_t free_sizes_size     = flengths.get_byte_size();
    size_t free_versions_size  = is_shared ? fversions.get_byte_size() : 0;

    // Calculate write positions
    size_t free_positions_pos = reserve_pos;
    size_t free_sizes_pos     = free_positions_pos + free_positions_size;
    size_t free_versions_pos  = free_sizes_pos     + free_sizes_size;
    size_t top_pos            = free_versions_pos  + free_versions_size;

    // Update top to point to the calculated positions
    top.set(0, names_pos); // Throws
    top.set(1, tables_pos); // Throws
    top.set(2, free_positions_pos); // Throws
    top.set(3, free_sizes_pos); // Throws
    if (is_shared)
        top.set(4, free_versions_pos); // Throws

    // Get final sizes
    size_t top_size = top.get_byte_size();
    size_t end_pos = top_pos + top_size;
    TIGHTDB_ASSERT(end_pos <= reserve_pos + max_free_space_needed);

    // Deduct the used space from the reserved chunk. Note that we
    // have made sure that the remaining size is never zero. Also, by
    // the call to fpositions.ensure_minimum_width() above, we have
    // made sure that fpositions has the capacity to store the new
    // larger value without reallocation.
    size_t rest = reserve_pos + reserve_size - end_pos;
    TIGHTDB_ASSERT(rest > 0);
    fpositions.set(reserve_ndx, end_pos); // Throws
    flengths.set(reserve_ndx, rest); // Throws

    // The free-list now have their final form, so we can write them
    // to the file
    write_at(free_positions_pos, fpositions.get_header(), free_positions_size); // Throws
    write_at(free_sizes_pos, flengths.get_header(), free_sizes_size); // Throws
    if (is_shared)
        write_at(free_versions_pos, fversions.get_header(), free_versions_size); // Throws

    // Write top
    write_at(top_pos, top.get_header(), top_size); // Throws

    // Return top_pos so that it can be saved in lock file used
    // for coordination
    return top_pos;
}


void GroupWriter::merge_free_space()
{
    Array& positions = m_group.m_free_positions;
    Array& lengths   = m_group.m_free_lengths;
    Array& versions  = m_group.m_free_versions;
    bool is_shared = m_group.m_is_shared;

    if (lengths.is_empty())
        return;

    size_t n = lengths.size() - 1;
    for (size_t i = 0; i < n; ++i) {
        size_t i2 = i + 1;
        size_t pos1  = to_size_t(positions.get(i));
        size_t size1 = to_size_t(lengths.get(i));
        size_t pos2  = to_size_t(positions.get(i2));
        if (pos2 == pos1 + size1) {
            // If this is a shared db, we can only merge
            // segments where no part is currently in use
            if (is_shared) {
                size_t v1 = to_size_t(versions.get(i));
                if (v1 >= m_readlock_version)
                    continue;
                size_t v2 = to_size_t(versions.get(i2));
                if (v2 >= m_readlock_version)
                    continue;
            }

            // Merge
            size_t size2 = to_size_t(lengths.get(i2));
            lengths.set(i, size1 + size2);
            positions.erase(i2);
            lengths.erase(i2);
            if (is_shared)
                versions.erase(i2);

            --n;
            --i;
        }
    }
}


size_t GroupWriter::get_free_space(size_t size)
{
    TIGHTDB_ASSERT(size % 8 == 0); // 8-byte alignment
    TIGHTDB_ASSERT(m_file_map.get_size() % 8 == 0); // 8-byte alignment

    pair<size_t, size_t> p = reserve_free_space(size);

    Array& positions = m_group.m_free_positions;
    Array& lengths   = m_group.m_free_lengths;
    Array& versions  = m_group.m_free_versions;
    bool is_shared = m_group.m_is_shared;

    // Claim space from identified chunk
    size_t chunk_ndx  = p.first;
    size_t chunk_pos  = to_size_t(positions.get(chunk_ndx));
    size_t chunk_size = p.second;
    TIGHTDB_ASSERT(chunk_size >= size);

    size_t rest = chunk_size - size;
    if (rest > 0) {
        positions.set(chunk_ndx, chunk_pos + size); // FIXME: Undefined conversion to signed
        lengths.set(chunk_ndx, rest); // FIXME: Undefined conversion to signed
    }
    else {
        positions.erase(chunk_ndx);
        lengths.erase(chunk_ndx);
        if (is_shared)
            versions.erase(chunk_ndx);
    }

    return chunk_pos;
}


pair<size_t, size_t> GroupWriter::reserve_free_space(size_t size)
{
    Array& lengths  = m_group.m_free_lengths;
    Array& versions = m_group.m_free_versions;
    bool is_shared = m_group.m_is_shared;

    // Since we do a first-fit search for small chunks, the top pieces
    // are likely to get smaller and smaller. So when we are looking
    // for bigger chunks we are likely to find them faster by skipping
    // the first half of the list.
    size_t end   = lengths.size();
    size_t begin = size < 1024 ? 0 : end / 2;

    // Do we have a free space we can reuse?
  again:
    for (size_t i = begin; i != end; ++i) {
        size_t chunk_size = to_size_t(lengths.get(i));
        if (chunk_size >= size) {
            // Only blocks that are not occupied by current readers
            // are allowed to be used.
            if (is_shared) {
                size_t ver = to_size_t(versions.get(i));
                if (ver >= m_readlock_version)
                    continue;
            }

            // Match found!
            return make_pair(i, chunk_size);
        }
    }

    if (begin > 0) {
        begin = 0;
        end = begin;
        goto again;
    }

    // No free space, so we have to extend the file.
    return extend_free_space(size);
}


pair<size_t, size_t> GroupWriter::extend_free_space(size_t requested_size)
{
    Array& positions = m_group.m_free_positions;
    Array& lengths   = m_group.m_free_lengths;
    Array& versions  = m_group.m_free_versions;
    bool is_shared = m_group.m_is_shared;

    // FIXME: What we really need here is the "logical" size of the
    // file and not the real size. The real size may have changed
    // without the free space information having been adjusted
    // accordingly. This can happen, for example, if write_group()
    // fails before writing the new top-ref, but after having extended
    // the file size. We currently do not have a concept of a logical
    // file size, but if provided, it would have to be stored as part
    // of a database version such that it is updated atomically
    // together with the rest of the contents of the version.
    size_t file_size = m_file_map.get_size();

    bool extend_last_chunk = false;
    size_t last_chunk_size;
    if (!positions.is_empty()) {
        bool in_use = false;
        if (is_shared) {
            size_t ver = to_size_t(versions.back());
            if (ver >= m_readlock_version)
                in_use = true;
        }
        if (!in_use) {
            size_t last_pos  = to_size_t(positions.back());
            size_t last_size = to_size_t(lengths.back());
            TIGHTDB_ASSERT(last_size < requested_size);
            TIGHTDB_ASSERT(last_pos + last_size <= file_size);
            if (last_pos + last_size == file_size) {
                extend_last_chunk = true;
                last_chunk_size = last_size;
                requested_size -= last_size;
            }
        }
    }

    size_t min_file_size = file_size;
    if (int_add_with_overflow_detect(min_file_size, requested_size)) {
        throw runtime_error("File size overflow");
    }

    // We double the size until we reach 'stop_doubling_size'. From
    // then on we increment the size in steps of
    // 'stop_doubling_size'. This is to achieve a reasonable
    // compromise between minimizing fragmentation (maximizing
    // performance) and minimizing over-allocation.
    size_t stop_doubling_size = 128 * (1024*1024L); // = 128 MiB
    TIGHTDB_ASSERT(stop_doubling_size % 8 == 0);

    size_t new_file_size = file_size;
    while (new_file_size < min_file_size) {
        if (new_file_size < stop_doubling_size) {
            // The file contains at least a header, so the size can never
            // be zero. We need this to ensure that the number of
            // iterations will be finite.
            TIGHTDB_ASSERT(new_file_size != 0);
            // Be sure that the doubling does not overflow
            TIGHTDB_ASSERT(stop_doubling_size <= numeric_limits<size_t>::max() / 2);
            new_file_size *= 2;
        }
        else {
            if (int_add_with_overflow_detect(new_file_size, stop_doubling_size)) {
                new_file_size = numeric_limits<size_t>::max();
                new_file_size &= ~size_t(0x7); // 8-byte alignment
            }
        }
    }

    // The size must be a multiple of 8. This is guaranteed as long as
    // the initial size is a multiple of 8.
    TIGHTDB_ASSERT(new_file_size % 8 == 0);

    // Note: File::prealloc() may misbehave under race conditions (see
    // documentation of File::prealloc()). Fortunately, no race
    // conditions can occur, because in transactional mode we hold a
    // write lock at this time, and in non-transactional mode it is
    // the responsibility of the user to ensure non-concurrent
    // mutation access.
    m_alloc.m_file.prealloc(0, new_file_size);

    m_file_map.remap(m_alloc.m_file, File::access_ReadWrite, new_file_size);

    size_t chunk_ndx  = positions.size();
    size_t chunk_size = new_file_size - file_size;
    if (extend_last_chunk) {
        --chunk_ndx;
        chunk_size += last_chunk_size;
        TIGHTDB_ASSERT(chunk_size % 8 == 0); // 8-byte alignment
        lengths.set(chunk_ndx, chunk_size);
    }
    else { // Else add new free space
        TIGHTDB_ASSERT(chunk_size % 8 == 0); // 8-byte alignment
        positions.add(file_size);
        lengths.add(chunk_size);
        if (is_shared)
            versions.add(0); // new space is always free for writing
    }

    return make_pair(chunk_ndx, chunk_size);
}


size_t GroupWriter::write(const char* data, size_t size)
{
    // Get position of free space to write in (expanding file if needed)
    size_t pos = get_free_space(size);
    TIGHTDB_ASSERT((pos & 0x7) == 0); // Write position should always be 64bit aligned

    // Write the block
    char* dest = m_file_map.get_addr() + pos;
    copy(data, data+size, dest);

    // return the position it was written
    return pos;
}


void GroupWriter::write_at(size_t pos, const char* data, size_t size)
{
    char* dest = m_file_map.get_addr() + pos;

    char* mmap_end = m_file_map.get_addr() + m_file_map.get_size();
    char* copy_end = dest + size;
    TIGHTDB_ASSERT(copy_end <= mmap_end);
    static_cast<void>(mmap_end);
    static_cast<void>(copy_end);

    copy(data, data+size, dest);
}


void GroupWriter::commit(ref_type new_top_ref)
{
    // Write data
    m_file_map.sync(); // Throws

    // File header is 24 bytes, composed of three 64-bit
    // blocks. The two first being top_refs (only one valid
    // at a time) and the last being the info block.
    char* file_header = m_file_map.get_addr();

    // Least significant bit in last byte of info block indicates
    // which top_ref block is valid
    int current_valid_ref = file_header[16+7] & 0x1;
    int new_valid_ref = current_valid_ref ^ 0x1;

    // FIXME: What rule guarantees that the new top ref is written to
    // physical medium before the swapping bit?

    // Update top ref pointer
    uint64_t* top_refs = reinterpret_cast<uint64_t*>(file_header);
    top_refs[new_valid_ref] = new_top_ref;
    file_header[16+7] = char(new_valid_ref); // swap

    // Write new header to disk
    m_file_map.sync(); // Throws
}



#ifdef TIGHTDB_DEBUG

void GroupWriter::dump()
{
    Array& fpositions = m_group.m_free_positions;
    Array& flengths   = m_group.m_free_lengths;
    Array& fversions  = m_group.m_free_versions;
    bool is_shared = m_group.m_is_shared;

    size_t count = flengths.size();
    cout << "count: " << count << ", m_size = " << m_file_map.get_size() << ", "
        "version >= " << m_readlock_version << "\n";
    if (!is_shared) {
        for (size_t i = 0; i < count; ++i) {
            cout << i << ": " << fpositions.get(i) << ", " << flengths.get(i) << "\n";
        }
    }
    else {
        for (size_t i = 0; i < count; ++i) {
            cout << i << ": " << fpositions.get(i) << ", " << flengths.get(i) << " - "
                "" << fversions.get(i) << "\n";
        }
    }
}

#endif
