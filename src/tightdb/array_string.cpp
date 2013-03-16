#include <cstdlib>
#include <cstdio> // debug
#include <algorithm>
#include <iostream>

#include <tightdb/utilities.hpp>
#include <tightdb/column.hpp>
#include <tightdb/array_string.hpp>

using namespace std;

namespace {

const int max_width = 64;

// When len = 0 returns 0
// When len = 1 returns 4
// When 2 <= len < 256, returns 2**ceil(log2(len+1)).
// Thus, 0 < len < 256 implies that len < round_up(len).
size_t round_up(size_t len)
{
    if (len < 2) return len << 2;
    len |= len >> 1;
    len |= len >> 2;
    len |= len >> 4;
    ++len;
    return len;
}

} // anonymous namespace


namespace tightdb {


void ArrayString::set(size_t ndx, const char* data, size_t size)
{
    TIGHTDB_ASSERT(ndx < m_len);
    TIGHTDB_ASSERT(size < size_t(max_width)); // otherwise we have to use another column type

    // Check if we need to copy before modifying
    CopyOnWrite(); // Throws

    // Make room for the new value plus a zero-termination
    if (m_width <= size) {
        if (size == 0 && m_width == 0) return;

        TIGHTDB_ASSERT(0 < size);

        // Calc min column width
        size_t new_width = ::round_up(size);

        TIGHTDB_ASSERT(size < new_width);

        // FIXME: Should we try to avoid double copying when realloc fails to preserve the address?
        Alloc(m_len, new_width); // Throws

        char* base = m_data;
        char* new_end = base + m_len*new_width;

        // Expand the old values in reverse order
        if (0 < m_width) {
            const char* old_end = base + m_len*m_width;
            while (new_end != base) {
                *--new_end = char(*--old_end + (new_width-m_width));
                {
                    char* new_begin = new_end - (new_width-m_width);
                    fill(new_begin, new_end, 0); // Extend padding with zero bytes
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
    begin = copy(data, data+size, begin);
    fill(begin, end, 0); // Pad with zero bytes
    TIGHTDB_STATIC_ASSERT(max_width <= 128, "Padding size must fit in 7-bits");
    TIGHTDB_ASSERT(end - begin < max_width);
    int pad_size = int(end - begin);
    *end = char(pad_size);
}


void ArrayString::insert(size_t ndx, const char* data, size_t size)
{
    TIGHTDB_ASSERT(ndx <= m_len);
    TIGHTDB_ASSERT(size < size_t(max_width)); // otherwise we have to use another column type

    // Check if we need to copy before modifying
    CopyOnWrite(); // Throws

    // Calc min column width (incl trailing zero-byte)
    size_t new_width = max(m_width, ::round_up(size));

    // Make room for the new value
    Alloc(m_len+1, new_width); // Throws

    if (0 < size || 0 < m_width) {
        char* base = m_data;
        const char* old_end = base + m_len*m_width;
        char*       new_end = base + m_len*new_width + new_width;

        // Move values after insertion point (may expand)
        if (ndx != m_len) {
            if (TIGHTDB_UNLIKELY(m_width < new_width)) {
                char* const new_begin = base + ndx*new_width + new_width;
                if (0 < m_width) {
                    // Expand the old values
                    do {
                        *--new_end = char(*--old_end + (new_width-m_width));
                        {
                            char* new_begin2 = new_end - (new_width-m_width);
                            fill(new_begin2, new_end, 0); // Extend padding with zero bytes
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
            char* pad_begin = copy(data, data+size, new_begin);
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
                        fill(new_begin, new_end, 0); // Extend padding with zero bytes
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

    ++m_len;
}

void ArrayString::erase(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < m_len);

    // Check if we need to copy before modifying
    CopyOnWrite(); // Throws

    // move data backwards after deletion
    if (ndx < m_len-1) {
        char* const new_begin = m_data + ndx*m_width;
        char* const old_begin = new_begin + m_width;
        char* const old_end   = m_data + m_len*m_width;
        copy(old_begin, old_end, new_begin);
    }

    --m_len;

    // Update length in header
    set_header_len(m_len);
}

size_t ArrayString::CalcByteLen(size_t count, size_t width) const
{
    // FIXME: This arithemtic could overflow. Consider using <tightdb/overflow.hpp>
    return 8 + (count * width);
}

size_t ArrayString::CalcItemCount(size_t bytes, size_t width) const TIGHTDB_NOEXCEPT
{
    if (width == 0) return size_t(-1); // zero-width gives infinite space

    const size_t bytes_without_header = bytes - 8;
    return bytes_without_header / width;
}

size_t ArrayString::count(const char* value, size_t start, size_t end) const
{
    const size_t len = strlen(value);
    size_t count = 0;

    size_t lastmatch = start - 1;
    for (;;) {
        lastmatch = FindWithLen(value, len, lastmatch+1, end);
        if (lastmatch != not_found)
            ++count;
        else break;
    }

    return count;
}

size_t ArrayString::find_first(const char* value, size_t start, size_t end) const
{
    TIGHTDB_ASSERT(value);
    return FindWithLen(value, strlen(value), start, end);
}

void ArrayString::find_all(Array& result, const char* value, size_t add_offset, size_t start, size_t end)
{
    TIGHTDB_ASSERT(value);

    const size_t len = strlen(value);

    size_t first = start - 1;
    for (;;) {
        first = FindWithLen(value, len, first + 1, end);
        if (first != (size_t)-1)
            result.add(first + add_offset);
        else break;
    }
}

size_t ArrayString::FindWithLen(const char* value, size_t len, size_t start, size_t end) const
{
    TIGHTDB_ASSERT(value);

    if (end == size_t(-1)) end = m_len;
    if (start == end) return size_t(-1);
    TIGHTDB_ASSERT(start < m_len && end <= m_len && start < end);
    if (m_len == 0) return size_t(-1); // empty list
    if (len >= m_width) return size_t(-1); // A string can never be wider than the column width

    // todo, ensure behaves as expected when m_width = 0

    for (size_t i = start; i < end; ++i) {
        if (value[0] == (char)m_data[i * m_width] && value[len] == (char)m_data[i * m_width + len]) {
            const char* v = m_data + i * m_width;
            if (strncmp(value, v, len) == 0) return i;
        }
    }

    return (size_t)-1; // not found
}

bool ArrayString::Compare(const ArrayString& c) const
{
    if (c.size() != size()) return false;

    for (size_t i = 0; i < size(); ++i) {
        if (get(i) != c.get(i)) return false;
    }

    return true;
}


#ifdef TIGHTDB_DEBUG

void ArrayString::StringStats() const
{
    size_t total = 0;
    size_t longest = 0;

    for (size_t i = 0; i < m_len; ++i) {
        const char* str = get_c_str(i);
        const size_t len = strlen(str)+1;

        total += len;
        if (len > longest) longest = len;
    }

    const size_t size = m_len * m_width;
    const size_t zeroes = size - total;
    const size_t zavg = zeroes / (m_len ? m_len : 1); // avoid possible div by zero

    cout << "Count: " << m_len << "\n";
    cout << "Width: " << m_width << "\n";
    cout << "Total: " << size << "\n";
    cout << "Capacity: " << m_capacity << "\n\n";
    cout << "Bytes string: " << total << "\n";
    cout << "     longest: " << longest << "\n";
    cout << "Bytes zeroes: " << zeroes << "\n";
    cout << "         avg: " << zavg << "\n";
}

/*
void ArrayString::ToDot(FILE* f) const
{
    const size_t ref = getRef();

    fprintf(f, "n%zx [label=\"", ref);

    for (size_t i = 0; i < m_len; ++i) {
        if (i > 0) fprintf(f, " | ");

        fprintf(f, "%s", get_c_str(i));
    }

    fprintf(f, "\"];\n");
}
*/

void ArrayString::ToDot(ostream& out, const char* title) const
{
    const size_t ref = GetRef();

    if (title) {
        out << "subgraph cluster_" << ref << " {" << endl;
        out << " label = \"" << title << "\";" << endl;
        out << " color = white;" << endl;
    }

    out << "n" << hex << ref << dec << "[shape=none,label=<";
    out << "<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\"><TR>" << endl;

    // Header
    out << "<TD BGCOLOR=\"lightgrey\"><FONT POINT-SIZE=\"7\">";
    out << "0x" << hex << ref << dec << "</FONT></TD>" << endl;

    for (size_t i = 0; i < m_len; ++i) {
        out << "<TD>\"" << get_c_str(i) << "\"</TD>" << endl;
    }

    out << "</TR></TABLE>>];" << endl;
    if (title) out << "}" << endl;
}

#endif // TIGHTDB_DEBUG

}
