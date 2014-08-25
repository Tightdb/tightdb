#include <algorithm>

#ifdef _WIN32
#include <win32/types.h>
#endif

#include <tightdb/array_blobs_big.hpp>
#include <tightdb/column.hpp>


using namespace std;
using namespace tightdb;


void ArrayBigBlobs::add(BinaryData value, bool add_zero_term)
{
    TIGHTDB_ASSERT(value.size() == 0 || value.data());

    ArrayBlob new_blob(m_alloc);
    new_blob.create(); // Throws
    new_blob.add(value.data(), value.size(), add_zero_term); // Throws
    Array::add(int_fast64_t(new_blob.get_ref())); // Throws
}


void ArrayBigBlobs::set(std::size_t ndx, BinaryData value, bool add_zero_term)
{
    TIGHTDB_ASSERT(ndx < size());
    TIGHTDB_ASSERT(value.size() == 0 || value.data());

    ArrayBlob blob(m_alloc);
    ref_type ref = get_as_ref(ndx);
    blob.init_from_ref(ref);
    blob.set_parent(this, ndx);
    blob.clear(); // Throws
    blob.add(value.data(), value.size(), add_zero_term); // Throws
}


void ArrayBigBlobs::insert(size_t ndx, BinaryData value, bool add_zero_term)
{
    TIGHTDB_ASSERT(ndx <= size());
    TIGHTDB_ASSERT(value.size() == 0 || value.data());

    ArrayBlob new_blob(m_alloc);
    new_blob.create(); // Throws
    new_blob.add(value.data(), value.size(), add_zero_term); // Throws
    Array::insert(ndx, int_fast64_t(new_blob.get_ref())); // Throws
}


size_t ArrayBigBlobs::count(BinaryData value, bool is_string,
                            size_t begin, size_t end) const TIGHTDB_NOEXCEPT
{
    size_t num_matches = 0;

    size_t begin_2 = begin;
    for (;;) {
        size_t ndx = find_first(value, is_string, begin_2, end);
        if (ndx == not_found)
            break;
        ++num_matches;
        begin_2 = ndx + 1;
    }

    return num_matches;
}


size_t ArrayBigBlobs::find_first(BinaryData value, bool is_string,
                                 size_t begin, size_t end) const TIGHTDB_NOEXCEPT
{
    if (end == npos)
        end = m_size;
    TIGHTDB_ASSERT(begin <= m_size && end <= m_size && begin <= end);

    // When strings are stored as blobs, they are always zero-terminated
    // but the value we get as input might not be.
    size_t value_size = value.size();
    size_t full_size = is_string ? value_size+1 : value_size;

    for (size_t i = begin; i != end; ++i) {
        ref_type ref = get_as_ref(i);
        const char* blob_header = get_alloc().translate(ref);
        size_t blob_size = get_size_from_header(blob_header);
        if (blob_size == full_size) {
            const char* blob_value = ArrayBlob::get(blob_header, 0);
            if (equal(blob_value, blob_value + value_size, value.data()))
                return i;
        }
    }

    return not_found;
}


void ArrayBigBlobs::find_all(Column& result, BinaryData value, bool is_string, size_t add_offset,
                             size_t begin, size_t end)
{
    size_t begin_2 = begin;
    for (;;) {
        size_t ndx = find_first(value, is_string, begin_2, end);
        if (ndx == not_found)
            break;
        result.add(add_offset + ndx); // Throws
        begin_2 = ndx + 1;
    }
}


ref_type ArrayBigBlobs::bptree_leaf_insert(size_t ndx, BinaryData value, bool add_zero_term,
                                           TreeInsertBase& state)
{
    size_t leaf_size = size();
    TIGHTDB_ASSERT(leaf_size <= TIGHTDB_MAX_BPNODE_SIZE);
    if (leaf_size < ndx)
        ndx = leaf_size;
    if (TIGHTDB_LIKELY(leaf_size < TIGHTDB_MAX_BPNODE_SIZE)) {
        insert(ndx, value, add_zero_term);
        return 0; // Leaf was not split
    }

    // Split leaf node
    ArrayBigBlobs new_leaf(m_alloc);
    new_leaf.create(); // Throws
    if (ndx == leaf_size) {
        new_leaf.add(value, add_zero_term);
        state.m_split_offset = ndx;
    }
    else {
        for (size_t i = ndx; i != leaf_size; ++i) {
            ref_type blob_ref = Array::get_as_ref(i);
            new_leaf.Array::add(blob_ref);
        }
        Array::truncate(ndx); // Avoiding destruction of transferred blobs
        add(value, add_zero_term);
        state.m_split_offset = ndx + 1;
    }
    state.m_split_size = leaf_size + 1;
    return new_leaf.get_ref();
}


#ifdef TIGHTDB_DEBUG

void ArrayBigBlobs::Verify() const
{
    TIGHTDB_ASSERT(has_refs());
    for (size_t i = 0; i < size(); ++i) {
        ref_type blob_ref = Array::get_as_ref(i);
        ArrayBlob blob(m_alloc);
        blob.init_from_ref(blob_ref);
        blob.Verify();
    }
}

void ArrayBigBlobs::to_dot(std::ostream& out, bool, StringData title) const
{
    ref_type ref = get_ref();

    out << "subgraph cluster_binary" << ref << " {" << endl;
    out << " label = \"ArrayBinary";
    if (title.size() != 0)
        out << "\\n'" << title << "'";
    out << "\";" << endl;

    Array::to_dot(out, "big_blobs_leaf");

    for (size_t i = 0; i < size(); ++i) {
        ref_type blob_ref = Array::get_as_ref(i);
        ArrayBlob blob(m_alloc);
        blob.init_from_ref(blob_ref);
        blob.set_parent(const_cast<ArrayBigBlobs*>(this), i);
        blob.to_dot(out);
    }

    out << "}" << endl;

    to_dot_parent_edge(out);
}

#endif
