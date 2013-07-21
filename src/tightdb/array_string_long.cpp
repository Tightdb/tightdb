#ifdef _MSC_VER
#include <win32/types.h> //ssize_t
#endif

#include <tightdb/array_string_long.hpp>
#include <tightdb/array_blob.hpp>
#include <tightdb/column.hpp>

using namespace std;

namespace tightdb {

ArrayStringLong::ArrayStringLong(ArrayParent* parent, size_t pndx, Allocator& alloc):
    Array(type_HasRefs, parent, pndx, alloc),
    m_offsets(type_Normal, 0, 0, alloc), m_blob(0, 0, alloc)
{
    // Add subarrays for long string
    Array::add(m_offsets.get_ref());
    Array::add(m_blob.get_ref());
    m_offsets.set_parent(this, 0);
    m_blob.set_parent(this, 1);
}

ArrayStringLong::ArrayStringLong(ref_type ref, ArrayParent* parent, size_t pndx,
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

void ArrayStringLong::add(StringData value)
{
    bool add_zero_term = true;
    m_blob.add(value.data(), value.size(), add_zero_term);
    size_t end = value.size() + 1;
    if (!m_offsets.is_empty())
        end += to_size_t(m_offsets.back());
    m_offsets.add(end);
}

void ArrayStringLong::set(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx < m_offsets.size());

    size_t begin = 0 < ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    size_t end   = to_size_t(m_offsets.get(ndx));
    bool add_zero_term = true;
    m_blob.replace(begin, end, value.data(), value.size(), add_zero_term);

    size_t new_end = begin + value.size() + 1;
    int64_t diff =  int64_t(new_end) - int64_t(end);
    m_offsets.adjust(ndx, diff);
}

void ArrayStringLong::insert(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx <= m_offsets.size());

    size_t pos = 0 < ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    bool add_zero_term = true;
    m_blob.insert(pos, value.data(), value.size(), add_zero_term);

    m_offsets.insert(ndx,   pos + value.size() + 1);
    m_offsets.adjust(ndx+1,       value.size() + 1);
}

void ArrayStringLong::erase(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < m_offsets.size());

    size_t begin = 0 < ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    size_t end   = to_size_t(m_offsets.get(ndx));

    m_blob.erase(begin, end);
    m_offsets.erase(ndx);
    m_offsets.adjust(ndx, int64_t(begin) - int64_t(end));
}

void ArrayStringLong::resize(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < m_offsets.size());

    size_t len = ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;

    m_offsets.resize(ndx);
    m_blob.resize(len);
}

void ArrayStringLong::clear()
{
    m_blob.clear();
    m_offsets.clear();
}

size_t ArrayStringLong::count(StringData value, size_t begin, size_t end) const
{
    size_t count = 0;

    size_t lastmatch = begin - 1;
    for (;;) {
        lastmatch = find_first(value, lastmatch+1, end);
        if (lastmatch != not_found)
            ++count;
        else break;
    }

    return count;
}

size_t ArrayStringLong::find_first(StringData value, size_t begin, size_t end) const
{
    size_t n = m_offsets.size();
    if (end == size_t(-1)) end = n;
    TIGHTDB_ASSERT(begin <= n && end <= n && begin <= end);

    size_t begin2 = 0 < begin ? to_size_t(m_offsets.get(begin-1)) : 0;
    for (size_t i=begin; i<end; ++i) {
        size_t end2 = to_size_t(m_offsets.get(i));
        size_t end3 = end2 - 1; // Discount terminating zero
        if (StringData(m_blob.get(begin2), end3-begin2) == value) return i;
        begin2 = end2;
    }

    return not_found;
}

void ArrayStringLong::find_all(Array& result, StringData value, size_t add_offset,
                              size_t begin, size_t end) const
{
    size_t first = begin - 1;
    for (;;) {
        first = find_first(value, first + 1, end);
        if (first != not_found)
            result.add(first + add_offset);
        else break;
    }
}


void ArrayStringLong::ForEachOffsetOp::handle_chunk(const int64_t* begin, const int64_t* end)
    TIGHTDB_NOEXCEPT
{
    const int buf_size = 16;
    StringData buf[buf_size];
    size_t offset = m_offset;
    while (buf_size < end - begin) {
        for (int i=0; i<buf_size; ++i) {
            size_t next_offset = *(begin++);
            const char* data = m_blob.get(offset);
            size_t size = next_offset - offset - 1; // Discount the terminating null
            buf[i] = StringData(data, size);
            offset = next_offset;
        }
        m_op->handle_chunk(buf, buf + buf_size);
    }
    for (int i = 0; i < int(end - begin); ++i) {
        size_t next_offset = *(begin+i);
        const char* data = m_blob.get(offset);
        size_t size = next_offset - offset - 1; // Discount the terminating null
        buf[i] = StringData(data, size);
        offset = next_offset;
    }
    m_op->handle_chunk(buf, buf + (end - begin));
    m_offset = offset;
}


#ifdef TIGHTDB_DEBUG

void ArrayStringLong::ToDot(ostream& out, StringData title) const
{
    ref_type ref = get_ref();

    out << "subgraph cluster_arraystringlong" << ref << " {" << endl;
    out << " label = \"ArrayStringLong";
    if (0 < title.size()) out << "\\n'" << title << "'";
    out << "\";" << endl;

    Array::ToDot(out, "stringlong_top");
    m_offsets.ToDot(out, "offsets");
    m_blob.ToDot(out, "blob");

    out << "}" << endl;
}

#endif // TIGHTDB_DEBUG

}
