#ifndef __TDB_ARRAY__
#define __TDB_ARRAY__

#ifdef _MSC_VER
#include "win32/stdint.h"
#else
#include <stdint.h> // unint8_t etc
#endif
//#include <climits> // size_t
#include <cstdlib> // size_t
#include <cstring> // memmove
#include "alloc.h"
#include <iostream>

#ifdef _DEBUG
#include <stdio.h>
#include <assert.h>
#endif

// Pre-definitions
class Column;


enum ColumnDef {
	COLUMN_NORMAL,
	COLUMN_NODE,
	COLUMN_HASREFS
};

class Array {
public:
	static const int HeaderSize = 8;

	Array(size_t ref, Array* parent=NULL, size_t pndx=0, Allocator& alloc=DefaultAllocator);
	Array(size_t ref, const Array* parent, size_t pndx, Allocator& alloc=DefaultAllocator);
	Array(ColumnDef type=COLUMN_NORMAL, Array* parent=NULL, size_t pndx=0, Allocator& alloc=DefaultAllocator);
	Array(Allocator& alloc);
	Array(const Array& a);

	bool operator==(const Array& a) const;

	void SetType(ColumnDef type);
	void SetParent(Array* parent, size_t pndx);
	void UpdateRef(size_t ref);

	__inline static size_t GetCapacity(const void* p) {
		// Parse the capacity part of 8byte header
		const uint8_t* const header = (uint8_t*)p;
		return (header[4] << 16) + (header[5] << 8) + header[6];
	}

	__inline static size_t GetRefSize(const void* p) {
		// Parse the capacity part of 8byte header
		const uint8_t* const header = (uint8_t*)p;
		return (header[1] << 16) + (header[2] << 8) + header[3];
	}

    __inline static void SetRefSize(void* ref, size_t len) {
	    uint8_t* const header = (uint8_t*)(ref);
	    header[1] = ((len >> 16) & 0x000000FF);
	    header[2] = (len >> 8) & 0x000000FF;
	    header[3] = len & 0x000000FF;
    }


	size_t Size() const {
#ifdef _DEBUG
        assert(GetRefSize(m_data-HeaderSize) == m_len);
#endif
        return m_len;
    }

	bool IsValid() const {return m_data != NULL;}
	void Invalidate() {m_data = NULL;}

	bool IsEmpty() const {return m_len == 0;}


	bool Insert(size_t ndx, int64_t value);
	
    inline bool Add(int64_t value) {
        return Insert(m_len, value);
    }

	bool Set(size_t ndx, int64_t value);
	int64_t Get(size_t ndx) const;
	
    int64_t operator[](size_t ndx) const {return Get(ndx);}
	int64_t Back() const;
	void Delete(size_t ndx);
	void Clear();

	bool Increment(int64_t value, size_t start=0, size_t end=-1);
	bool IncrementIf(int64_t limit, int64_t value);

	size_t FindPos(int64_t value) const;
	size_t FindPos2(int64_t value) const;
	size_t Find(int64_t value, size_t start=0, size_t end=-1) const;
	void FindAll(Column& result, int64_t value, size_t offset=0,
				 size_t start=0, size_t end=-1) const;
	void FindAllHamming(Column& result, uint64_t value, size_t maxdist, size_t offset=0) const;

	void Sort();

	void Resize(size_t count);

	bool IsNode() const {return m_isNode;}
	bool HasRefs() const {return m_hasRefs;}
	Array GetSubArray(size_t ndx);
	const Array GetSubArray(size_t ndx) const;
	size_t GetRef() const {return m_ref;};
	void Destroy();

	Allocator& GetAllocator() const {return m_alloc;}

	// Serialization
	size_t Write(std::ostream& target) const;

	// Debug
	size_t GetBitWidth() const {return m_width;}
#ifdef _DEBUG
	bool Compare(const Array& c) const;
	void Print() const;
	void Verify() const;
	void ToDot(FILE* f, bool horizontal=false) const;
#endif //_DEBUG

private:
	Array& operator=(const Array&) {return *this;} // not allowed

protected:
	void Create(size_t ref);

	void DoSort(size_t lo, size_t hi);

	// Getters and Setters for adaptive-packed arrays
	typedef int64_t(Array::*Getter)(size_t) const;
	typedef void(Array::*Setter)(size_t, int64_t);
	int64_t Get_0b(size_t ndx) const;
	int64_t Get_1b(size_t ndx) const;
	int64_t Get_2b(size_t ndx) const;
	int64_t Get_4b(size_t ndx) const;
	int64_t Get_8b(size_t ndx) const;
	int64_t Get_16b(size_t ndx) const;
	int64_t Get_32b(size_t ndx) const;
	int64_t Get_64b(size_t ndx) const;
	void Set_0b(size_t ndx, int64_t value);
	void Set_1b(size_t ndx, int64_t value);
	void Set_2b(size_t ndx, int64_t value);
	void Set_4b(size_t ndx, int64_t value);
	void Set_8b(size_t ndx, int64_t value);
	void Set_16b(size_t ndx, int64_t value);
	void Set_32b(size_t ndx, int64_t value);
	void Set_64b(size_t ndx, int64_t value);

	virtual size_t CalcByteLen(size_t count, size_t width) const;

	void SetWidth(size_t width);
	bool Alloc(size_t count, size_t width);
	bool CopyOnWrite();

	// Member variables
	Getter m_getter;
	Setter m_setter;
	size_t m_ref;
	uint8_t* m_data;
	Array* m_parent;
	size_t m_parentNdx;
	size_t m_len;
	size_t m_capacity;
	size_t m_width;
	bool m_isNode;
	bool m_hasRefs;
	Allocator& m_alloc;
};

#endif //__TDB_ARRAY__
