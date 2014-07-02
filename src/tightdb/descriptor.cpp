#include <tightdb/util/unique_ptr.hpp>
#include <tightdb/descriptor.hpp>
#include <tightdb/column_string.hpp>

using namespace tightdb;
using namespace tightdb::util;


DescriptorRef Descriptor::get_subdescriptor(size_t column_ndx)
{
    DescriptorRef subdesc;

    // Reuse the the descriptor accessor if it is already in the map
    if (Descriptor* d = get_subdesc_accessor(column_ndx)) {
        subdesc.reset(d);
        goto out;
    }

    // Create a new descriptor accessor
    {
        SubspecRef subspec_ref = m_spec->get_subtable_spec(column_ndx);
        UniquePtr<Spec> subspec(new Spec(subspec_ref)); // Throws
        subdesc.reset(new Descriptor); // Throws
        m_subdesc_map.push_back(subdesc_entry(column_ndx, subdesc.get())); // Throws
        subdesc->attach(m_root_table.get(), this, subspec.get());
        subspec.release();
    }

  out:
    return move(subdesc);
}


size_t Descriptor::get_num_unique_values(size_t column_ndx) const
{
    TIGHTDB_ASSERT(is_attached());
    ColumnType col_type = m_spec->get_column_type(column_ndx);
    if (col_type != col_type_StringEnum)
        return 0;
    ref_type ref = m_spec->get_enumkeys_ref(column_ndx);
    AdaptiveStringColumn col(ref, 0, 0, m_spec->get_alloc()); // Throws
    return col.size();
}


Descriptor::~Descriptor() TIGHTDB_NOEXCEPT
{
    if (!is_attached())
        return;
    if (m_parent) {
        delete m_spec;
        m_parent->remove_subdesc_entry(this);
        m_parent.reset();
    }
    else {
        _impl::TableFriend::clear_root_table_desc(*m_root_table);
    }
    m_root_table.reset();
}


void Descriptor::detach() TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(is_attached());
    detach_subdesc_accessors();
    if (m_parent) {
        delete m_spec;
        m_parent.reset();
    }
    m_root_table.reset();
}


void Descriptor::detach_subdesc_accessors() TIGHTDB_NOEXCEPT
{
    if (!m_subdesc_map.empty()) {
        typedef subdesc_map::const_iterator iter;
        iter end = m_subdesc_map.end();
        for (iter i = m_subdesc_map.begin(); i != end; ++i) {
            // Must hold a reliable reference count while detaching
            DescriptorRef desc(i->m_subdesc);
            desc->detach();
        }
        m_subdesc_map.clear();
    }
}


void Descriptor::remove_subdesc_entry(Descriptor* subdesc) const TIGHTDB_NOEXCEPT
{
    typedef subdesc_map::iterator iter;
    iter end = m_subdesc_map.end();
    for (iter i = m_subdesc_map.begin(); i != end; ++i) {
        if (i->m_subdesc == subdesc) {
            m_subdesc_map.erase(i);
            return;
        }
    }
    TIGHTDB_ASSERT(false);
}


size_t* Descriptor::record_subdesc_path(size_t* begin, size_t* end) const TIGHTDB_NOEXCEPT
{
    size_t* begin_2 = end;
    const Descriptor* desc = this;
    for (;;) {
        if (desc->is_root())
            return begin_2;
        if (TIGHTDB_UNLIKELY(begin_2 == begin))
            return 0; // Not enough space in path buffer
        const Descriptor* parent = desc->m_parent.get();
        size_t column_ndx = not_found;
        typedef subdesc_map::iterator iter;
        iter end = parent->m_subdesc_map.end();
        for (iter i = parent->m_subdesc_map.begin(); i != end; ++i) {
            if (i->m_subdesc == desc) {
                column_ndx = i->m_column_ndx;
                break;
            }
        }
        TIGHTDB_ASSERT(column_ndx != not_found);
        *--begin_2 = column_ndx;
        desc = parent;
    }
}


Descriptor* Descriptor::get_subdesc_accessor(size_t column_ndx) TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(is_attached());

    typedef subdesc_map::iterator iter;
    iter end = m_subdesc_map.end();
    for (iter i = m_subdesc_map.begin(); i != end; ++i) {
        if (i->m_column_ndx == column_ndx)
            return i->m_subdesc;
    }
    return 0;
}


void Descriptor::adj_insert_column(size_t col_ndx) TIGHTDB_NOEXCEPT
{
    // Adjust the column indexes of subdescriptor accessors at higher
    // column indexes.
    typedef subdesc_map::iterator iter;
    iter end = m_subdesc_map.end();
    for (iter i = m_subdesc_map.begin(); i != end; ++i) {
        if (i->m_column_ndx >= col_ndx)
            ++i->m_column_ndx;
    }
}


void Descriptor::adj_erase_column(size_t col_ndx) TIGHTDB_NOEXCEPT
{
    // If it exists, remove and detach the subdescriptor accessor
    // associated with the removed column. Also adjust the column
    // indexes of subdescriptor accessors at higher column indexes.
    typedef subdesc_map::iterator iter;
    iter end = m_subdesc_map.end();
    iter erase = end;
    for (iter i = m_subdesc_map.begin(); i != end; ++i) {
        if (i->m_column_ndx == col_ndx) {
            // Must hold a reliable reference count while detaching
            DescriptorRef desc(i->m_subdesc);
            desc->detach();
            erase = i;
        }
        else if (i->m_column_ndx > col_ndx) {
            --i->m_column_ndx; // Account for the removed column
        }
    }
    if (erase != end)
        m_subdesc_map.erase(erase);
}
