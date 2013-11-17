#include <cstdlib>
#include <cstdio> // debug
#include <algorithm>
#include <iostream>
#include <iomanip>

#include <tightdb/utilities.hpp>
#include <tightdb/column.hpp>
#include <tightdb/array_string.hpp>

using namespace std;
using namespace tightdb;


namespace {

const int max_width = 64;

// When size = 0 returns 0
// When size = 1 returns 4
// When 2 <= size < 256, returns 2**ceil(log2(size+1)).
// Thus, 0 < size < 256 implies that size < round_up(size).
size_t round_up(size_t size)
{
    if (size < 2)
        return size << 2;
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    ++size;
    return size;
}

} // anonymous namespace


void ArrayString::set(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx < m_size);
    TIGHTDB_ASSERT(value.size() < size_t(max_width)); // otherwise we have to use another column type

    // Check if we need to copy before modifying
    copy_on_write(); // Throws

    // Make room for the new value plus a zero-termination
    if (m_width <= value.size()) {
        if (value.size() == 0 && m_width == 0)
            return;

        TIGHTDB_ASSERT(0 < value.size());

        // Calc min column width
        size_t new_width = ::round_up(value.size());

        TIGHTDB_ASSERT(value.size() < new_width);

        // FIXME: Should we try to avoid double copying when realloc fails to preserve the address?
        alloc(m_size, new_width); // Throws

        char* base = m_data;
        char* new_end = base + m_size*new_width;

        // Expand the old values in reverse order
        if (0 < m_width) {
            const char* old_end = base + m_size*m_width;
            while (new_end != base) {
                *--new_end = char(*--old_end + (new_width-m_width));
                {
                    char* new_begin = new_end - (new_width-m_width);
                    fill(new_begin, new_end, 0); // Extend zero padding
                    new_end = new_begin;
                }
                {
                    const char* old_begin = old_end - (m_width-1);
                    new_end = copy_backward(old_begin, old_end, new_end);
                    old_end = old_begin;
                }
            }
        }
        else {
            while (new_end != base) {
                *--new_end = char(new_width-1);
                {
                    char* new_begin = new_end - (new_width-1);
                    fill(new_begin, new_end, 0); // Fill with zero bytes
                    new_end = new_begin;
                }
            }
        }

        m_width = new_width;
    }

    TIGHTDB_ASSERT(0 < m_width);

    // Set the value
    char* begin = m_data + (ndx * m_width);
    char* end   = begin + (m_width-1);
    begin = copy(value.data(), value.data()+value.size(), begin);
    fill(begin, end, 0); // Pad with zero bytes
    TIGHTDB_STATIC_ASSERT(max_width <= 128, "Padding size must fit in 7-bits");
    TIGHTDB_ASSERT(end - begin < max_width);
    int pad_size = int(end - begin);
    *end = char(pad_size);
}


