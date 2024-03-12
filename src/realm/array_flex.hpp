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

#include <cstdint>
#include <stddef.h>
#include <vector>

namespace realm {
//
// Compress array in Flex format
// Decompress array in WTypeBits formats
//
class Array;
class ArrayEncode;
class QueryStateBase;
class ArrayFlex {
public:
    // encoding/decoding
    void init_array(char* h, uint8_t flags, size_t v_width, size_t ndx_width, size_t v_size, size_t ndx_size) const;
    void copy_data(const Array&, const std::vector<int64_t>&, const std::vector<size_t>&) const;
    // getters/setters
    int64_t get(const Array&, size_t) const;
    int64_t get(const char*, size_t, const ArrayEncode&) const;
    void get_chunk(const Array& h, size_t ndx, int64_t res[8]) const;
    void set_direct(const Array&, size_t, int64_t) const;

    template <typename Cond>
    bool find_all(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;

    int64_t sum(const Array&, size_t, size_t) const;

    struct WordTypeValue {};
    struct WordTypeIndex {};

private:
    int64_t do_get(uint64_t*, size_t, size_t, size_t, size_t, size_t, uint64_t) const;
    bool find_all_match(size_t start, size_t end, size_t baseindex, QueryStateBase* state) const;
    bool find_eq(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;
    bool find_neq(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;
    bool find_lt(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;
    bool find_gt(const Array&, int64_t, size_t, size_t, size_t, QueryStateBase*) const;
};
} // namespace realm
#endif // REALM_ARRAY_COMPRESS_HPP
