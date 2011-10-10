#include "Array.h"
#include <cassert>

#include "Column.h"

Array::Array(size_t ref, Array* parent, size_t pndx, Allocator& alloc)
: m_data(NULL), m_len(0), m_capacity(0), m_width(0), m_isNode(false), m_hasRefs(false), m_parent(parent), m_parentNdx(pndx), m_alloc(alloc) {
    Create(ref);
}

Array::Array(size_t ref, const Array* parent, size_t pndx, Allocator& alloc)
: m_data(NULL), m_len(0), m_capacity(0), m_width(0), m_isNode(false), m_hasRefs(false), m_parent(const_cast<Array*>(parent)), m_parentNdx(pndx), m_alloc(alloc) {
    Create(ref);
}

Array::Array(ColumnDef type, Array* parent, size_t pndx, Allocator& alloc)
: m_data(NULL), m_len(0), m_capacity(0), m_width(0), m_isNode(false), m_hasRefs(false), m_parent(parent), m_parentNdx(pndx), m_alloc(alloc) {
    if (type == COLUMN_NODE) m_isNode = m_hasRefs = true;
    else if (type == COLUMN_HASREFS)    m_hasRefs = true;

    Alloc(0, 0);
    SetWidth(0);
}

// Copy-constructor
// Note that this array now own the ref. Should only be used when
// the source array goes away right after (like return values from functions)
Array::Array(const Array& src) : m_parent(src.m_parent), m_parentNdx(src.m_parentNdx), m_alloc(src.m_alloc) {
    const size_t ref = src.GetRef();
    Create(ref);
}

void Array::Create(size_t ref) {
    assert(ref);

    MemRef::Header* const header = (MemRef::Header*)m_alloc.Translate(ref);
    m_isNode   = header->isNode;
    m_hasRefs  = header->hasRefs;
    m_width    = 1 << (header->width) >> 1; // 0, 1, 2, 4, 8, 16, 32, 64
    m_len      = header->count;
    m_capacity = header->capacity;

    m_ref = ref;
    m_data = (uint8_t*)header + MEMREF_HEADER_SIZE;

    SetWidth(m_width);
}

void Array::SetType(ColumnDef type) {
    if (type == COLUMN_NODE) m_isNode = m_hasRefs = true;
    else if (type == COLUMN_HASREFS)    m_hasRefs = true;
    else m_isNode = m_hasRefs = false;
}

bool Array::operator==(const Array& a) const {
    return m_data == a.m_data;
}

void Array::UpdateRef(size_t ref) {
    Create(ref);

    // Update ref in parent
    if (m_parent) m_parent->Set(m_parentNdx, ref);
}

/**
 * Takes a 64bit value and return the minimum number of bits needed to fit the
 * value.
 * For alignment this is rounded up to nearest log2.
 * Posssible results {0, 1, 2, 4, 8, 16, 32, 64}
 */