void ArrayString::insert(size_t ndx, StringData value)
{
    TIGHTDB_ASSERT(ndx <= m_size);
    TIGHTDB_ASSERT(value.size() < size_t(max_width)); // otherwise we have to use another column type

    // Check if we need to copy before modifying
    copy_on_write(); // Throws

    // Calc min column width (incl trailing zero-byte)
    size_t new_width = max(m_width, ::round_up(value.size()));

    // Make room for the new value
    alloc(m_size+1, new_width); // Throws

    if (0 < value.size() || 0 < m_width) {
        char* base = m_data;
        const char* old_end = base + m_size*m_width;
        char*       new_end = base + m_size*new_width + new_width;

        // Move values after insertion point (may expand)
        if (ndx != m_size) {
            if (TIGHTDB_UNLIKELY(m_width < new_width)) {
                char* const new_begin = base + ndx*new_width + new_width;
                if (0 < m_width) {
                    // Expand the old values
                    do {
                        *--new_end = char(*--old_end + (new_width-m_width));
                        {
                            char* new_begin2 = new_end - (new_width-m_width);
                            fill(new_begin2, new_end, 0); // Extend zero padding
                            new_end = new_begin2;
                        }
                        {
                            const char* old_begin = old_end - (m_width-1);
                            new_end = copy_backward(old_begin, old_end, new_end);
                            old_end = old_begin;
                        }
                    }
                    while (new_end != new_begin);
                }
                else {
                    do {
                        *--new_end = char(new_width-1);
                        {
                            char* new_begin2 = new_end - (new_width-1);
                            fill(new_begin2, new_end, 0); // Fill with zero bytes
                            new_end = new_begin2;
                        }
                    }
                    while (new_end != new_begin);
                }
            }
            else {
                // when no expansion just move the following entries forward
                const char* old_begin = base + ndx*m_width;
                new_end = copy_backward(old_begin, old_end, new_end);
                old_end = old_begin;
            }
        }

        // Set the value
        {
            char* new_begin = new_end - new_width;
            char* pad_begin = copy(value.data(), value.data()+value.size(), new_begin);
            --new_end;
            fill(pad_begin, new_end, 0); // Pad with zero bytes
            TIGHTDB_STATIC_ASSERT(max_width <= 128, "Padding size must fit in 7-bits");
            TIGHTDB_ASSERT(new_end - pad_begin < max_width);
            int pad_size = int(new_end - pad_begin);
            *new_end = char(pad_size);
            new_end = new_begin;
        }

        // Expand values before insertion point
        if (TIGHTDB_UNLIKELY(m_width < new_width)) {
            if (0 < m_width) {
                while (new_end != base) {
                    *--new_end = char(*--old_end + (new_width-m_width));
                    {
                        char* new_begin = new_end - (new_width-m_width);
                        fill(new_begin, new_end, 0); // Extend zero padding
                        new_end = new_begin;
                    }
                    {
                        const char* old_begin = old_end - (m_width-1);
                        new_end = copy_backward(old_begin, old_end, new_end);
                        old_end = old_begin;
                    }
                }
            }
            else {
                while (new_end != base) {
                    *--new_end = char(new_width-1);
                    {
                        char* new_begin = new_end - (new_width-1);
                        fill(new_begin, new_end, 0); // Fill with zero bytes
                        new_end = new_begin;
                    }
                }
            }
            m_width = new_width;
        }
    }

    ++m_size;
}

void ArrayString::erase(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < m_size);

    // Check if we need to copy before modifying
    copy_on_write(); // Throws

    // move data backwards after deletion
    if (ndx < m_size-1) {
        char* new_begin = m_data + ndx*m_width;
        char* old_begin = new_begin + m_width;
        char* old_end   = m_data + m_size*m_width;
        copy(old_begin, old_end, new_begin);
    }

    --m_size;

    // Update size in header
    set_header_size(m_size);
}

size_t ArrayString::CalcByteLen(size_t count, size_t width) const
{
    // FIXME: This arithemtic could overflow. Consider using one of
    // the functions in <tightdb/safe_int_ops.hpp>
    return header_size + (count * width);
}

size_t ArrayString::CalcItemCount(size_t bytes, size_t width) const TIGHTDB_NOEXCEPT
{
    if (width == 0) return size_t(-1); // zero-width gives infinite space

    size_t bytes_without_header = bytes - header_size;
    return bytes_without_header / width;
}

size_t ArrayString::count(StringData value, size_t begin, size_t end) const TIGHTDB_NOEXCEPT
{
    size_t num_matches = 0;

    size_t begin_2 = begin;
    for (;;) {
        size_t ndx = find_first(value, begin_2, end);
        if (ndx == not_found)
            break;
        ++num_matches;
        begin_2 = ndx + 1;
    }

    return num_matches;
}

