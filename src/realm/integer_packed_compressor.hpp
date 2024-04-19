/*************************************************************************
 *
 * Copyright 2024 Realm Inc.
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

#ifndef PACKED_COMPRESSOR_HPP
#define PACKED_COMPRESSOR_HPP

#include <realm/array.hpp>
#include <realm/array_direct.hpp>

#include <cstdint>
#include <stddef.h>

namespace realm {

//
// Compress array in Packed format
// Decompress array in WTypeBits formats
//
class PackedCompressor {
public:
    // encoding/decoding
    void init_array(char*, uint8_t, size_t, size_t) const;
    void copy_data(const Array&, Array&) const;
    // get or set
    inline int64_t get(const IntegerCompressor&, size_t) const;
    inline std::vector<int64_t> get_all(const IntegerCompressor& c, size_t b, size_t e) const;
    inline void get_chunk(const IntegerCompressor&, size_t, int64_t res[8]) const;
    inline void set_direct(const IntegerCompressor&, size_t, int64_t) const;

    template <typename Cond>
    inline bool find_all(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

private:
    bool find_all_match(size_t start, size_t end, size_t baseindex, QueryStateBase* state) const;

    template <typename Cond>
    inline bool find_parallel(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

    template <typename Cond>
    inline bool find_linear(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

    template <typename Cond>
    inline bool run_parallel_scan(size_t, size_t) const;
};

inline int64_t PackedCompressor::get(const IntegerCompressor& c, size_t ndx) const
{
    bf_iterator it{c.data(), 0, c.v_width(), c.v_width(), ndx};
    return sign_extend_field_by_mask(c.v_mask(), *it);
}

inline std::vector<int64_t> PackedCompressor::get_all(const IntegerCompressor& c, size_t b, size_t e) const
{
    const auto range = (e-b);
    const auto v_w = c.v_width();
    const auto data = c.data();
    const auto sign_mask = c.v_mask();
    const auto starting_bit = b * v_w;
    const auto total_bits = starting_bit + (v_w * range);
    const auto mask = c.v_bit_mask();
    const auto bit_per_it = num_bits_for_width(v_w);
    
    std::vector<int64_t> res;
    res.reserve(range); //this is very important, x4 faster pre-allocating the array
    unaligned_word_iter unaligned_data_iterator(data, starting_bit);
    auto cnt_bits = starting_bit;
    while ((cnt_bits + bit_per_it) < total_bits) {
        auto word = unaligned_data_iterator.get_with_unsafe_prefetch(bit_per_it);
        const auto next_chunk = cnt_bits + bit_per_it;
        while(cnt_bits < next_chunk && cnt_bits < total_bits) {
            res.push_back(sign_extend_field_by_mask(sign_mask, word & mask));
            cnt_bits+=v_w;
            word>>=v_w;
        }
        unaligned_data_iterator.bump(bit_per_it);
    }
    if (cnt_bits < total_bits) {
        auto word = unaligned_data_iterator.get_with_unsafe_prefetch(static_cast<unsigned>(total_bits - cnt_bits));
        while (cnt_bits < total_bits) {
            res.push_back(sign_extend_field_by_mask(sign_mask, word & mask));
            cnt_bits += v_w;
            word >>= v_w;
        }
    }
    return res;
}

inline void PackedCompressor::set_direct(const IntegerCompressor& c, size_t ndx, int64_t value) const
{
    bf_iterator it{c.data(), 0, c.v_width(), c.v_width(), ndx};
    it.set_value(value);
}

inline void PackedCompressor::get_chunk(const IntegerCompressor& c, size_t ndx, int64_t res[8]) const
{
    auto sz = 8;
    std::memset(res, 0, sizeof(int64_t) * sz);
    auto supposed_end = ndx + sz;
    size_t i = ndx;
    size_t index = 0;
    // this can be done better, in one go, retrieve both!!!
    for (; i < supposed_end; ++i) {
        res[index++] = get(c, i);
    }
    for (; index < 8; ++index) {
        res[index++] = get(c, i++);
    }
}


template <typename Cond>
inline bool PackedCompressor::find_all(const Array& arr, int64_t value, size_t start, size_t end, size_t baseindex,
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

    if (!run_parallel_scan<Cond>(arr.m_width, end - start))
        return find_linear<Cond>(arr, value, start, end, baseindex, state);

    return find_parallel<Cond>(arr, value, start, end, baseindex, state);
}

template <typename Cond>
inline bool PackedCompressor::find_parallel(const Array& arr, int64_t value, size_t start, size_t end,
                                            size_t baseindex, QueryStateBase* state) const
{
    //
    // Main idea around find parallel (applicable to flex arrays too).
    // Try to find the starting point where the condition can be met, comparing as many values as a single 64bit can
    // contain in parallel. Once we have found the starting point, keep matching values as much as we can between
    // start and end.
    //
    // EG: we store the value 6, with width 4bits (0110), 6 is 4 bits because, 110 (6) + sign bit 0.
    // Inside 64bits we can fit max 16 times 6. If we go from index 0 to 15 throughout the same 64 bits, we need to
    // apply a mask and a shift bits every time, then compare the values.
    // This is not the cheapest thing to do. Instead we can compare all values contained within 64 bits in one go and
    // see if there is a match with what we are looking for. Reducing the number of comparison by ~logk(N) where K is
    // the width of each single value within a 64 bit word and N is the total number of values stored in the array.

    // apparently the compiler is not able to deduce the type of a global function after moving stuff in the header
    // (no so sure why)
    static auto vector_compare = [](uint64_t MSBs, uint64_t a, uint64_t b) {
        if constexpr (std::is_same_v<Cond, Equal>)
            return find_all_fields_EQ(MSBs, a, b);
        if constexpr (std::is_same_v<Cond, NotEqual>)
            return find_all_fields_NE(MSBs, a, b);
        if constexpr (std::is_same_v<Cond, Greater>)
            return find_all_fields_signed_GT(MSBs, a, b);
        if constexpr (std::is_same_v<Cond, Less>)
            return find_all_fields_signed_LT(MSBs, a, b);
        REALM_UNREACHABLE();
    };

    const auto data = (const uint64_t*)arr.m_data;
    const auto width = arr.m_width;
    const auto MSBs = arr.integer_compressor().msb();
    const auto search_vector = populate(arr.m_width, value);
    while (start < end) {
        start = parallel_subword_find(vector_compare, data, 0, width, MSBs, search_vector, start, end);
        if (start < end)
            if (!state->match(start + baseindex))
                return false;
        ++start;
    }
    return true;
}

template <typename Cond>
inline bool PackedCompressor::find_linear(const Array& arr, int64_t value, size_t start, size_t end, size_t baseindex,
                                          QueryStateBase* state) const
{
    auto compare = [](int64_t a, int64_t b) {
        if constexpr (std::is_same_v<Cond, Equal>)
            return a == b;
        if constexpr (std::is_same_v<Cond, NotEqual>)
            return a != b;
        if constexpr (std::is_same_v<Cond, Greater>)
            return a > b;
        if constexpr (std::is_same_v<Cond, Less>)
            return a < b;
    };
    const auto& c = arr.integer_compressor();
    bf_iterator it{c.data(), 0, c.v_width(), c.v_width(), start};
    for (; start < end; ++start) {
        it.move(start);
        const auto sv = sign_extend_field_by_mask(c.v_mask(), *it);
        if (compare(sv, value) && !state->match(start + baseindex))
            return false;
    }
    return true;
}

template <typename Cond>
inline bool PackedCompressor::run_parallel_scan(size_t width, size_t range) const
{
    if constexpr (std::is_same_v<Cond, NotEqual>) {
        // we seem to be particularly slow doing parallel scan in packed for NotEqual.
        // we are much better with a linear scan. TODO: investigate this.
        return false;
    }
    if constexpr (std::is_same_v<Cond, Equal>) {
        return width < 32 && range >= 20;
    }
    // > and < need a different heuristic
    return width <= 20 && range >= 20;
}

} // namespace realm

#endif // PACKED_COMPRESSOR_HPP