static unsigned int BitWidth(int64_t v) {
    if ((v >> 4) == 0) {
        static const int8_t bits[] = {0, 1, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
        return bits[(int8_t)v];
    }

    // First flip all bits if bit 63 is set (will now always be zero)
    if (v < 0) v = ~v;

    // Then check if bits 15-31 used (32b), 7-31 used (16b), else (8b)
    return (v >> 31) ? 64 : ((v >> 15) ? 32 : ((v >> 7) ? 16 : 8));
}

void Array::SetParent(Array* parent, size_t pndx) {
    m_parent = parent;
    m_parentNdx = pndx;
}

Array Array::GetSubArray(size_t ndx) {
    assert(ndx < m_len);
    assert(m_hasRefs);

    const size_t ref = (size_t)Get(ndx);
    assert(ref);

    return Array(ref, this, ndx, m_alloc);
}

const Array Array::GetSubArray(size_t ndx) const {
    assert(ndx < m_len);
    assert(m_hasRefs);

    return Array((size_t)Get(ndx), this, ndx, m_alloc);
}

void Array::Destroy() {
    if (!m_data) return;

    if (m_hasRefs) {
        for (size_t i = 0; i < Size(); ++i) {
            const size_t ref = (size_t)Get(i);
            Array sub(ref, this, i, m_alloc);
            sub.Destroy();
        }
    }

    m_alloc.Free(m_ref, MEMREF_GET_HEADER(m_data));
    m_data = NULL;
}

void Array::Clear() {
    // Make sure we don't have any dangling references
    if (m_hasRefs) {
        for (size_t i = 0; i < Size(); ++i) {
            Array sub((size_t)Get(i), this, i);
            sub.Destroy();
        }
    }

    // Truncate size to zero (but keep capacity)
    m_len = 0;
    SetWidth(0);
}

void Array::Delete(size_t ndx) {
    assert(ndx < m_len);

    // Move values below deletion up
    if (m_width < 8) {
        for (size_t i = ndx+1; i < m_len; ++i) {
            const int64_t v = (this->*m_getter)(i);
            (this->*m_setter)(i-1, v);
        }
    }
    else if (ndx < m_len-1) {
        // when byte sized, use memmove
        const size_t w = (m_width == 64) ? 8 : (m_width == 32) ? 4 : (m_width == 16) ? 2 : 1;
        unsigned char* dst = m_data + (ndx * w);
        unsigned char* src = dst + w;
        const size_t count = (m_len - ndx - 1) * w;
        memmove(dst, src, count);
    }

    // Update length (also in header)
    --m_len;
    MEMREF_GET_HEADER(m_data)->count = m_len;
}

int64_t Array::Get(size_t ndx) const {
    assert(ndx < m_len);
    return (this->*m_getter)(ndx);
}

int64_t Array::Back() const {
    assert(m_len);
    return (this->*m_getter)(m_len-1);
}

bool Array::Set(size_t ndx, int64_t value) {
    assert(ndx < m_len);

    // Make room for the new value
    const size_t width = BitWidth(value);
    if (width > m_width) {
        Getter oldGetter = m_getter;
        if (!Alloc(m_len, width)) return false;
        SetWidth(width);

        // Expand the old values
        int k = (int)m_len;
        while (--k >= 0) {
            const int64_t v = (this->*oldGetter)(k);
            (this->*m_setter)(k, v);
        }
    }

    // Set the value
    (this->*m_setter)(ndx, value);

    return true;
}

bool Array::Insert(size_t ndx, int64_t value) {
    assert(ndx <= m_len);

    Getter getter = m_getter;

    // Make room for the new value
    const size_t width = BitWidth(value);
    const bool doExpand = (width > m_width);
    if (doExpand) {
        if (!Alloc(m_len+1, width)) return false;
        SetWidth(width);
    }
    else {
        if (!Alloc(m_len+1, m_width)) return false;
    }

    // Move values below insertion (may expand)
    if (doExpand || m_width < 8) {
        int k = (int)m_len;
        while (--k >= (int)ndx) {
            const int64_t v = (this->*getter)(k);
            (this->*m_setter)(k+1, v);
        }
    }
    else if (ndx != m_len) {
        // when byte sized and no expansion, use memmove
        const size_t w = (m_width == 64) ? 8 : (m_width == 32) ? 4 : (m_width == 16) ? 2 : 1;
        unsigned char* src = m_data + (ndx * w);
        unsigned char* dst = src + w;
        const size_t count = (m_len - ndx) * w;
        memmove(dst, src, count);
    }

    // Insert the new value
    (this->*m_setter)(ndx, value);

    // Expand values above insertion
    if (doExpand) {
        int k = (int)ndx;
        while (--k >= 0) {
            const int64_t v = (this->*getter)(k);
            (this->*m_setter)(k, v);
        }
    }

    // Update length
    // (no need to do it in header as it has been done by Alloc)
    ++m_len;

    return true;
}

bool Array::Add(int64_t value) {
    return Insert(m_len, value);
}

void Array::Resize(size_t count) {
    assert(count <= m_len);

    // Update length (also in header)
    m_len = count;
    MEMREF_GET_HEADER(m_data)->count = m_len;
}

bool Array::Increment(int64_t value, size_t start, size_t end) {
    if (end == -1) end = m_len;
    assert(start < m_len);
    assert(end >= start && end <= m_len);

    // Increment range
    for (size_t i = start; i < end; ++i) {
        Set(i, Get(i) + value);
    }
    return true;
}

bool Array::IncrementIf(int64_t limit, int64_t value) {
    // Update (incr or decrement) values bigger or equal to the limit
    for (size_t i = 0; i < m_len; ++i) {
        const int64_t v = Get(i);
        if (v >= limit) Set(i, v + value);
    }
    return true;
}

size_t Array::FindPos(int64_t target) const {
    int low = -1;
    int high = (int)m_len;

    // Binary search based on:
    // http://www.tbray.org/ongoing/When/200x/2003/03/22/Binary
    // Finds position of largest value SMALLER than the target (for lookups in
    // nodes)
    while (high - low > 1) {
        const size_t probe = ((unsigned int)low + (unsigned int)high) >> 1;
        const int64_t v = (this->*m_getter)(probe);

        if (v > target) high = (int)probe;
        else            low = (int)probe;
    }
    if (high == (int)m_len) return (size_t)-1;
    else return high;
}

size_t Array::FindPos2(int64_t target) const {
    int low = -1;
    int high = (int)m_len;

    // Binary search based on:
    // http://www.tbray.org/ongoing/When/200x/2003/03/22/Binary
    // Finds position of closest value BIGGER OR EQUAL to the target (for
    // lookups in indexes)
    while (high - low > 1) {
        const size_t probe = ((unsigned int)low + (unsigned int)high) >> 1;
        const int64_t v = (this->*m_getter)(probe);

        if (v < target) low = (int)probe;
        else            high = (int)probe;
    }
    if (high == (int)m_len) return (size_t)-1;
    else return high;
}

size_t Array::Find(int64_t value, size_t start, size_t end) const {
    if (IsEmpty()) return (size_t)-1;
    if (end == -1) end = m_len;
    if (start == end) return (size_t)-1;

    assert(start < m_len && end <= m_len && start < end);

    // If the value is wider than the column
    // then we know it can't be there
    const size_t width = BitWidth(value);
    if (width > m_width) return (size_t)-1;

    // Do optimized search based on column width
    if (m_width == 0) {
        return start; // value can only be zero
    }
    else if (m_width == 2) {
        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0x3 * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 32;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x5555555555555555UL) & ~v2
                                             & 0xAAAAAAAAAAAAAAAAUL;
            if (hasZeroByte) break;
            ++p;
        }

        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 32;

        // Manually check the rest
        while (i < end) {
            const size_t offset = i >> 2;
            const int64_t v = (m_data[offset] >> ((i & 3) << 1)) & 0x03;
            if (v == value) return i;
            ++i;
        }
    }
    else if (m_width == 4) {
        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0xF * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 16;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x1111111111111111UL) & ~v2 
                                             & 0x8888888888888888UL;
            if (hasZeroByte) break;
            ++p;
        }

        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 16;

        // Manually check the rest
        while (i < end) {
            const size_t offset = i >> 1;
            const int64_t v = (m_data[offset] >> ((i & 1) << 2)) & 0xF;
            if (v == value) return i;
            ++i;
        }
    }
  else if (m_width == 8) {
        // TODO: Handle partial searches

        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0xFF * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 8;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x0101010101010101ULL) & ~v2
                                             & 0x8080808080808080ULL;
            if (hasZeroByte) break;
            ++p;
        }

        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 8;
        const int8_t* d = (const int8_t*)m_data;

        // Manually check the rest
        while (i < end) {
            if (value == d[i]) return i;
            ++i;
        }
    }
    else if (m_width == 16) {
        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0xFFFF * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 4;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x0001000100010001UL) & ~v2
                                             & 0x8000800080008000UL;
            if (hasZeroByte) break;
            ++p;
        }
        
        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 4;
        const int16_t* d = (const int16_t*)m_data;

        // Manually check the rest
        while (i < end) {
            if (value == d[i]) return i;
            ++i;
        }
    }
    else if (m_width == 32) {
        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0xFFFFFFFF * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 2;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x0000000100000001UL) & ~v2
                                             & 0x8000800080000000UL;
            if (hasZeroByte) break;
            ++p;
        }
        
        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 2;
        const int32_t* d = (const int32_t*)m_data;

        // Manually check the rest
        while (i < end) {
            if (value == d[i]) return i;
            ++i;
        }
    }
    else if (m_width == 64) {
        const int64_t v = (int64_t)value;
        const int64_t* p = (const int64_t*)m_data + start;
        const int64_t* const e = (const int64_t*)m_data + end;
        while (p < e) {
            if (*p == v) return p - (const int64_t*)m_data;
            ++p;
        }
    }
    else {
        // Naive search
        for (size_t i = start; i < end; ++i) {
            const int64_t v = (this->*m_getter)(i);
            if (v == value) return i;
        }
    }

    return (size_t)-1; // not found
}

