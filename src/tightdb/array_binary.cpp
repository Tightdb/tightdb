#ifdef _MSC_VER
#include <win32/types.h>
#endif

#include <tightdb/array_binary.hpp>
#include <tightdb/array_blob.hpp>

using namespace std;

namespace tightdb {


ArrayBinary::ArrayBinary(ArrayParent* parent, size_t pndx, Allocator& alloc):
    Array(type_HasRefs, parent, pndx, alloc),
    m_offsets(type_Normal, 0, 0, alloc), m_blob(0, 0, alloc)
{
    // Add subarrays for long string
    Array::add(m_offsets.get_ref());
    Array::add(m_blob.get_ref());
    m_offsets.set_parent(this, 0);
    m_blob.set_parent(this, 1);
}

ArrayBinary::ArrayBinary(ref_type ref, ArrayParent* parent, size_t pndx,
                         Allocator& alloc) TIGHTDB_NOEXCEPT:
    Array(ref, parent, pndx, alloc), m_offsets(Array::get_as_ref(0), 0, 0, alloc),
    m_blob(Array::get_as_ref(1), 0, 0, alloc)
{
    TIGHTDB_ASSERT(has_refs() && is_leaf()); // has_refs() indicates that this is a long string
    TIGHTDB_ASSERT(Array::size() == 2);
    TIGHTDB_ASSERT(m_blob.size() == (m_offsets.is_empty() ? 0 : to_size_t(m_offsets.back())));

    m_offsets.set_parent(this, 0);
    m_blob.set_parent(this, 1);
}

// Creates new array (but invalid, call update_ref() to init)
//ArrayBinary::ArrayBinary(Allocator& alloc) : Array(alloc) {}

void ArrayBinary::add(BinaryData value)
{
    TIGHTDB_ASSERT(value.size() == 0 || value.data());

    m_blob.add(value.data(), value.size());
    m_offsets.add(m_offsets.is_empty() ? value.size() : m_offsets.back() + value.size());
}

void ArrayBinary::set(size_t ndx, BinaryData value)
{
    TIGHTDB_ASSERT(ndx < m_offsets.size());
    TIGHTDB_ASSERT(value.size() == 0 || value.data());

    size_t start = ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    size_t current_end = to_size_t(m_offsets.get(ndx));
    ssize_t diff =  (start + value.size()) - current_end;

    m_blob.replace(start, current_end, value.data(), value.size());
    m_offsets.adjust(ndx, diff);
}

void ArrayBinary::insert(size_t ndx, BinaryData value)
{
    TIGHTDB_ASSERT(ndx <= m_offsets.size());
    TIGHTDB_ASSERT(value.size() == 0 || value.data());

    size_t pos = ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;

    m_blob.insert(pos, value.data(), value.size());
    m_offsets.insert(ndx, pos + value.size());
    m_offsets.adjust(ndx+1, value.size());
}

void ArrayBinary::set_string(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx < m_offsets.size());
    TIGHTDB_ASSERT(value.size() == 0 || value.data());

    size_t start = ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    size_t current_end = to_size_t(m_offsets.get(ndx));
    ssize_t diff =  (start + value.size() + 1) - current_end;

    bool add_zero_term = true;
    m_blob.replace(start, current_end, value.data(), value.size(), add_zero_term);
    m_offsets.adjust(ndx, diff);
}

void ArrayBinary::insert_string(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx <= m_offsets.size());
    TIGHTDB_ASSERT(value.size() == 0 || value.data());

    size_t pos = ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;

    bool add_zero_term = true;
    m_blob.insert(pos, value.data(), value.size(), add_zero_term);
    m_offsets.insert(ndx, pos + value.size() + 1);
    m_offsets.adjust(ndx+1, value.size() + 1);
}

void ArrayBinary::erase(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < m_offsets.size());

    size_t start = ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    size_t end = to_size_t(m_offsets.get(ndx));

    m_blob.erase(start, end);
    m_offsets.erase(ndx);
    m_offsets.adjust(ndx, int64_t(start) - end);
}

void ArrayBinary::resize(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < m_offsets.size());

    size_t len = ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;

    m_offsets.resize(ndx);
    m_blob.resize(len);
}

void ArrayBinary::clear()
{
    m_blob.clear();
    m_offsets.clear();
}

BinaryData ArrayBinary::get_direct(Allocator& alloc, const char* header, size_t ndx) TIGHTDB_NOEXCEPT
{
    pair<size_t, size_t> p = Array::get_two_as_size(header, 0);
    const char* offsets_header = alloc.translate(p.first);
    const char* blob_header = alloc.translate(p.second);
    size_t begin, end;
    if (ndx) {
        pair<size_t, size_t> p2 = Array::get_two_as_size(offsets_header, ndx-1);
        begin = p2.first;
        end   = p2.second;
    }
    else {
        begin = 0;
        end   = Array::get_as_size(offsets_header, ndx);
    }
    return BinaryData(ArrayBlob::get_direct(blob_header, begin), end-begin);
}


#ifdef TIGHTDB_DEBUG

void ArrayBinary::ToDot(ostream& out, const char* title) const
{
    ref_type ref = get_ref();

    out << "subgraph cluster_binary" << ref << " {" << endl;
    out << " label = \"ArrayBinary";
    if (title) out << "\\n'" << title << "'";
    out << "\";" << endl;

    Array::ToDot(out, "binary_top");
    m_offsets.ToDot(out, "offsets");
    m_blob.ToDot(out, "blob");

    out << "}" << endl;
}

#endif // TIGHTDB_DEBUG

}
