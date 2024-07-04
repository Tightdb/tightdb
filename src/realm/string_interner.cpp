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

#include <realm/string_interner.hpp>
#include <realm/string_compressor.hpp>
#include <realm/string_data.hpp>
#include <string_view>

namespace realm {

// Fast mapping of strings (or rather hash of strings) to string IDs.
//
// We use a tree where:
// * All interior nodes are radix nodes with a fan-out of 256.
// * Leaf nodes with up to 16 entries are just lists, searched linearly
// * Leaf nodes with more than 16 entries and less than 1K are hash tables.
//   Hash tables use linear search starting from the entry found by hashing.
//
constexpr static size_t linear_search_limit = 16;
constexpr static size_t hash_node_min_size = 32;
constexpr static size_t hash_node_max_size = 1024;
constexpr static size_t radix_node_consumes_bits = 8;
constexpr static size_t radix_node_size = 1ULL << radix_node_consumes_bits;

// helpers
struct HashMapIter {
    Array& m_array;
    uint32_t hash_filter;
    uint16_t index;
    uint16_t left_to_search;
    uint8_t hash_size;
    HashMapIter(Array& array, uint32_t hash, uint8_t hash_size)
        : m_array(array)
        , hash_filter(hash)
        , hash_size(hash_size)
    {
        set_index(0);
    }
    HashMapIter(Array& dummy)
        : m_array(dummy)
    {
        left_to_search = 0;
    }
    inline uint32_t get()
    {
        return (uint32_t)(m_array.get(index) >> hash_size);
    }
    inline bool empty()
    {
        auto element = m_array.get(index);
        return (element >> hash_size) == 0;
    }
    inline void set(uint64_t element)
    {
        m_array.set(index, element);
    }
    inline bool matches()
    {
        auto mask = 0xFFFFFFFFUL >> (32 - hash_size);
        auto element = m_array.get(index);
        return ((element & mask) == hash_filter) && (element >> hash_size);
    }
    inline bool is_valid()
    {
        return left_to_search != 0;
    }
    inline void set_index(size_t i, size_t search_limit = linear_search_limit)
    {
        index = (uint16_t)i;
        left_to_search = (uint16_t)std::min(m_array.size(), (size_t)search_limit);
    }
    void operator++()
    {
        if (is_valid()) {
            left_to_search--;
            index++;
            if (index == m_array.size()) {
                index = 0;
            }
        }
    }
};

// Attempt to build a hash leaf from a smaller hash leaf or a non-hash leaf.
static bool rehash(Array& from, Array& to, uint8_t hash_size)
{
    REALM_ASSERT_DEBUG(from.size() * 2 <= to.size());

    for (size_t i = 0; i < from.size(); ++i) {
        auto entry = (size_t)from.get(i);
        if ((entry >> hash_size) == 0)
            continue;
        size_t starting_index = entry & (to.size() - 1);
        HashMapIter it(to, 0, hash_size);
        it.set_index(starting_index);
        while (it.is_valid() && !it.empty()) {
            ++it;
        }
        if (!it.is_valid()) {
            // abort rehashing, we need a larger to-space
            return false;
        }
        REALM_ASSERT(it.empty());
        it.set(entry);
    }
    return true;
}

// Add a binding from hash value to id.
static void add_to_hash_map(Array& node, uint64_t hash, uint64_t id, uint8_t hash_size)
{
    REALM_ASSERT(node.is_attached());
    if (!node.has_refs()) {
        // it's a leaf.
        if (node.size() < linear_search_limit) {
            // it's a list with room to grow
            node.add(((uint64_t)id << hash_size) | hash);
            return;
        }
        if (node.size() == linear_search_limit) {
            // it's a full list, must be converted to a hash table
            Array new_node(node.get_alloc());
            new_node.create(NodeHeader::type_Normal, false, hash_node_min_size, 0);
            new_node.set_parent(node.get_parent(), node.get_ndx_in_parent());
            new_node.update_parent();
            // transform existing list into hash table
            rehash(node, new_node, hash_size);
            node.destroy();
            node.init_from_parent();
        }
        // it's a hash table. Grow if needed up till 'hash_node_max_size' entries
        while (node.size() < hash_node_max_size) {
            auto size = node.size();
            size_t start_index = hash & (size - 1);
            HashMapIter it(node, 0, hash_size);
            it.set_index(start_index);
            while (it.is_valid() && !it.empty()) {
                ++it;
            }
            if (it.is_valid()) {
                // found an empty spot within search range
                it.set(((uint64_t)id << hash_size) | hash);
                return;
            }
            if (node.size() >= hash_node_max_size)
                break;
            // No free spot found - rehash into bigger and bigger tables
            auto new_size = node.size();
            bool need_to_rehash = true;
            Array new_node(node.get_alloc());
            while (need_to_rehash && new_size < hash_node_max_size) {
                new_size *= 2;
                new_node.create(NodeHeader::type_Normal, false, new_size, 0);
                need_to_rehash = !rehash(node, new_node, hash_size);
                if (need_to_rehash) { // we failed, try again - or shift to radix
                    // I find it counter-intuitive. But it CAN happen.
                    new_node.destroy();
                }
            }
            if (need_to_rehash)
                break;
            new_node.set_parent(node.get_parent(), node.get_ndx_in_parent());
            new_node.update_parent();
            node.destroy();
            node.init_from_parent();
        }
        // we ran out of space. Rewrite as a radix node with subtrees
        Array new_node(node.get_alloc());
        new_node.create(NodeHeader::type_HasRefs, false, radix_node_size, 0);
        new_node.set_parent(node.get_parent(), node.get_ndx_in_parent());
        new_node.update_parent();
        for (size_t index = 0; index < node.size(); ++index) {
            auto element = node.get(index);
            auto hash = element & (0xFFFFFFFF >> (32 - hash_size));
            auto string_id = element >> hash_size;
            if (string_id == 0)
                continue;
            auto remaining_hash = hash >> radix_node_consumes_bits;
            add_to_hash_map(new_node, remaining_hash, string_id, hash_size - 8);
        }
        node.destroy();
        node.init_from_parent();
    }
    // We have a radix node and need to insert the new binding into the proper subtree
    size_t index = hash & (radix_node_size - 1);
    auto rot = node.get_as_ref_or_tagged(index);
    REALM_ASSERT(!rot.is_tagged());
    Array subtree(node.get_alloc());
    if (rot.get_as_ref() == 0) {
        // no subtree present, create an empty one
        subtree.set_parent(&node, index);
        subtree.create(NodeHeader::type_Normal);
        subtree.update_parent();
    }
    else {
        // subtree already present
        subtree.set_parent(&node, index);
        subtree.init_from_parent();
    }
    // recurse into subtree
    add_to_hash_map(subtree, hash >> radix_node_consumes_bits, id, hash_size - radix_node_consumes_bits);
}

static std::vector<uint32_t> hash_to_id(Array& node, uint32_t hash, uint8_t hash_size)
{
    std::vector<uint32_t> result;
    REALM_ASSERT(node.is_attached());
    if (!node.has_refs()) {
        // it's a leaf - default is a list, search starts from index 0.
        HashMapIter it(node, hash, hash_size);
        if (node.size() >= hash_node_min_size) {
            // it is a hash table, so use hash to select index to start searching
            // table size must be power of two!
            size_t index = hash & (node.size() - 1);
            it.set_index(index);
        }
        // collect all matching values within allowed range
        while (it.is_valid()) {
            if (it.matches()) {
                result.push_back(it.get());
            }
            ++it;
        }
        return result;
    }
    else {
        // it's a radix node
        size_t index = hash & (node.size() - 1);
        auto rot = node.get_as_ref_or_tagged(index);
        REALM_ASSERT(rot.is_ref());
        if (rot.get_as_ref() == 0) {
            // no subtree, return empty vector
            return result;
        }
        // descend into subtree
        Array subtree(node.get_alloc());
        subtree.set_parent(&node, index);
        subtree.init_from_parent();
        return hash_to_id(subtree, hash >> radix_node_consumes_bits, hash_size - radix_node_consumes_bits);
    }
}


enum positions { Pos_Version, Pos_ColKey, Pos_Size, Pos_Compressor, Pos_Data, Pos_Map, Top_Size };
struct StringInterner::DataLeaf {
    std::vector<CompressedStringView> m_compressed;
    ref_type m_leaf_ref = 0;
    bool m_is_loaded = false;
    DataLeaf() {}
    DataLeaf(ref_type ref)
        : m_leaf_ref(ref)
    {
    }
};

StringInterner::StringInterner(Allocator& alloc, Array& parent, ColKey col_key, bool writable)
    : m_parent(parent)
    , m_top(alloc)
    , m_data(alloc)
    , m_hash_map(alloc)
    , m_current_string_leaf(alloc)
    , m_current_long_string_node(alloc)
{
    REALM_ASSERT_DEBUG(col_key != ColKey());
    size_t index = col_key.get_index().val;
    // ensure that m_top and m_data is well defined and reflect any existing data
    // We'll have to extend this to handle no defined backing
    m_top.set_parent(&parent, index);
    m_data.set_parent(&m_top, Pos_Data);
    m_hash_map.set_parent(&m_top, Pos_Map);
    m_col_key = col_key;
    update_from_parent(writable);
}

void StringInterner::update_from_parent(bool writable)
{
    auto parent_idx = m_top.get_ndx_in_parent();
    bool valid_top_ref_spot = m_parent.is_attached() && parent_idx < m_parent.size();
    bool valid_top = valid_top_ref_spot && m_parent.get_as_ref(parent_idx);
    if (valid_top) {
        m_top.update_from_parent();
        m_data.update_from_parent();
        m_hash_map.update_from_parent();
    }
    else if (writable && valid_top_ref_spot) {
        m_top.create(NodeHeader::type_HasRefs, false, Top_Size, 0);
        m_top.set(Pos_Version, (1 << 1) + 1); // version number 1.
        m_top.set(Pos_Size, (0 << 1) + 1);    // total size 0
        m_top.set(Pos_ColKey, (m_col_key.value << 1) + 1);
        m_top.set(Pos_Compressor, 0);

        // create first level of data tree here (to simplify other stuff)
        m_data.create(NodeHeader::type_HasRefs, false, 0);
        m_data.update_parent();

        m_hash_map.create(NodeHeader::type_Normal);
        m_hash_map.update_parent();
        m_top.update_parent();
        valid_top = true;
    }
    if (!valid_top) {
        // We're lacking part of underlying data and not allowed to create it, so enter "dead" mode
        m_compressor.reset();
        m_compressed_leafs.clear();
        // m_compressed_string_map.clear();
        m_top.detach();
        m_data.detach();
        m_hash_map.detach();
        m_compressor.reset();
        return;
    }
    // validate we're accessing data for the correct column. A combination of column erase
    // and insert could lead to an interner being paired with wrong data in the file.
    // If so, we clear internal data forcing rebuild_internal() to rebuild from scratch.
    int64_t data_colkey = m_top.get_as_ref_or_tagged(Pos_ColKey).get_as_int();
    if (m_col_key.value != data_colkey) {
        // new column, new data
        m_compressor.reset();
        m_decompressed_strings.clear();
    }
    if (!m_compressor)
        m_compressor = std::make_unique<StringCompressor>(m_top.get_alloc(), m_top, Pos_Compressor, writable);
    else
        m_compressor->refresh(writable);
    if (m_data.size()) {
        auto ref_to_write_buffer = m_data.get_as_ref(m_data.size() - 1);
        const char* header = m_top.get_alloc().translate(ref_to_write_buffer);
        bool is_array_of_cprs = NodeHeader::get_hasrefs_from_header(header);
        if (is_array_of_cprs) {
            m_current_long_string_node.set_parent(&m_data, m_data.size() - 1);
            m_current_long_string_node.update_from_parent();
        }
        else {
            m_current_long_string_node.detach();
        }
    }
    else
        m_current_long_string_node.detach(); // just in case...

    // rebuild internal structures......
    rebuild_internal();
    m_current_string_leaf.detach();
}

void StringInterner::rebuild_internal()
{
    std::lock_guard lock(m_mutex);
    // release old decompressed strings
    for (size_t idx = 0; idx < m_in_memory_strings.size(); ++idx) {
        StringID id = m_in_memory_strings[idx];
        if (id > m_decompressed_strings.size()) {
            m_in_memory_strings[idx] = m_in_memory_strings.back();
            m_in_memory_strings.pop_back();
            continue;
        }
        if (auto& w = m_decompressed_strings[id - 1].m_weight) {
            w >>= 1;
        }
        else {
            m_decompressed_strings[id - 1].m_decompressed.reset();
            m_in_memory_strings[idx] = m_in_memory_strings.back();
            m_in_memory_strings.pop_back();
            continue;
        }
    }

    size_t target_size = (size_t)m_top.get_as_ref_or_tagged(Pos_Size).get_as_int();
    m_decompressed_strings.resize(target_size);
    if (m_data.size() != m_compressed_leafs.size()) {
        m_compressed_leafs.resize(m_data.size());
    }
    // always force new setup of all leafs:
    // update m_compressed_leafs to reflect m_data
    for (size_t idx = 0; idx < m_compressed_leafs.size(); ++idx) {
        auto ref = m_data.get_as_ref(idx);
        auto& leaf_meta = m_compressed_leafs[idx];
        leaf_meta.m_is_loaded = false;
        leaf_meta.m_compressed.clear();
        leaf_meta.m_leaf_ref = ref;
    }
}

StringInterner::~StringInterner() {}

StringID StringInterner::intern(StringData sd)
{
    REALM_ASSERT(m_top.is_attached());
    std::lock_guard lock(m_mutex);
    // special case for null string
    if (sd.data() == nullptr)
        return 0;
    uint32_t h = (uint32_t)sd.hash();
    auto candidates = hash_to_id(m_hash_map, h, 32);
    for (auto& candidate : candidates) {
        auto candidate_cpr = get_compressed(candidate);
        if (m_compressor->compare(sd, candidate_cpr) == 0)
            return candidate;
    }
    // it's a new string
    bool learn = true;
    auto c_str = m_compressor->compress(sd, learn);
    m_decompressed_strings.push_back({64, std::make_unique<std::string>(sd)});
    auto id = m_decompressed_strings.size();
    m_in_memory_strings.push_back(id);
    add_to_hash_map(m_hash_map, h, id, 32);
    size_t index = (size_t)m_top.get_as_ref_or_tagged(Pos_Size).get_as_int();
    REALM_ASSERT_DEBUG(index == id - 1);
    bool need_long_string_node = c_str.size() >= 65536;

    // TODO: update_internal must set up m_current_long_string_node if it is in use
    if (need_long_string_node && !m_current_long_string_node.is_attached()) {

        m_current_long_string_node.create(NodeHeader::type_HasRefs);

        if ((index & 0xFF) == 0) {
            // if we're starting on a new leaf, extend parent array for it
            m_data.add(0);
            m_compressed_leafs.push_back({});
            m_current_long_string_node.set_parent(&m_data, m_data.size() - 1);
            m_current_long_string_node.update_parent();
            REALM_ASSERT_DEBUG(!m_current_string_leaf.is_attached() || m_current_string_leaf.size() == 0);
            m_current_string_leaf.detach();
        }
        else {
            // we have been building an existing leaf and need to shift representation.
            // but first we need to update leaf accessor for existing leaf
            if (m_current_string_leaf.is_attached()) {
                m_current_string_leaf.update_from_parent();
            }
            else {
                m_current_string_leaf.init_from_ref(m_current_string_leaf.get_ref_from_parent());
            }
            REALM_ASSERT_DEBUG(m_current_string_leaf.size() > 0);
            m_current_long_string_node.set_parent(&m_data, m_data.size() - 1);
            m_current_long_string_node.update_parent();
            // convert the current leaf into a long string node. (array of strings in separate arrays)
            for (auto s : m_compressed_leafs.back().m_compressed) {
                ArrayUnsigned arr(m_top.get_alloc());
                arr.create(s.size, 65535);
                unsigned short* dest = reinterpret_cast<unsigned short*>(arr.m_data);
                std::copy_n(s.data, s.size, dest);
                m_current_long_string_node.add(arr.get_ref());
            }
            m_current_string_leaf.destroy();
            // force later reload of leaf
            m_compressed_leafs.back().m_is_loaded = false;
        }
    }
    if (m_current_long_string_node.is_attached()) {
        ArrayUnsigned arr(m_top.get_alloc());
        arr.create(c_str.size(), 65535);
        unsigned short* begin = c_str.data();
        if (begin) {
            // if the compressed string is empty, 'begin' is zero and we don't copy
            size_t n = c_str.size();
            unsigned short* dest = reinterpret_cast<unsigned short*>(arr.m_data);
            std::copy_n(begin, n, dest);
        }
        m_current_long_string_node.add(arr.get_ref());
        m_current_long_string_node.update_parent();
        if (m_current_long_string_node.size() == 256) {
            // exit from  "long string mode"
            m_current_long_string_node.detach();
        }
        CompressionSymbol* p_start = reinterpret_cast<CompressionSymbol*>(arr.m_data);
        m_compressed_leafs.back().m_compressed.push_back({p_start, arr.size()});
    }
    else {
        // Append to leaf with up to 256 entries.
        // First create a new leaf if needed (limit number of entries to 256 pr leaf)
        bool need_leaf_update = !m_current_string_leaf.is_attached() || (index & 0xFF) == 0;
        if (need_leaf_update) {
            m_current_string_leaf.set_parent(&m_data, index >> 8);
            if ((index & 0xFF) == 0) {
                // create new leaf
                m_current_string_leaf.create(0, 65535);
                m_data.add(m_current_string_leaf.get_ref());
                m_compressed_leafs.push_back({});
            }
            else {
                // just setup leaf accessor
                if (m_current_string_leaf.is_attached()) {
                    m_current_string_leaf.update_from_parent();
                }
                else {
                    m_current_string_leaf.init_from_ref(m_current_string_leaf.get_ref_from_parent());
                }
            }
        }
        REALM_ASSERT(c_str.size() < 65535);
        // Add compressed string at end of leaf
        m_current_string_leaf.add(c_str.size());
        for (auto c : c_str) {
            m_current_string_leaf.add(c);
        }
        REALM_ASSERT_DEBUG(m_compressed_leafs.size());
        CompressionSymbol* p = reinterpret_cast<CompressionSymbol*>(m_current_string_leaf.m_data);
        auto p_limit = p + m_current_string_leaf.size();
        auto p_start = p_limit - c_str.size();
        m_compressed_leafs.back().m_compressed.push_back({p_start, c_str.size()});
        REALM_ASSERT(m_compressed_leafs.back().m_compressed.size() <= 256);
    }
    m_top.adjust(Pos_Size, 2); // type is has_Refs, so increment is by 2
    load_leaf_if_new_ref(m_compressed_leafs.back(), m_data.get_as_ref(m_data.size() - 1));
#ifdef REALM_DEBUG
    auto csv = get_compressed(id);
    CompressedStringView csv2(c_str);
    REALM_ASSERT(csv == csv2);
#endif
    return id;
}

bool StringInterner::load_leaf_if_needed(DataLeaf& leaf)
{
    if (!leaf.m_is_loaded) {
        // start with an empty leaf:
        leaf.m_compressed.clear();
        leaf.m_compressed.reserve(256);

        // must interpret leaf first - the leaf is either a single array holding all strings,
        // or an array with each (compressed) string placed in its own array.
        const char* header = m_top.get_alloc().translate(leaf.m_leaf_ref);
        bool is_single_array = !NodeHeader::get_hasrefs_from_header(header);
        if (is_single_array) {
            size_t leaf_offset = 0;
            ArrayUnsigned leaf_array(m_top.get_alloc());
            leaf_array.init_from_ref(leaf.m_leaf_ref);
            REALM_ASSERT(NodeHeader::get_encoding(leaf_array.get_header()) == NodeHeader::Encoding::WTypBits);
            REALM_ASSERT(NodeHeader::get_width_from_header(leaf_array.get_header()) == 16);
            // This is dangerous if the leaf is for some reason not in the assumed format
            CompressionSymbol* c = reinterpret_cast<CompressionSymbol*>(leaf_array.m_data);
            auto leaf_size = leaf_array.size();
            while (leaf_offset < leaf_size) {
                size_t length = c[leaf_offset];
                REALM_ASSERT_DEBUG(length == leaf_array.get(leaf_offset));
                leaf_offset++;
                leaf.m_compressed.push_back({c + leaf_offset, length});
                REALM_ASSERT_DEBUG(leaf.m_compressed.size() <= 256);
                leaf_offset += length;
            }
        }
        else {
            // Not a single leaf - instead an array of strings
            Array arr(m_top.get_alloc());
            arr.init_from_ref(leaf.m_leaf_ref);
            for (size_t idx = 0; idx < arr.size(); ++idx) {
                ArrayUnsigned str_array(m_top.get_alloc());
                ref_type ref = arr.get_as_ref(idx);
                str_array.init_from_ref(ref);
                REALM_ASSERT(NodeHeader::get_encoding(str_array.get_header()) == NodeHeader::Encoding::WTypBits);
                REALM_ASSERT(NodeHeader::get_width_from_header(str_array.get_header()) == 16);
                CompressionSymbol* c = reinterpret_cast<CompressionSymbol*>(str_array.m_data);
                leaf.m_compressed.push_back({c, str_array.size()});
            }
        }
        leaf.m_is_loaded = true;
        return true;
    }
    return false;
}

// Danger: Only to be used if you know that a change in content ==> different ref
bool StringInterner::load_leaf_if_new_ref(DataLeaf& leaf, ref_type new_ref)
{
    if (leaf.m_leaf_ref != new_ref) {
        leaf.m_leaf_ref = new_ref;
        leaf.m_is_loaded = false;
        leaf.m_compressed.resize(0);
    }
    return load_leaf_if_needed(leaf);
}

CompressedStringView& StringInterner::get_compressed(StringID id)
{
    auto index = id - 1; // 0 represents null
    auto hi = index >> 8;
    auto lo = index & 0xFFUL;

    DataLeaf& leaf = m_compressed_leafs[hi];
    load_leaf_if_needed(leaf);
    REALM_ASSERT_DEBUG(lo < leaf.m_compressed.size());
    return leaf.m_compressed[lo];
}

std::optional<StringID> StringInterner::lookup(StringData sd)
{
    if (!m_top.is_attached()) {
        // "dead" mode
        return {};
    }
    std::lock_guard lock(m_mutex);
    if (sd.data() == nullptr)
        return 0;
    uint32_t h = (uint32_t)sd.hash();
    auto candidates = hash_to_id(m_hash_map, h, 32);
    for (auto& candidate : candidates) {
        auto candidate_cpr = get_compressed(candidate);
        if (m_compressor->compare(sd, candidate_cpr) == 0)
            return candidate;
    }
    return {};
}

int StringInterner::compare(StringID A, StringID B)
{
    std::lock_guard lock(m_mutex);
    // 0 is null, the first index starts from 1.
    REALM_ASSERT_DEBUG(A <= m_decompressed_strings.size());
    REALM_ASSERT_DEBUG(B <= m_decompressed_strings.size());
    // comparisons against null
    if (A == B && A == 0)
        return 0;
    if (A == 0)
        return -1;
    if (B == 0)
        return 1;
    // ok, no nulls.
    REALM_ASSERT(m_compressor);
    return m_compressor->compare(get_compressed(A), get_compressed(B));
}

int StringInterner::compare(StringData s, StringID A)
{
    std::lock_guard lock(m_mutex);
    REALM_ASSERT_DEBUG(A <= m_decompressed_strings.size());
    // comparisons against null
    if (s.data() == nullptr && A == 0)
        return 0;
    if (s.data() == nullptr)
        return 1;
    if (A == 0)
        return -1;
    // ok, no nulls
    REALM_ASSERT(m_compressor);
    return m_compressor->compare(s, get_compressed(A));
}


StringData StringInterner::get(StringID id)
{
    REALM_ASSERT(m_compressor);
    std::lock_guard lock(m_mutex);
    if (id == 0)
        return StringData{nullptr};
    REALM_ASSERT_DEBUG(id <= m_decompressed_strings.size());
    CachedString& cs = m_decompressed_strings[id - 1];
    if (cs.m_decompressed) {
        if (cs.m_weight < 128)
            cs.m_weight += 64;
        return {cs.m_decompressed->c_str(), cs.m_decompressed->size()};
    }
    cs.m_weight = 64;
    cs.m_decompressed = std::make_unique<std::string>(m_compressor->decompress(get_compressed(id)));
    m_in_memory_strings.push_back(id);
    return {cs.m_decompressed->c_str(), cs.m_decompressed->size()};
}

} // namespace realm