void Array::FindAll(Column& result, int64_t value, size_t colOffset,
                    size_t start, size_t end) const {
    if (IsEmpty()) return;
    if (end == -1) end = m_len;
    if (start == end) return;

    assert(start < m_len && end <= m_len && start < end);

    // If the value is wider than the column
    // then we know it can't be there
    const size_t width = BitWidth(value);
    if (width > m_width) return;

    // Do optimized search based on column width
    if (m_width == 0) {
        for(size_t i = start; i < end; i++){
            result.Add(i + colOffset); // All values can only be zero.
        }
    }
    else if (m_width == 2) {
        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0x3 * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 32;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x5555555555555555UL) & ~v2 
                                         & 0xAAAAAAAAAAAAAAAAUL;
            if (hasZeroByte){
                // Element number at start of block
                size_t i = (p - (const int64_t*)m_data) * 32;
                // Last element of block
                size_t j = i + 32;

                // check block
                while (i < j) {
                    const size_t offset = i >> 2;
                    const int64_t v = (m_data[offset] >> ((i & 3) << 1)) & 0x03;
                    if (v == value) result.Add(i + colOffset);
                    ++i;
                }
            }
            ++p;
        }

        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 32;

        // Manually check the rest
        while (i < end) {
            const size_t offset = i >> 2;
            const int64_t v = (m_data[offset] >> ((i & 3) << 1)) & 0x03;
            if (v == value) result.Add(i + colOffset);
            ++i;
        }
    }
    else if (m_width == 4) {
        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0xF * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 16;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x1111111111111111UL) & ~v2 
                                         & 0x8888888888888888UL;
            if (hasZeroByte){
                // Element number at start of block
                size_t i = (p - (const int64_t*)m_data) * 16;
                // Last element of block
                size_t j = i + 16;

                // check block
                while (i < j) {
                    const size_t offset = i >> 1;
                    const int64_t v = (m_data[offset] >> ((i & 1) << 2)) & 0xF;
                    if (v == value) result.Add(i + colOffset);
                    ++i;
                }
            }
            ++p;
        }

        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 16;

        // Manually check the rest
        while (i < end) {
            const size_t offset = i >> 1;
            const int64_t v = (m_data[offset] >> ((i & 1) << 2)) & 0xF;
            if (v == value) result.Add(i + colOffset);
            ++i;
        }
    }
    else if (m_width == 8) {
        // TODO: Handle partial searches

        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0xFF * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 8;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x0101010101010101ULL) & ~v2
                                             & 0x8080808080808080ULL;
            if (hasZeroByte){
                // Element number at start of block
                size_t i = (p - (const int64_t*)m_data) * 8;
                // Last element of block
                size_t j = i + 8;
                // Data pointer
                const int8_t* d = (const int8_t*)m_data;

                // check block
                while (i < j) {
                    if (value == d[i]) result.Add(i + colOffset);
                    ++i;
                }
            }
            ++p;
        }

        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 8;
        const int8_t* d = (const int8_t*)m_data;

        // Manually check the rest
        while (i < end) {
            if (value == d[i]) result.Add(i + colOffset);
            ++i;
        }
    }
    else if (m_width == 16) {
        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0xFFFF * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 4;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x0001000100010001UL) & ~v2
                                             & 0x8000800080008000UL;
            if (hasZeroByte){
                // Element number at start of block
                size_t i = (p - (const int64_t*)m_data) * 4;
                // Last element of block
                size_t j = i + 4;
                // Data pointer
                const int16_t* d = (const int16_t*)m_data;

                // check block
                while (i < j) {
                    if (value == d[i]) result.Add(i + colOffset);
                    ++i;
                }
            }
            ++p;
        }

        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 4;
        const int16_t* d = (const int16_t*)m_data;

        // Manually check the rest
        while (i < end) {
            if (value == d[i]) result.Add(i + colOffset);
            ++i;
        }
    }
    else if (m_width == 32) {
        // Create a pattern to match 64bits at a time
        const int64_t v = ~0ULL/0xFFFFFFFF * value;

        const int64_t* p = (const int64_t*)m_data + start;
        const size_t end64 = m_len / 2;
        const int64_t* const e = (const int64_t*)m_data + end64;

        // Check 64bits at a time for match
        while (p < e) {
            const uint64_t v2 = *p ^ v; // zero matching bit segments
            const uint64_t hasZeroByte = (v2 - 0x0000000100000001UL) & ~v2
                                             & 0x8000800080000000UL;
            if (hasZeroByte){
                // Element number at start of block
                size_t i = (p - (const int64_t*)m_data) * 2;
                // Last element of block
                size_t j = i + 2;
                // Data pointer
                const int32_t* d = (const int32_t*)m_data;

                // check block
                while (i < j) {
                    if (value == d[i]) result.Add(i + colOffset);
                    ++i;
                }
            }
            ++p;
        }

        // Position of last chunk (may be partial)
        size_t i = (p - (const int64_t*)m_data) * 2;
        const int32_t* d = (const int32_t*)m_data;

        // Manually check the rest
        while (i < end) {
            if (value == d[i]) result.Add(i + colOffset);
            ++i;
        }
    }
    else if (m_width == 64) {
        const int64_t v = (int64_t)value;
        const int64_t* p = (const int64_t*)m_data + start;
        const int64_t* const e = (const int64_t*)m_data + end;
        while (p < e) {
            if (*p == v) result.Add((p - (const int64_t*)m_data) + colOffset);
            ++p;
        }
    }
    else {
        // Naive search
        for (size_t i = start; i < end; ++i) {
            const int64_t v = (this->*m_getter)(i);
            if (v == value) result.Add(i + colOffset);
        }
    }
}

