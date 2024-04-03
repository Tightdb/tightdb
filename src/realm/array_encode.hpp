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

#ifndef REALM_ARRAY_ENCODE_HPP
#define REALM_ARRAY_ENCODE_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <realm/query_conditions.hpp>
#include <realm/array_direct.hpp>

namespace realm {

class Array;
class QueryStateBase;
class ArrayEncode {
public:
    // commit => encode, COW/insert => decode
    bool encode(const Array&, Array&) const;
    bool decode(Array&) const;

    void init(const char* h);

    // init from mem B
    inline size_t size() const;
    inline size_t width() const;
    inline size_t ndx_size() const;
    inline size_t ndx_width() const;
    inline NodeHeader::Encoding get_encoding() const;
    inline size_t v_size() const;
    inline uint64_t width_mask() const;
    inline uint64_t ndx_mask() const;
    inline uint64_t msb() const;
    inline uint64_t ndx_msb() const;
    inline size_t field_count() const;
    inline size_t ndx_field_count() const;
    inline size_t bit_count_per_iteration() const;
    inline size_t ndx_bit_count_per_iteration() const;

    // get/set
    inline int64_t get(size_t) const;
    inline void get_chunk(size_t ndx, int64_t res[8]) const;
    inline void set_direct(size_t, int64_t) const;

    // query interface
    template <typename Cond>
    inline bool find_all(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

private:
    // Same idea we have for Array, we want to avoid to constantly checking whether we
    // have compressed in packed or flex, and jump straight to the right implementation,
    // avoiding branch mis-predictions, which made some queries run ~6/7x slower.

    using Getter = int64_t (ArrayEncode::*)(size_t) const;
    using ChunkGetterChunk = void (ArrayEncode::*)(size_t, int64_t[8]) const;
    using DirectSetter = void (ArrayEncode::*)(size_t, int64_t) const;
    using Finder = bool (ArrayEncode::*)(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;
    using FinderTable = std::array<Finder, cond_VTABLE_FINDER_COUNT>;

    struct VTable {
        Getter m_getter{nullptr};
        ChunkGetterChunk m_chunk_getter{nullptr};
        DirectSetter m_direct_setter{nullptr};
        FinderTable m_finder;
    };
    struct VTableForPacked;
    struct VTableForFlex;
    const VTable* m_vtable = nullptr;

    // getting and setting interface specifically for encoding formats
    int64_t get_packed(size_t) const;
    int64_t get_flex(size_t) const;
    void get_chunk_packed(size_t, int64_t[8]) const;
    void get_chunk_flex(size_t, int64_t[8]) const;
    void set_direct_packed(size_t, int64_t) const;
    void set_direct_flex(size_t, int64_t) const;
    // query interface
    template <typename Cond>
    bool find_all_packed(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;
    template <typename Cond>
    bool find_all_flex(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

    // internal impl
    inline void set(char* data, size_t w, size_t ndx, int64_t v) const;
    size_t flex_encoded_array_size(const std::vector<int64_t>&, const std::vector<size_t>&, size_t&, size_t&) const;
    size_t packed_encoded_array_size(std::vector<int64_t>&, size_t, size_t&) const;
    void encode_values(const Array&, std::vector<int64_t>&, std::vector<size_t>&) const;
    inline bool is_packed() const;
    inline bool is_flex() const;

    // for testing
    bool always_encode(const Array&, Array&, Node::Encoding) const;

private:
    using Encoding = NodeHeader::Encoding;
    Encoding m_encoding{NodeHeader::Encoding::WTypBits};
    size_t m_v_width = 0, m_v_size = 0, m_ndx_width = 0, m_ndx_size = 0;
    uint64_t m_MSBs = 0, m_ndx_MSBs = 0, m_v_mask = 0, m_ndx_mask = 0;
    mutable bf_iterator m_data_iterator;
    mutable bf_iterator m_ndx_iterator;
};

inline bool ArrayEncode::is_packed() const
{
    return m_encoding == NodeHeader::Encoding::Packed;
}

inline bool ArrayEncode::is_flex() const
{
    return m_encoding == NodeHeader::Encoding::Flex;
}

inline size_t ArrayEncode::size() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_encoding == Encoding::Packed ? v_size() : ndx_size();
}

inline size_t ArrayEncode::v_size() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_v_size;
}

inline size_t ArrayEncode::ndx_size() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_ndx_size;
}

inline size_t ArrayEncode::width() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_v_width;
}

inline size_t ArrayEncode::ndx_width() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_ndx_width;
}

