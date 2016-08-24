/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#include <algorithm>

#include <realm/array_blob.hpp>

using namespace realm;

constexpr size_t MAX_BLOB_NODE_SIZE = (max_array_payload - Array::header_size) & 0xFFFFFFF0;

size_t ArrayBlob::read(size_t pos, char* buffer, size_t max_size) const noexcept
{
    if (get_context_flag()) {
        size_t size_copied = 0;
        size_t ndx = 0;
        size_t current_size = Array::get_size_from_header(m_alloc.translate(Array::get_as_ref(ndx)));

        // Find the blob to start from
        while (pos >= current_size) {
            ndx++;
            if (ndx >= size()) {
                return 0;
            }
            pos -= current_size;
            current_size = Array::get_size_from_header(m_alloc.translate(Array::get_as_ref(ndx)));
        }

        while (max_size) {
            ArrayBlob blob(m_alloc);
            blob.init_from_ref(get_as_ref(ndx));

            size_t actual = blob.read(pos, buffer, max_size);
            pos = 0;
            size_copied += actual;
            max_size -= actual;
            buffer += actual;

            if (max_size) {
                ndx++;
                if (ndx >= size()) {
                    break;
                }
            }
        }
        return size_copied;
    }
    else {
        size_t size_to_copy = (pos > m_size) ? 0 : std::min(max_size, m_size - pos);
        std::copy(m_data + pos, m_data + pos + size_to_copy, buffer);
        return size_to_copy;
    }
}

ref_type ArrayBlob::replace(size_t begin, size_t end, const char* data, size_t data_size, bool add_zero_term)
{
    REALM_ASSERT_3(begin, <=, end);
    REALM_ASSERT_3(end, <=, m_size);
    REALM_ASSERT(data_size == 0 || data);

    if (get_context_flag()) {
        ArrayBlob lastNode(m_alloc);
        lastNode.init_from_ref(get_as_ref(size() - 1));
        lastNode.set_parent(this, size() - 1);

        size_t space_left = MAX_BLOB_NODE_SIZE - lastNode.size();
        size_t size_to_copy = std::min(space_left, data_size);
        lastNode.add(data, size_to_copy);
        data_size -= space_left;
        data += space_left;

        while (data_size) {
            size_to_copy = std::min(MAX_BLOB_NODE_SIZE, data_size);
            ArrayBlob new_blob(m_alloc);
            new_blob.create(); // Throws

            Array::add(new_blob.add(data, size_to_copy));
            data_size -= size_to_copy;
            data += size_to_copy;
        }
    }
    else {
        size_t remove_size = end - begin;
        size_t add_size = add_zero_term ? data_size + 1 : data_size;
        size_t new_size = m_size - remove_size + add_size;

        if (new_size > MAX_BLOB_NODE_SIZE) {
            REALM_ASSERT(begin == 0 && end == 0);   // For the time being, only support append

            Array new_root(m_alloc);
            new_root.create(type_HasRefs, true); // Throws
            new_root.add(get_ref());
            return reinterpret_cast<ArrayBlob*>(&new_root)->replace(begin, end, data, data_size, add_zero_term);
        }

        copy_on_write(); // Throws

        // Reallocate if needed - also updates header
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
    return get_ref();
}

#ifdef REALM_DEBUG  // LCOV_EXCL_START ignore debug functions

size_t ArrayBlob::blob_size() const noexcept
{
    if (get_context_flag()) {
        size_t total_size = 0;
        for (size_t i = 0; i < size(); ++i) {
            char* header = m_alloc.translate(Array::get_as_ref(i));
            total_size += Array::get_size_from_header(header);
        }
        return total_size;
    }
    else {
        return size();
    }
}

void ArrayBlob::verify() const
{
    if (get_context_flag()) {
        REALM_ASSERT(has_refs());
        for (size_t i = 0; i < size(); ++i) {
            ref_type blob_ref = Array::get_as_ref(i);
            REALM_ASSERT(blob_ref != 0);
            ArrayBlob blob(m_alloc);
            blob.init_from_ref(blob_ref);
            blob.verify();
        }
    }
    else {
        REALM_ASSERT(!has_refs());
    }
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
    out << blob_size() << " bytes"; // TODO: write content
    out << "</TD>" << std::endl;

    out << "</TR></TABLE>>];" << std::endl;

    if (title.size() != 0)
        out << "}" << std::endl;

    to_dot_parent_edge(out);
}

#endif // LCOV_EXCL_STOP ignore debug functions
