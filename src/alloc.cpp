#include "AllocSlab.h"
#include <assert.h>

#define ALLOC_SLAB_SIZE     256
#define ALLOC_SLAB_BASELINE 10

// MP: This class is not ready for review

SlabAlloc::SlabAlloc() : m_shared(NULL), m_baseline(ALLOC_SLAB_BASELINE) {
}

SlabAlloc::~SlabAlloc() {
#ifdef _DEBUG
	assert(IsAllFree());
#endif //_DEBUG

	// Release all allocated memory
	for (size_t i = 0; i < m_slabs.GetSize(); ++i) {
		void* p = (void*)(intptr_t)m_slabs[i].pointer;
		free(p);
	}
}

MemRef SlabAlloc::Alloc(size_t size) {
	// Do we have a free space we can reuse?
	for (size_t i = 0; i < m_freeSpace.GetSize(); ++i) {
		FreeSpace::Cursor r = m_freeSpace[i];
		if (r.size >= (int)size) {
			const size_t location = (size_t)r.ref;
			const size_t rest = (size_t)r.size - size;

			// Update free list
			if (rest == 0) m_freeSpace.DeleteRow(i);
			else {
				r.size = rest;
				r.ref += (unsigned int)size;
			}

			void* pointer = Translate(location);
			return MemRef(pointer, location);
		}
	}

	// Else, allocate new slab
	const size_t multible = ALLOC_SLAB_SIZE * ((size / ALLOC_SLAB_SIZE) + 1);
	const size_t slabsBack = m_slabs.IsEmpty() ? m_baseline : m_slabs.Back().offset;
	const size_t doubleLast = m_slabs.IsEmpty() ? 0 :
		                                          (slabsBack - (m_slabs.GetSize() == 1) ? (size_t)0 : m_slabs[-2].offset) * 2;
	const size_t newsize = multible > doubleLast ? multible : doubleLast;

	// Allocate memory 
	void* slab = malloc(newsize);
	if (!slab) return MemRef(NULL, 0);

	// Add to slab table
	Slabs::Cursor s = m_slabs.Add();
	s.offset = slabsBack + newsize;
	s.pointer = (intptr_t)slab;

	// Update free list
	const size_t rest = newsize - size;
	FreeSpace::Cursor f = m_freeSpace.Add();
	f.ref = slabsBack + size;
	f.size = rest;

	return MemRef(slab, slabsBack);
}

void SlabAlloc::Free(size_t ref, MemRef::Header* header) {
	// Get size from segment
	const size_t size = header->capacity;
	const size_t refEnd = ref + size;
	bool isMerged = false;

	// Check if we can merge with start of free block
	size_t n = m_freeSpace.ref.Find(refEnd);
	if (n != (size_t)-1) {
		// No consolidation over slab borders
		if (m_slabs.offset.Find(refEnd) == (size_t)-1) {
			m_freeSpace[n].ref = ref;
			m_freeSpace[n].size += size;
			isMerged = true;
		}
	}

	// Check if we can merge with end of free block
	if (m_slabs.offset.Find(ref) == (size_t)-1) { // avoid slab borders
		const size_t count = m_freeSpace.GetSize();
		for (size_t i = 0; i < count; ++i) {
			FreeSpace::Cursor c = m_freeSpace[i];
			const size_t end = (size_t)(c.ref + c.size);
			if (ref == end) {
				if (isMerged) {
					c.size += m_freeSpace[n].size;
					m_freeSpace.DeleteRow(n);
				}
				else c.size += size;

				return;
			}
		}
	}

	// Else just add to freelist
	if (!isMerged) m_freeSpace.Add(ref, size);
}

MemRef SlabAlloc::ReAlloc(size_t ref, MemRef::Header* header, size_t size, bool doCopy=true) {
	//TODO: Check if we can extend current space 
    //MP: Not reviewed (not unit tested)

	// Allocate new space
	const MemRef space = Alloc(size);
	if (!space.pointer) return space;

	if (doCopy) {
		// Get size of old segment
		const size_t oldsize = header->length;

		// Copy existing segment
		memcpy(space.pointer, header, oldsize);

		// Add old segment to freelist
		Free(ref, header);
	}

	return space;
}

void* SlabAlloc::Translate(size_t ref) const {
    //MP: Not reviewed - m_baseline arbitrary and m_shared is undefined
	if (ref < m_baseline) return m_shared + ref;
	else {
		const size_t ndx = m_slabs.offset.FindPos(ref);
		assert(ndx != -1);

		const size_t offset = ndx ? m_slabs[ndx-1].offset : m_baseline;
		return (char*)(intptr_t)m_slabs[ndx].pointer + (ref - offset);
	}
}

#ifdef _DEBUG

bool SlabAlloc::IsAllFree() const {
	if (m_freeSpace.GetSize() != m_slabs.GetSize()) return false;

	// Verify that free space matches slabs
	size_t ref = m_baseline;
	for (size_t i = 0; i < m_slabs.GetSize(); ++i) {
		const Slabs::Cursor c = m_slabs[i];
		const size_t size = (size_t)(c.offset - ref);

		const size_t r = m_freeSpace.ref.Find(ref);
		if (r == (size_t)-1) return false;
		if (size != (size_t)m_freeSpace[r].size) return false;

		ref = (size_t)c.offset;
	}
	return true;
}

void SlabAlloc::Verify() const {
	// Make sure that all free blocks fit within a slab
	for (size_t i = 0; i < m_freeSpace.GetSize(); ++i) {
		const FreeSpace::Cursor c = m_freeSpace[i];
		const size_t ref = (size_t)c.ref;

		const size_t ndx = m_slabs.offset.FindPos(ref);
		assert(ndx != -1);

		const size_t slab_end = (size_t)m_slabs[ndx].offset;
		const size_t free_end = (size_t)(ref + c.size);

		assert(free_end <= slab_end);
	}
}

#endif //_DEBUG
