#include <algorithm>

#include <realm/array_blob.hpp>

using namespace realm;


void ArrayBlob::replace(size_t begin, size_t end, const char* data, size_t data_size, bool add_zero_term)
{
    REALM_ASSERT_3(begin, <=, end);
    REALM_ASSERT_3(end, <=, m_size);
    REALM_ASSERT(data_size == 0 || data);

    copy_on_write(); // Throws

    // Reallocate if needed
    size_t remove_size = end - begin;
    size_t add_size = data_size;
    if (add_zero_term)
        ++add_size;
    size_t new_size = m_size - remove_size + add_size;
    // also updates header
    alloc(new_size, 1); // Throws

    char* modify_begin = m_data + begin;

    // Resize previous space to fit new data
    // (not needed if we append to end)
    if (begin != m_size) {
        const char* old_begin = m_data + end;
        const char* old_end   = m_data + m_size;
        if (remove_size < add_size) { // expand gap
            char* new_end = m_data + new_size;
            std::copy_backward(old_begin, old_end, new_end);
        }
        else if (add_size < remove_size) { // shrink gap
            char* new_begin = modify_begin + add_size;
            std::copy(old_begin, old_end, new_begin);
        }
    }

    // Insert the data
    modify_begin = std::copy(data, data+data_size, modify_begin);
    if (add_zero_term)
        *modify_begin = 0;

    m_size = new_size;
}


#ifdef REALM_DEBUG  // LCOV_EXCL_START ignore debug functions

void ArrayBlob::verify() const
{
    REALM_ASSERT(!has_refs());
}

void ArrayBlob::to_dot(std::ostream& out, StringData title) const
{
    ref_type ref = get_ref();

    if (title.size() != 0) {
        out << "subgraph cluster_" << ref << " {" << std::endl;
        out << " label = \"" << title << "\";" << std::endl;
        out << " color = white;" << std::endl;
    }

    out << "n" << std::hex << ref << std::dec << "[shape=none,label=<";
    out << "<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\"><TR>" << std::endl;

    // Header
    out << "<TD BGCOLOR=\"lightgrey\"><FONT POINT-SIZE=\"7\"> ";
    out << "0x" << std::hex << ref << std::dec << "<BR/>";
    out << "</FONT></TD>" << std::endl;

    // Values
    out << "<TD>";
    out << size() << " bytes"; //TODO: write content
    out << "</TD>" << std::endl;

    out << "</TR></TABLE>>];" << std::endl;

    if (title.size() != 0)
        out << "}" << std::endl;

    to_dot_parent_edge(out);
}

#endif // LCOV_EXCL_STOP ignore debug functions
