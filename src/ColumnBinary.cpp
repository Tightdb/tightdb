#include "ColumnBinary.h"

// Pre-declare local functions
bool IsNodeFromRef(size_t ref, Allocator& alloc);

bool IsNodeFromRef(size_t ref, Allocator& alloc) {
	const uint8_t* const header = (uint8_t*)alloc.Translate(ref);
	const bool isNode = (header[0] & 0x80) != 0;

	return isNode;
}

ColumnBinary::ColumnBinary(Allocator& alloc) {
	m_array = new ArrayBinary(NULL, 0, alloc);
}

ColumnBinary::ColumnBinary(size_t ref, ArrayParent *parent, size_t pndx, Allocator& alloc) {
	const bool isNode = IsNodeFromRef(ref, alloc);
	if (isNode) {
		m_array = new Array(ref, parent, pndx, alloc);
	}
	else {
		m_array = new ArrayBinary(ref, parent, pndx, alloc);
	}
}

ColumnBinary::ColumnBinary(size_t ref, const ArrayParent *parent, size_t pndx, Allocator& alloc) {
	const bool isNode = IsNodeFromRef(ref, alloc);
	if (isNode) {
		m_array = new Array(ref, parent, pndx, alloc);
	}
	else {
		m_array = new ArrayBinary(ref, parent, pndx, alloc);
	}
}

ColumnBinary::~ColumnBinary() {
	if (IsNode()) delete m_array;
	else delete (ArrayBinary*)m_array;
}

void ColumnBinary::Destroy() {
	if (IsNode()) m_array->Destroy();
	else ((ArrayBinary*)m_array)->Destroy();
}

void ColumnBinary::UpdateRef(size_t ref) {
	assert(IsNodeFromRef(ref, m_array->GetAllocator())); // Can only be called when creating node

	if (IsNode()) m_array->UpdateRef(ref);
	else {
		ArrayParent *const parent = m_array->GetParent();
		const size_t pndx   = m_array->GetParentNdx();

		// Replace the string array with int array for node
		Array* array = new Array(ref, parent, pndx, m_array->GetAllocator());
		delete m_array;
		m_array = array;

		// Update ref in parent
		if (parent) parent->update_child_ref(pndx, ref);
	}
}

bool ColumnBinary::IsEmpty() const {
	if (IsNode()) {
		const Array offsets = NodeGetOffsets();
		return offsets.IsEmpty();
	}
	else {
		return ((ArrayBinary*)m_array)->IsEmpty();
	}
}

size_t ColumnBinary::Size() const {
	if (IsNode())  {
		const Array offsets = NodeGetOffsets();
		const size_t size = offsets.IsEmpty() ? 0 : (size_t)offsets.Back();
		return size;
	}
	else {
		return ((ArrayBinary*)m_array)->Size();
	}
}

void ColumnBinary::Clear() {
	if (m_array->IsNode()) {
		ArrayParent *const parent = m_array->GetParent();
		const size_t pndx = m_array->GetParentNdx();

		// Revert to binary array
		ArrayBinary* const array = new ArrayBinary(parent, pndx, m_array->GetAllocator());
		if (parent) parent->update_child_ref(pndx, array->GetRef());

		// Remove original node
		m_array->Destroy();
		delete m_array;

		m_array = array;
	}
	else ((ArrayBinary*)m_array)->Clear();
}

BinaryData ColumnBinary::Get(size_t ndx) const {
	assert(ndx < Size());
	return TreeGet<BinaryData,ColumnBinary>(ndx);
}

const void* ColumnBinary::GetData(size_t ndx) const {
	assert(ndx < Size());
	const BinaryData bin = TreeGet<BinaryData,ColumnBinary>(ndx);
	return bin.pointer;
}

size_t ColumnBinary::GetLen(size_t ndx) const {
	assert(ndx < Size());
	const BinaryData bin = TreeGet<BinaryData,ColumnBinary>(ndx);
	return bin.len;
}

void ColumnBinary::Set(size_t ndx, const void* value, size_t len) {
	assert(ndx < Size());
	const BinaryData bin = {value, len};
	Set(ndx, bin);
}

bool ColumnBinary::Set(size_t ndx, BinaryData bin) {
	assert(ndx < Size());
	return TreeSet<BinaryData,ColumnBinary>(ndx, bin);
}

void ColumnBinary::Add(const void* value, size_t len) {
	Insert(Size(), value, len);
}

bool ColumnBinary::Add(BinaryData bin) {
	return Insert(Size(), bin);
}

void ColumnBinary::Insert(size_t ndx, const void* value, size_t len) {
	assert(ndx <= Size());
	const BinaryData bin = {value, len};
	Insert(ndx, bin);
}

bool ColumnBinary::Insert(size_t ndx, BinaryData bin) {
	assert(ndx <= Size());
	return TreeInsert<BinaryData,ColumnBinary>(ndx, bin);
}

void ColumnBinary::Delete(size_t ndx) {
	assert(ndx < Size());
	TreeDelete<BinaryData,ColumnBinary>(ndx);
}

void ColumnBinary::Resize(size_t ndx) {
	assert(!IsNode()); // currently only available on leaf level (used by b-tree code)
	assert(ndx < Size());
	((ArrayBinary*)m_array)->Resize(ndx);
}

BinaryData ColumnBinary::LeafGet(size_t ndx) const {
	const ArrayBinary* const array = (ArrayBinary*)m_array;
	const BinaryData bin = {array->Get(ndx), array->GetLen(ndx)};
	return bin;
}

bool ColumnBinary::LeafSet(size_t ndx, BinaryData value) {
	((ArrayBinary*)m_array)->Set(ndx, value.pointer, value.len);
	return true;
}

bool ColumnBinary::LeafInsert(size_t ndx, BinaryData value) {
	((ArrayBinary*)m_array)->Insert(ndx, value.pointer, value.len);
	return true;
}

void ColumnBinary::LeafDelete(size_t ndx) {
	((ArrayBinary*)m_array)->Delete(ndx);
}

#ifdef _DEBUG

void ColumnBinary::LeafToDot(std::ostream& out, const Array& array) const {
	// Rebuild array to get correct type
	const size_t ref = array.GetRef();
	const ArrayBinary binarray(ref, NULL, 0, array.GetAllocator());
	
	binarray.ToDot(out);
}

#endif //_DEBUG