size_t ArrayString::find_first(StringData value, size_t begin, size_t end) const TIGHTDB_NOEXCEPT
{
    if (end == size_t(-1))
        end = m_size;
    TIGHTDB_ASSERT(begin <= m_size && end <= m_size && begin <= end);

    if (m_width == 0)
        return value.size() == 0 && begin < end ? begin : size_t(-1);

    // A string can never be wider than the column width
    if (m_width <= value.size())
        return size_t(-1);

    if (value.size() == 0) {
        const char* data = m_data + (m_width-1);
        for (size_t i = begin; i != end; ++i) {
            size_t size = (m_width-1) - data[i * m_width];
            if (TIGHTDB_UNLIKELY(size == 0))
                return i;
        }
    }
    else {
        for (size_t i = begin; i != end; ++i) {
            const char* data = m_data + (i * m_width);
            size_t j = 0;
            for (;;) {
                if (TIGHTDB_LIKELY(data[j] != value[j]))
                    break;
                ++j;
                if (TIGHTDB_UNLIKELY(j == value.size())) {
                    size_t size = (m_width-1) - data[m_width-1];
                    if (TIGHTDB_LIKELY(size == value.size()))
                        return i;
                    break;
                }
            }
        }
    }

    return not_found;
}

void ArrayString::find_all(Array& result, StringData value, size_t add_offset,
                           size_t begin, size_t end)
{
    size_t begin_2 = begin;
    for (;;) {
        size_t ndx = find_first(value, begin_2, end);
        if (ndx == not_found)
            break;
        result.add(add_offset + ndx); // Throws
        begin_2 = ndx + 1;
    }
}

bool ArrayString::compare_string(const ArrayString& c) const
{
    if (c.size() != size())
        return false;

    for (size_t i = 0; i < size(); ++i) {
        if (get(i) != c.get(i))
            return false;
    }

    return true;
}

ref_type ArrayString::bptree_leaf_insert(size_t ndx, StringData value, TreeInsertBase& state)
{
    size_t leaf_size = size();
    TIGHTDB_ASSERT(leaf_size <= TIGHTDB_MAX_LIST_SIZE);
    if (leaf_size < ndx) ndx = leaf_size;
    if (TIGHTDB_LIKELY(leaf_size < TIGHTDB_MAX_LIST_SIZE)) {
        insert(ndx, value);
        return 0; // Leaf was not split
    }

    // Split leaf node
    ArrayString new_leaf(0, 0, get_alloc());
    if (ndx == leaf_size) {
        new_leaf.add(value);
        state.m_split_offset = ndx;
    }
    else {
        for (size_t i = ndx; i != leaf_size; ++i) {
            new_leaf.add(get(i));
        }
        resize(ndx);
        add(value);
        state.m_split_offset = ndx + 1;
    }
    state.m_split_size = leaf_size + 1;
    return new_leaf.get_ref();
}


#ifdef TIGHTDB_DEBUG

void ArrayString::string_stats() const
{
    size_t total = 0;
    size_t longest = 0;

    for (size_t i = 0; i < m_size; ++i) {
        StringData str = get(i);
        size_t size = str.size() + 1;
        total += size;
        if (size > longest) longest = size;
    }

    size_t size = m_size * m_width;
    size_t zeroes = size - total;
    size_t zavg = zeroes / (m_size ? m_size : 1); // avoid possible div by zero

    cout << "Size: " << m_size << "\n";
    cout << "Width: " << m_width << "\n";
    cout << "Total: " << size << "\n";
    cout << "Capacity: " << m_capacity << "\n\n";
    cout << "Bytes string: " << total << "\n";
    cout << "     longest: " << longest << "\n";
    cout << "Bytes zeroes: " << zeroes << "\n";
    cout << "         avg: " << zavg << "\n";
}


void ArrayString::to_dot(ostream& out, StringData title) const
{
    ref_type ref = get_ref();

    if (title.size() != 0) {
        out << "subgraph cluster_" << ref << " {" << endl;
        out << " label = \"" << title << "\";" << endl;
        out << " color = white;" << endl;
    }

    out << "n" << hex << ref << dec << "[shape=none,label=<";
    out << "<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\"><TR>" << endl;

    // Header
    out << "<TD BGCOLOR=\"lightgrey\"><FONT POINT-SIZE=\"7\">";
    out << "0x" << hex << ref << dec << "</FONT></TD>" << endl;

    for (size_t i = 0; i < m_size; ++i)
        out << "<TD>\"" << get(i) << "\"</TD>" << endl;

    out << "</TR></TABLE>>];" << endl;

    if (title.size() != 0)
        out << "}" << endl;

    to_dot_parent_edge(out);
}

#endif // TIGHTDB_DEBUG
