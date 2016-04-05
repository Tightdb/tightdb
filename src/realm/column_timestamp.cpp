/*************************************************************************
 *
 * REALM CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2015] Realm Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of Realm Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to Realm Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from Realm Incorporated.
 *
 **************************************************************************/

#include <realm/column_timestamp.hpp>
#include <realm/index_string.hpp>

namespace realm {


TimestampColumn::TimestampColumn(Allocator& alloc, ref_type ref, bool nullable):
    m_nullable(nullable)
{
    char* header = alloc.translate(ref);
    MemRef mem(header, ref);

    Array* root = new Array(alloc); // Throws
    root->init_from_mem(mem);
    m_array.reset(root);

    ref_type seconds = m_array->get_as_ref(0);
    ref_type nano = m_array->get_as_ref(1);

    if (m_nullable) {
        m_nullable_seconds.init_from_ref(alloc, seconds);
        m_nullable_seconds.set_parent(root, 0);
    }
    else {
        m_nonnullable_seconds.init_from_ref(alloc, seconds);
        m_nonnullable_seconds.set_parent(root, 0);
    }
    m_nanoseconds.init_from_ref(alloc, nano);
    m_nanoseconds.set_parent(root, 1);
}


TimestampColumn::~TimestampColumn() noexcept
{

}


template<class BT> class TimestampColumn::CreateHandler: public ColumnBase::CreateHandler {
public:
    CreateHandler(typename BT::value_type value, Allocator& alloc):
        m_value(value), m_alloc(alloc) {}

    ref_type create_leaf(size_t size) override
    {
        MemRef mem = BT::create_leaf(Array::type_Normal, size, m_value, m_alloc); // Throws
        return mem.m_ref;
    }
private:
    const typename BT::value_type m_value;
    Allocator& m_alloc;
};

ref_type TimestampColumn::create(Allocator& alloc, size_t size, bool nullable)
{
    Array top(alloc);
    top.create(Array::type_HasRefs, false /* context_flag */, 2);

    ref_type seconds;

    if (nullable) {
        CreateHandler<BpTree<util::Optional<int64_t>>> create_handler{null{}, alloc};
        seconds = ColumnBase::create(alloc, size, create_handler);
    }
    else {
        CreateHandler<BpTree<int64_t>> create_handler{0, alloc};
        seconds = ColumnBase::create(alloc, size, create_handler);
    }

    CreateHandler<BpTree<int64_t>> nano_create_handler{0, alloc};
    ref_type nano = ColumnBase::create(alloc, size, nano_create_handler);

    top.set_as_ref(0, seconds);
    top.set_as_ref(1, nano);

    ref_type top_ref = top.get_ref();
    return top_ref;
}


/// Get the number of entries in this column. This operation is relatively
/// slow.
size_t TimestampColumn::size() const noexcept
{
    // FIXME: Consider debug asserts on the columns having the same size
    if (m_nullable)
        return m_nullable_seconds.size();
    return m_nonnullable_seconds.size();
}

/// Whether or not this column is nullable.
bool TimestampColumn::is_nullable() const noexcept
{
    return m_nullable;
}

/// Whether or not the value at \a row_ndx is NULL. If the column is not
/// nullable, always returns false.
bool TimestampColumn::is_null(size_t row_ndx) const noexcept
{
    if (!m_nullable)
        return false;
    return m_nullable_seconds.is_null(row_ndx);
}

/// Sets the value at \a row_ndx to be NULL.
/// \throw LogicError Thrown if this column is not nullable.
void TimestampColumn::set_null(size_t row_ndx)
{
    REALM_ASSERT(m_nullable);
    m_nullable_seconds.set_null(row_ndx);
    if (has_search_index()) {
        m_search_index->set(row_ndx, null{});
    }
}

void TimestampColumn::insert_rows(size_t row_ndx, size_t num_rows_to_insert, size_t prior_num_rows,
    bool nullable)
{
    static_cast<void>(prior_num_rows);
    if (row_ndx == size())
        row_ndx = npos;
    if (nullable)
        m_nullable_seconds.insert(row_ndx, null{}, num_rows_to_insert);
    else
        m_nonnullable_seconds.insert(row_ndx, 0, num_rows_to_insert);
    m_nanoseconds.insert(row_ndx, 0, num_rows_to_insert);

    if (has_search_index()) {
        size_t size = this->size();
        bool is_append = row_ndx == npos || row_ndx == size;
        if (is_append)
            row_ndx = size;
        if (nullable) {
            m_search_index->insert(row_ndx, null{}, num_rows_to_insert, is_append);
        }
        else {
            m_search_index->insert(row_ndx, Timestamp{0, 0}, num_rows_to_insert, is_append);
        }
    }
}

void TimestampColumn::erase_rows(size_t row_ndx, size_t num_rows_to_erase, size_t /*prior_num_rows*/,
    bool broken_reciprocal_backlinks)
{
    static_cast<void>(broken_reciprocal_backlinks);
    bool is_last = (row_ndx + num_rows_to_erase) == size();
    for (size_t i = 0; i < num_rows_to_erase; ++i) {
        size_t ndx = row_ndx + num_rows_to_erase - i - 1;
        if (m_nullable)
            m_nullable_seconds.erase(ndx, is_last);
        else
            m_nonnullable_seconds.erase(ndx, is_last);
        m_nanoseconds.erase(ndx, is_last);
        
        if (has_search_index()) {
            m_search_index->erase<StringData>(ndx, is_last);
        }
    }
}

void TimestampColumn::move_last_row_over(size_t row_ndx, size_t prior_num_rows,
    bool broken_reciprocal_backlinks)
{
    static_cast<void>(broken_reciprocal_backlinks);
    
    size_t last_row_ndx = prior_num_rows - 1;
    
    if (has_search_index()) {
        // remove the value to be overwritten from index
        bool is_last = true; // This tells StringIndex::erase() to not adjust subsequent indexes
        m_search_index->erase<StringData>(row_ndx, is_last); // Throws

        // update index to point to new location
        if (row_ndx != last_row_ndx) {
            auto moved_value = get(last_row_ndx);
            m_search_index->update_ref(moved_value, last_row_ndx, row_ndx); // Throws
        }
    }

    if (m_nullable)
        m_nullable_seconds.move_last_over(row_ndx, prior_num_rows);
    else
        m_nonnullable_seconds.move_last_over(row_ndx, prior_num_rows);
    m_nanoseconds.move_last_over(row_ndx, prior_num_rows);
}

void TimestampColumn::clear(size_t num_rows, bool broken_reciprocal_backlinks)
{
    REALM_ASSERT_EX(num_rows == size(), num_rows, size());
    static_cast<void>(broken_reciprocal_backlinks);
    if (m_nullable)
        m_nullable_seconds.clear();
    else
        m_nonnullable_seconds.clear();
    m_nanoseconds.clear();
    if (has_search_index()) {
        m_search_index->clear();
    }
}

void TimestampColumn::swap_rows(size_t row_ndx_1, size_t row_ndx_2)
{
    if (has_search_index()) {
        auto value_1 = get(row_ndx_1);
        auto value_2 = get(row_ndx_2);
        size_t size = this->size();
        bool row_ndx_1_is_last = row_ndx_1 == size - 1;
        bool row_ndx_2_is_last = row_ndx_2 == size - 1;
        m_search_index->erase<StringData>(row_ndx_1, row_ndx_1_is_last);
        m_search_index->insert(row_ndx_1, value_2, 1, row_ndx_1_is_last);
        m_search_index->erase<StringData>(row_ndx_2, row_ndx_2_is_last);
        m_search_index->insert(row_ndx_2, value_1, 1, row_ndx_2_is_last);
    }

    auto tmp1 = get(row_ndx_1).m_seconds;
    if (m_nullable) {
        m_nullable_seconds.set(row_ndx_1, m_nullable_seconds.get(row_ndx_2));
        m_nullable_seconds.set(row_ndx_2, tmp1);
    }
    else {
        m_nonnullable_seconds.set(row_ndx_1, m_nonnullable_seconds.get(row_ndx_2));
        m_nonnullable_seconds.set(row_ndx_2, tmp1);
    }
    auto tmp2 = m_nanoseconds.get(row_ndx_1);
    m_nanoseconds.set(row_ndx_1, m_nanoseconds.get(row_ndx_2));
    m_nanoseconds.set(row_ndx_2, tmp2);
}

void TimestampColumn::destroy() noexcept
{
    if (m_nullable)
        m_nullable_seconds.destroy();
    else
        m_nonnullable_seconds.destroy();
    m_nanoseconds.destroy();
    if (m_array)
        m_array->destroy();
}

StringData TimestampColumn::get_index_data(size_t ndx, StringIndex::StringConversionBuffer& buffer) const noexcept
{
    return GetIndexData<Timestamp>::get_index_data(get(ndx), buffer);
}

void TimestampColumn::populate_search_index()
{
    REALM_ASSERT(has_search_index());
    // Populate the index
    size_t num_rows = size();
    for (size_t row_ndx = 0; row_ndx != num_rows; ++row_ndx) {
        bool is_append = true;
        auto value = get(row_ndx);
        m_search_index->insert(row_ndx, value, 1, is_append); // Throws
    }
}

StringIndex* TimestampColumn::create_search_index()
{
    REALM_ASSERT(!has_search_index());
    m_search_index.reset(new StringIndex(this, get_alloc())); // Throws
    populate_search_index();
    return m_search_index.get();
}

void TimestampColumn::destroy_search_index() noexcept
{
    m_search_index.reset();
}

void TimestampColumn::set_search_index_ref(ref_type ref, ArrayParent* parent,
        size_t ndx_in_parent, bool allow_duplicate_values)
{
    REALM_ASSERT(!m_search_index);
    m_search_index.reset(new StringIndex(ref, parent, ndx_in_parent, this,
                !allow_duplicate_values, get_alloc())); // Throws
}



MemRef TimestampColumn::clone_deep(Allocator& /*alloc*/) const
{
    // FIXME: Dummy implementation
    return MemRef();
}


ref_type TimestampColumn::write(size_t /*slice_offset*/, size_t /*slice_size*/, size_t /*table_size*/,
    _impl::OutputStream&) const
{
    // FIXME: Dummy implementation
    return 0;
}

void TimestampColumn::set_ndx_in_parent(size_t ndx) noexcept
{
    m_array->set_ndx_in_parent(ndx);
    if (has_search_index()) {
        m_search_index->set_ndx_in_parent(ndx + 1);
    }
}

void TimestampColumn::update_from_parent(size_t old_baseline) noexcept
{
    ColumnBaseSimple::update_from_parent(old_baseline);
    if (has_search_index()) {
        m_search_index->update_from_parent(old_baseline);
    }
}

void TimestampColumn::refresh_accessor_tree(size_t new_col_ndx, const Spec& spec)
{
    // FIXME: Dummy implementation
    
    if (has_search_index()) {
        m_search_index->refresh_accessor_tree(new_col_ndx, spec);
    }
}

#ifdef REALM_DEBUG

void TimestampColumn::verify() const
{
    // FIXME: Dummy implementation
}

void TimestampColumn::to_dot(std::ostream&, StringData /*title*/) const
{
    // FIXME: Dummy implementation
}

void TimestampColumn::do_dump_node_structure(std::ostream&, int /*level*/) const
{
    // FIXME: Dummy implementation
}

void TimestampColumn::leaf_to_dot(MemRef, ArrayParent*, size_t /*ndx_in_parent*/, std::ostream&) const
{
    // FIXME: Dummy implementation
}

#endif

void TimestampColumn::add(const Timestamp& ts)
{
    uint32_t nanoseconds = ts.is_null() ? 0 : ts.m_nanoseconds;
    if (m_nullable) {
        util::Optional<int64_t> seconds = ts.is_null() ? util::none : util::some<int64_t>(ts.m_seconds);
        m_nullable_seconds.insert(npos, seconds);
    }
    else {
        REALM_ASSERT(!ts.is_null());
        m_nonnullable_seconds.insert(npos, ts.m_seconds);
    }
    m_nanoseconds.insert(npos, nanoseconds);

    if (has_search_index()) {
        size_t ndx = size() - 1; // Slow
        m_search_index->insert(ndx, ts, 1, true);
    }
}

Timestamp TimestampColumn::get(size_t row_ndx) const noexcept
{
    int64_t seconds;
    if (m_nullable) {
        util::Optional<int64_t> maybe_seconds = m_nullable_seconds.get(row_ndx);
        if (maybe_seconds)
            seconds = *maybe_seconds;
        else
            return null{};
    }
    else {
        seconds = m_nonnullable_seconds.get(row_ndx);
    }
    uint32_t ns = static_cast<uint32_t>(m_nanoseconds.get(row_ndx));
    return Timestamp{seconds, ns};
}

void TimestampColumn::set(size_t row_ndx, const Timestamp& ts)
{
    bool is_null = ts.is_null();
    util::Optional<int64_t> seconds = is_null ? util::none : util::make_optional(ts.m_seconds);
    int32_t nanoseconds = is_null ? 0 : ts.m_nanoseconds;
    if (m_nullable) {
        m_nullable_seconds.set(row_ndx, seconds);
    }
    else {
        REALM_ASSERT(!is_null);
        m_nonnullable_seconds.set(row_ndx, *seconds);
    }
    m_nanoseconds.set(row_ndx, nanoseconds);

    if (has_search_index()) {
        m_search_index->set(row_ndx, ts);
    }
}

bool TimestampColumn::compare(const TimestampColumn& c) const noexcept
{
    size_t n = size();
    if (c.size() != n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        bool left_is_null = is_null(i);
        bool right_is_null = c.is_null(i);
        if (left_is_null != right_is_null) {
            return false;
        }
        if (!left_is_null) {
            if (get(i) != c.get(i))
                return false;
        }
    }
    return true;
}


}