void Array::FindAllHamming(Column& result, uint64_t value, size_t maxdist, size_t offset) const {
    // Only implemented for 64bit values
    if (m_width != 64) {
        assert(false);
        return;
    }

    const uint64_t* p = (const uint64_t*)m_data;
    const uint64_t* const e = (const uint64_t*)m_data + m_len;

    // static values needed for population count
    const uint64_t m1  = 0x5555555555555555;
    const uint64_t m2  = 0x3333333333333333;
    const uint64_t m4  = 0x0f0f0f0f0f0f0f0f;
    const uint64_t h01 = 0x0101010101010101;

    while (p < e) {
        uint64_t x = *p ^ value;

        // population count
#if defined(WIN32) && defined(SSE42)
        x = _mm_popcnt_u64(x); // msvc sse4.2 intrinsic
#elif defined(GCC)
        x = __builtin_popcountll(x); // gcc intrinsic
#else
        x -= (x >> 1) & m1;
        x = (x & m2) + ((x >> 2) & m2);
        x = (x + (x >> 4)) & m4;
        x = (x * h01)>>56;
#endif

        if (x < maxdist) {
            const size_t pos = p - (const uint64_t*)m_data;
            result.Add64(offset + pos);
        }

        ++p;
    }
}



bool Array::Alloc(size_t count, size_t width) {
    // Calculate size in bytes
    size_t len = MEMREF_HEADER_SIZE; // always need room for header
    switch (width) {
    case 0:
        break;
    case 1:
        len += count >> 3;
        if (count & 0x07) ++len;
        break;
    case 2:
        len += count >> 2;
        if (count & 0x03) ++len;
        break;
    case 4:
        len += count >> 1;
        if (count & 0x01) ++len;
        break;
    default:
        assert(width == 8 || width == 16 || width == 32 || width == 64);
        len += count * (width >> 3);
    }

    if (len > m_capacity) {
        // Try to expand with 50% to avoid to many reallocs
        size_t new_capacity = m_capacity ? m_capacity + m_capacity / 2 : 128;
        if (new_capacity < len) new_capacity = len; 

        // Allocate the space
        MemRef mref;
        if (m_data) mref = m_alloc.ReAlloc(MEMREF_GET_HEADER(m_data), new_capacity);
        else mref = m_alloc.Alloc(new_capacity);

        if (!mref.pointer) return false;

        m_ref = mref.ref;
        m_data = (unsigned char*)mref.pointer + MEMREF_HEADER_SIZE;
        m_capacity = new_capacity;

        // Update ref in parent
        if (m_parent) m_parent->Set(m_parentNdx, mref.ref); //TODO: ref
    }

    // Pack width in 3 bits (log2)
    unsigned int w = 0;
    unsigned int b = (unsigned int)width;
    while (b) {++w; b >>= 1;}
    assert(0 <= w && w < 8);

    // Update 8-byte header
    // isNode 1 bit, hasRefs 1 bit, 3 bits unused, width 3 bits, len 3 bytes,
    // capacity 3 bytes
    MemRef::Header* const header = MEMREF_GET_HEADER(m_data);
    header->isNode   = m_isNode;
    header->hasRefs  = m_hasRefs;
    header->width    = w;
    header->count    = count;
    header->capacity = m_capacity;

    return true;
}

