/*************************************************************************
 *
 * Copyright 2023 Realm Inc.
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

#ifndef REALM_ARRAY_FLEX_HPP
#define REALM_ARRAY_FLEX_HPP

#include <realm/array.hpp>

#include <cstdint>
#include <stddef.h>
#include <vector>


namespace realm {

struct WordTypeValue {};
struct WordTypeIndex {};

//
// Compress array in Flex format
// Decompress array in WTypeBits formats
//
class ArrayFlex {
public:
    // encoding/decoding
    void init_array(char* h, uint8_t flags, size_t v_width, size_t ndx_width, size_t v_size, size_t ndx_size) const;
    void copy_data(const Array&, const std::vector<int64_t>&, const std::vector<size_t>&) const;
    // getters/setters
    inline int64_t get(bf_iterator&, bf_iterator&, size_t, uint64_t) const;
    inline void get_chunk(bf_iterator&, bf_iterator&, size_t, uint64_t, int64_t[8]) const;
    inline void set_direct(bf_iterator&, bf_iterator&, size_t, int64_t) const;

    template <typename Cond>
    inline bool find_all(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

private:
    bool find_all_match(size_t, size_t, size_t, QueryStateBase*) const;

    template <typename Cond>
    inline bool find_linear(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

    template <typename CondVal, typename CondIndex>
    inline bool find_parallel(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

    template <typename LinearCond, typename ParallelCond1, typename ParallelCond2>
    inline bool do_find_all(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

    template <typename Cond>
    inline bool run_parallel_subscan(size_t, size_t, size_t) const;
};

inline int64_t ArrayFlex::get(bf_iterator& data_iterator, bf_iterator& ndx_iterator, size_t ndx, uint64_t mask) const
{
    ndx_iterator.move(ndx);
    data_iterator.move(*ndx_iterator);
    return sign_extend_field_by_mask(mask, *data_iterator);
}

inline void ArrayFlex::get_chunk(bf_iterator& data_it, bf_iterator& ndx_it, size_t ndx, uint64_t mask,
                                 int64_t res[8]) const
{
    auto sz = 8;
    std::memset(res, 0, sizeof(int64_t) * sz);
    auto supposed_end = ndx + sz;
    size_t i = ndx;
    size_t index = 0;
    for (; i < supposed_end; ++i) {
        res[index++] = get(data_it, ndx_it, i, mask);
    }
    for (; index < 8; ++index) {
        res[index++] = get(data_it, ndx_it, i++, mask);
    }
}

void ArrayFlex::set_direct(bf_iterator& data_it, bf_iterator& ndx_it, size_t ndx, int64_t value) const
{
    ndx_it.move(ndx);
    data_it.move(*ndx_it);
    data_it.set_value(value);
}

template <typename Cond>
inline bool ArrayFlex::find_all(const Array& arr, int64_t value, size_t start, size_t end, size_t baseindex,
                                QueryStateBase* state) const
{
    REALM_ASSERT_DEBUG(start <= arr.m_size && (end <= arr.m_size || end == size_t(-1)) && start <= end);
    Cond c;

    if (end == npos)
        end = arr.m_size;

    if (!(arr.m_size > start && start < end))
        return true;

    const auto lbound = arr.m_lbound;
    const auto ubound = arr.m_ubound;

    if (!c.can_match(value, lbound, ubound))
        return true;

    if (c.will_match(value, lbound, ubound)) {
        return find_all_match(start, end, baseindex, state);
    }

    REALM_ASSERT_DEBUG(arr.m_width != 0);

    if constexpr (std::is_same_v<Equal, Cond>) {
        return do_find_all<Equal, Equal, Equal>(arr, value, start, end, baseindex, state);
    }
    else if constexpr (std::is_same_v<NotEqual, Cond>) {
        return do_find_all<NotEqual, NotEqual, LessEqual>(arr, value, start, end, baseindex, state);
    }
    else if constexpr (std::is_same_v<Less, Cond>) {
        return do_find_all<Less, GreaterEqual, Less>(arr, value, start, end, baseindex, state);
    }
    else if constexpr (std::is_same_v<Greater, Cond>) {
        return do_find_all<Greater, Greater, GreaterEqual>(arr, value, start, end, baseindex, state);
    }
    return true;
}

template <typename LinearCond, typename ParallelCond1, typename ParallelCond2>
inline bool ArrayFlex::do_find_all(const Array& arr, int64_t value, size_t start, size_t end, size_t baseindex,
                                   QueryStateBase* state) const
{
    const auto v_width = arr.m_width;
    const auto v_range = arr.get_encoder().v_size();
    const auto ndx_range = end - start;
    if (!run_parallel_subscan<LinearCond>(v_width, v_range, ndx_range))
        return find_linear<LinearCond>(arr, value, start, end, baseindex, state);
    return find_parallel<ParallelCond1, ParallelCond2>(arr, value, start, end, baseindex, state);
}

template <typename Cond>
inline bool ArrayFlex::find_linear(const Array& arr, int64_t value, size_t start, size_t end, size_t baseindex,
                                   QueryStateBase* state) const
{
    const auto cmp = [](int64_t item, int64_t key) {
        if constexpr (std::is_same_v<Cond, Equal>)
            return item == key;
        if constexpr (std::is_same_v<Cond, NotEqual>) {
            return item != key;
        }
        if constexpr (std::is_same_v<Cond, Less>) {
            return item < key;
        }
        if constexpr (std::is_same_v<Cond, Greater>) {
            return item > key;
        }
        REALM_UNREACHABLE();
    };

    const auto& encoder = arr.get_encoder();
    auto& ndx_it = encoder.ndx_iterator();
    auto& data_it = encoder.data_iterator();
    ndx_it.move(start);
    while (start < end) {
        data_it.move(*ndx_it);
        const auto sv = sign_extend_field_by_mask(encoder.width_mask(), *data_it);
        if (cmp(sv, value) && !state->match(start + baseindex))
            return false;
        ++start;
        ++ndx_it;
    }
    return true;
}

template <typename Cond, typename Type = WordTypeValue>
inline uint64_t vector_compare(uint64_t MSBs, uint64_t a, uint64_t b)
{
    if constexpr (std::is_same_v<Cond, Equal>)
        return find_all_fields_EQ(MSBs, a, b);
    if constexpr (std::is_same_v<Cond, NotEqual>)
        return find_all_fields_NE(MSBs, a, b);

    if constexpr (std::is_same_v<Cond, Greater>) {
        if (std::is_same_v<Type, WordTypeValue>)
            return find_all_fields_signed_GT(MSBs, a, b);
        if (std::is_same_v<Type, WordTypeIndex>)
            return find_all_fields_unsigned_GT(MSBs, a, b);
        REALM_UNREACHABLE();
    }
    if constexpr (std::is_same_v<Cond, GreaterEqual>) {
        if constexpr (std::is_same_v<Type, WordTypeValue>)
            return find_all_fields_signed_GE(MSBs, a, b);
        if constexpr (std::is_same_v<Type, WordTypeIndex>)
            return find_all_fields_unsigned_GE(MSBs, a, b);
        REALM_UNREACHABLE();
    }
    if constexpr (std::is_same_v<Cond, Less>) {
        if constexpr (std::is_same_v<Type, WordTypeValue>)
            return find_all_fields_signed_LT(MSBs, a, b);
        if constexpr (std::is_same_v<Type, WordTypeIndex>)
            return find_all_fields_unsigned_LT(MSBs, a, b);
        REALM_UNREACHABLE();
    }
    if constexpr (std::is_same_v<Cond, LessEqual>) {
        if constexpr (std::is_same_v<Type, WordTypeValue>)
            return find_all_fields_signed_LT(MSBs, a, b);
        if constexpr (std::is_same_v<Type, WordTypeIndex>)
            return find_all_fields_unsigned_LE(MSBs, a, b);
        REALM_UNREACHABLE();
    }
}

template <typename CondVal, typename CondIndex>
inline bool ArrayFlex::find_parallel(const Array& arr, int64_t value, size_t start, size_t end, size_t baseindex,
                                     QueryStateBase* state) const
{
    const auto& encoder = arr.m_encoder;
    const auto v_width = encoder.width();
    const auto v_size = encoder.v_size();
    const auto ndx_width = encoder.ndx_width();
    const auto offset = v_size * v_width;
    uint64_t* data = (uint64_t*)arr.m_data;

    auto MSBs = encoder.msb();
    auto search_vector = populate(v_width, value);
    auto v_start = parallel_subword_find(vector_compare<CondVal>, data, 0, v_width, MSBs, search_vector, 0, v_size);
    if (v_start == v_size)
        return true;

    MSBs = encoder.ndx_msb();
    search_vector = populate(ndx_width, v_start);
    while (start < end) {
        start = parallel_subword_find(vector_compare<CondIndex, WordTypeIndex>, data, offset, ndx_width, MSBs,
                                      search_vector, start, end);
        if (start < end)
            if (!state->match(start + baseindex))
                return false;

        ++start;
    }
    return true;
}

template <typename Cond>
inline bool ArrayFlex::run_parallel_subscan(size_t v_width, size_t v_range, size_t ndx_range) const
{
    if (ndx_range <= 32)
        return false;
    // the threshold for v_width is empirical, some intuition for this is probably that we need to consider 2 parallel
    // scans, one for finding the matching value and one for the indices (max bit-width 8, becasue max array size is
    // 256). When we scan the values in parallel, we go through them all, we can't follow the hint given [start, end],
    // thus a full scan of values makes sense only for certain widths that are not too big, in order to compare in
    // parallel as many values as we can in one go.
    return v_width <= 20 && v_range >= 20;
}

} // namespace realm
#endif // REALM_ARRAY_COMPRESS_HPP