inline NodeHeader::Encoding ArrayEncode::get_encoding() const
{
    return m_encoding;
}

inline uint64_t ArrayEncode::width_mask() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_v_mask;
}

inline uint64_t ArrayEncode::ndx_mask() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_ndx_mask;
}

inline uint64_t ArrayEncode::msb() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_MSBs;
}

inline uint64_t ArrayEncode::ndx_msb() const
{
    using Encoding = NodeHeader::Encoding;
    REALM_ASSERT_DEBUG(m_encoding == Encoding::Packed || m_encoding == Encoding::Flex);
    return m_ndx_MSBs;
}

inline void ArrayEncode::set(char* data, size_t w, size_t ndx, int64_t v) const
{
    if (w == 0)
        realm::set_direct<0>(data, ndx, v);
    else if (w == 1)
        realm::set_direct<1>(data, ndx, v);
    else if (w == 2)
        realm::set_direct<2>(data, ndx, v);
    else if (w == 4)
        realm::set_direct<4>(data, ndx, v);
    else if (w == 8)
        realm::set_direct<8>(data, ndx, v);
    else if (w == 16)
        realm::set_direct<16>(data, ndx, v);
    else if (w == 32)
        realm::set_direct<32>(data, ndx, v);
    else if (w == 64)
        realm::set_direct<64>(data, ndx, v);
    else
        REALM_UNREACHABLE();
}

inline int64_t ArrayEncode::get(size_t ndx) const
{
    REALM_ASSERT_DEBUG(ndx < size());
    REALM_ASSERT_DEBUG(is_packed() || is_flex());
    REALM_ASSERT_DEBUG(m_vtable->m_getter);
    return (this->*(m_vtable->m_getter))(ndx);
}

inline void ArrayEncode::get_chunk(size_t ndx, int64_t res[8]) const
{
    REALM_ASSERT_DEBUG(ndx < size());
    REALM_ASSERT_DEBUG(is_packed() || is_flex());
    REALM_ASSERT_DEBUG(m_vtable->m_chunk_getter);
    (this->*(m_vtable->m_chunk_getter))(ndx, res);
}

inline void ArrayEncode::set_direct(size_t ndx, int64_t value) const
{
    REALM_ASSERT_DEBUG(ndx < size());
    REALM_ASSERT_DEBUG(is_packed() || is_flex());
    REALM_ASSERT_DEBUG(m_vtable->m_direct_setter);
    (this->*(m_vtable->m_direct_setter))(ndx, value);
}

template <typename Cond>
inline bool ArrayEncode::find_all(const Array& arr, int64_t value, size_t start, size_t end, size_t baseindex,
                                  QueryStateBase* state) const
{
    REALM_ASSERT_DEBUG(is_packed() || is_flex());
    if constexpr (std::is_same_v<Cond, Equal>) {
        return (this->*(m_vtable->m_finder[cond_Equal]))(arr, value, start, end, baseindex, state);
    }
    if constexpr (std::is_same_v<Cond, NotEqual>) {
        return (this->*(m_vtable->m_finder[cond_NotEqual]))(arr, value, start, end, baseindex, state);
    }
    if constexpr (std::is_same_v<Cond, Less>) {
        return (this->*(m_vtable->m_finder[cond_Less]))(arr, value, start, end, baseindex, state);
    }
    if constexpr (std::is_same_v<Cond, Greater>) {
        return (this->*(m_vtable->m_finder[cond_Greater]))(arr, value, start, end, baseindex, state);
    }
    REALM_UNREACHABLE();
}

} // namespace realm
#endif // REALM_ARRAY_ENCODE_HPP