void Array::SetWidth(size_t width) {
    if (width == 0) {
        m_getter = &Array::Get_0b;
        m_setter = &Array::Set_0b;
    }
    else if (width == 1) {
        m_getter = &Array::Get_1b;
        m_setter = &Array::Set_1b;
    }
    else if (width == 2) {
        m_getter = &Array::Get_2b;
        m_setter = &Array::Set_2b;
    }
    else if (width == 4) {
        m_getter = &Array::Get_4b;
        m_setter = &Array::Set_4b;
    }
    else if (width == 8) {
        m_getter = &Array::Get_8b;
        m_setter = &Array::Set_8b;
    }
    else if (width == 16) {
        m_getter = &Array::Get_16b;
        m_setter = &Array::Set_16b;
    }
    else if (width == 32) {
        m_getter = &Array::Get_32b;
        m_setter = &Array::Set_32b;
    }
    else if (width == 64) {
        m_getter = &Array::Get_64b;
        m_setter = &Array::Set_64b;
    }
    else {
        assert(false);
    }

    m_width = width;
}

int64_t Array::Get_0b(size_t) const {
    return 0;
}

int64_t Array::Get_1b(size_t ndx) const {
    const size_t offset = ndx >> 3;
    return (m_data[offset] >> (ndx & 7)) & 0x01;
}

int64_t Array::Get_2b(size_t ndx) const {
    const size_t offset = ndx >> 2;
    return (m_data[offset] >> ((ndx & 3) << 1)) & 0x03;
}

int64_t Array::Get_4b(size_t ndx) const {
    const size_t offset = ndx >> 1;
    return (m_data[offset] >> ((ndx & 1) << 2)) & 0x0F;
}

int64_t Array::Get_8b(size_t ndx) const {
    return *((const signed char*)(m_data + ndx));
}

int64_t Array::Get_16b(size_t ndx) const {
    const size_t offset = ndx * 2;
    return *(const int16_t*)(m_data + offset);
}

int64_t Array::Get_32b(size_t ndx) const {
    const size_t offset = ndx * 4;
    return *(const int32_t*)(m_data + offset);
}

int64_t Array::Get_64b(size_t ndx) const {
    const size_t offset = ndx * 8;
    return *(const int64_t*)(m_data + offset);
}

void Array::Set_0b(size_t, int64_t) {
}

void Array::Set_1b(size_t ndx, int64_t value) {
    const size_t offset = ndx >> 3;
    ndx &= 7;

    uint8_t* p = &m_data[offset];
    *p = (*p &~ (1 << ndx)) | (((uint8_t)value & 1) << ndx);
}

void Array::Set_2b(size_t ndx, int64_t value) {
    const size_t offset = ndx >> 2;
    const uint8_t n = (ndx & 3) << 1;

    uint8_t* p = &m_data[offset];
    *p = (*p &~ (0x03 << n)) | (((uint8_t)value & 0x03) << n);
}

void Array::Set_4b(size_t ndx, int64_t value) {
    const size_t offset = ndx >> 1;
    const uint8_t n = (ndx & 1) << 2;

    uint8_t* p = &m_data[offset];
    *p = (*p &~ (0x0F << n)) | (((uint8_t)value & 0x0F) << n);
}

void Array::Set_8b(size_t ndx, int64_t value) {
    *((char*)m_data + ndx) = (char)value;
}

void Array::Set_16b(size_t ndx, int64_t value) {
    const size_t offset = ndx * 2;
    *(int16_t*)(m_data + offset) = (int16_t)value;
}

void Array::Set_32b(size_t ndx, int64_t value) {
    const size_t offset = ndx * 4;
    *(int32_t*)(m_data + offset) = (int32_t)value;
}

void Array::Set_64b(size_t ndx, int64_t value) {
    const size_t offset = ndx * 8;
    *(int64_t*)(m_data + offset) = value;
}

void Array::Sort() {
    DoSort(0, m_len-1);
}

void Array::DoSort(size_t lo, size_t hi) {
    // Quicksort based on
    // http://www.inf.fh-flensburg.de/lang/algorithmen/sortieren/quick/quicken.htm
    int i = (int)lo;
    int j = (int)hi;

    // comparison element x
    const size_t ndx = (lo + hi)/2;
    const int64_t x = (size_t)Get(ndx);

    // partition
    do {
        while (Get(i) < x) i++;
        while (Get(j) > x) j--;
        if (i <= j) {
            const int64_t h = Get(i);
            Set(i, Get(j));
            Set(j, h);
            i++; j--;
        }
    } while (i <= j);

    //  recursion
    if ((int)lo < j) DoSort(lo, j);
    if (i < (int)hi) DoSort(i, hi);
}

#ifdef _DEBUG
#include "stdio.h"

void Array::Print() const {
    printf("%zx: (%zu) ", GetRef(), Size());
    for (size_t i = 0; i < Size(); ++i) {
        if (i) printf(", ");
        printf("%d", (int)Get(i));
    }
    printf("\n");
}

void Array::Verify() const {
    assert(m_width == 0 || m_width == 1 || m_width == 2 || m_width == 4 || m_width == 8 || m_width == 16 || m_width == 32 || m_width == 64);
}

void Array::ToDot(FILE* f, bool) const{
    const size_t ref = GetRef();

    fprintf(f, "n%zx [label=\"", ref);

    //if (!horizontal) fprintf(f, "{");
    for (size_t i = 0; i < m_len; ++i) {
        if (i > 0) fprintf(f, " | ");

        if (m_hasRefs) fprintf(f, "<%zu>",i);
        else fprintf(f, "%lld", Get(i));
    }
    //if (!horizontal) fprintf(f, "}");
    
    fprintf(f, "\"];\n");

    if (m_hasRefs) {
        for (size_t i = 0; i < m_len; ++i) {
            fprintf(f, "n%zx:%zu -> n%lld\n", ref, i, Get(i));
        }
    }
    fprintf(f, "\n");
}

#endif //_DEBUG
