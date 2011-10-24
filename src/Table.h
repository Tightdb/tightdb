#ifndef __TDB_TABLE__
#define __TDB_TABLE__

#include <cstring> // strcmp()
#include <time.h>
#include "Column.h"
#include "alloc.h"
#include "ColumnType.h"

class Accessor;
class TableView;
class Group;

class Table {
public:
	Table(Allocator& alloc=DefaultAllocator);
	Table(const Table& t);
	~Table();

	// Column meta info
	size_t GetColumnCount() const;
	const char* GetColumnName(size_t ndx) const;
	size_t GetColumnIndex(const char* name) const;
	ColumnType GetColumnType(size_t ndx) const;

	Table& operator=(const Table& t);

	bool IsEmpty() const {return m_size == 0;}
	size_t GetSize() const {return m_size;}

	size_t AddRow();
	void Clear();
	void DeleteRow(size_t ndx);
	void PopBack() {if (!IsEmpty()) DeleteRow(m_size-1);}

	// Adaptive ints
	int Get(size_t column_id, size_t ndx) const;
	void Set(size_t column_id, size_t ndx, int value);
	int64_t Get64(size_t column_id, size_t ndx) const;
	void Set64(size_t column_id, size_t ndx, int64_t value);
	bool GetBool(size_t column_id, size_t ndx) const;
	void SetBool(size_t column_id, size_t ndx, bool value);
	time_t GetDate(size_t column_id, size_t ndx) const;
	void SetDate(size_t column_id, size_t ndx, time_t value);

	// NOTE: Low-level insert functions. Always insert in all columns at once
	// and call InsertDone after to avoid table getting un-balanced.
	void InsertInt(size_t column_id, size_t ndx, int value);
	void InsertInt(size_t column_id, size_t ndx, int64_t value);
	void InsertBool(size_t column_id, size_t ndx, bool value) {InsertInt(column_id, ndx, value ? 1 :0);}
	void InsertDate(size_t column_id, size_t ndx, time_t value) {InsertInt(column_id, ndx, (int64_t)value);}
	template<class T> void InsertEnum(size_t column_id, size_t ndx, T value) {
		InsertInt(column_id, ndx, (int)value);
	}
	void InsertString(size_t column_id, size_t ndx, const char* value);
	void InsertDone();


	// Strings
	const char* GetString(size_t column_id, size_t ndx) const;
	void SetString(size_t column_id, size_t ndx, const char* value);
	
	size_t RegisterColumn(ColumnType type, const char* name);

	Column& GetColumn(size_t ndx);
	const Column& GetColumn(size_t ndx) const;
	AdaptiveStringColumn& GetColumnString(size_t ndx);
	const AdaptiveStringColumn& GetColumnString(size_t ndx) const;

	// Searching
	size_t Find(size_t column_id, int64_t value) const;
	size_t FindBool(size_t column_id, bool value) const;
	size_t FindString(size_t column_id, const char* value) const;
	size_t FindDate(size_t column_id, time_t value) const;
	void FindAll(TableView& tv, size_t column_id, int64_t value);
	void FindAllHamming(TableView& tv, size_t column_id, uint64_t value, size_t max);

	// Indexing
	bool HasIndex(size_t column_id) const;
	void SetIndex(size_t column_id);

	// Debug
#ifdef _DEBUG
	bool Compare(const Table& c) const;
	void Verify() const;
	void ToDot(const char* filename) const;
	void Print() const;
#endif //_DEBUG

protected:
	friend class Group;

	// Construct from ref
	Table(Allocator& alloc, size_t ref, Array* parent, size_t pndx);
	void SetParent(Array* parent, size_t pndx);
	size_t GetRef() const;
	void Invalidate() {m_top.Invalidate();}

	// Serialization
	size_t Write(std::ostream& out, size_t& pos) const;
	static Table LoadFromFile(const char* path);

	ColumnBase& GetColumnBase(size_t ndx);
	const ColumnBase& GetColumnBase(size_t ndx) const;

	// Member variables
	size_t m_size;
	
	// On-disk format
	Array m_top;
	Array m_spec;
	Array m_columns;
	ArrayString m_columnNames;

	// Cached columns
	Array m_cols;
	Allocator& m_alloc;
};

class TableView {
public:
	TableView(Table& source);
	TableView(const TableView& v);

	Table& GetParent() {return m_table;}
	Column& GetRefColumn() {return m_refs;}
	size_t GetRef(size_t ndx) const {return m_refs.Get(ndx);}

	bool IsEmpty() const {return m_refs.IsEmpty();}
	size_t GetSize() const {return m_refs.Size();}

	// Getting values
	int Get(size_t column_id, size_t ndx) const;
	int64_t Get64(size_t column_id, size_t ndx) const;
	bool GetBool(size_t column_id, size_t ndx) const;
	time_t GetDate(size_t column_id, size_t ndx) const;
	const char* GetString(size_t column_id, size_t ndx) const;

	// Setting values
	void Set(size_t column_id, size_t ndx, int value);
	void Set64(size_t column_id, size_t ndx, int64_t value);
	void SetBool(size_t column_id, size_t ndx, bool value);
	void SetDate(size_t column_id, size_t ndx, time_t value);
	void SetString(size_t column_id, size_t ndx, const char* value);

private:
	// Don't allow copying
	TableView& operator=(const TableView&) {return *this;}

	Table& m_table;
	Column m_refs;
};


class CursorBase {
public:
	CursorBase(Table& table, size_t ndx) : m_table(table), m_index(ndx) {};
	CursorBase(const CursorBase& v) : m_table(v.m_table), m_index(v.m_index) {};
	CursorBase& operator=(const CursorBase& v) {m_table = v.m_table; m_index = v.m_index; return *this;}

protected:
	Table& m_table;
	size_t m_index;
	friend class Accessor;
};

class Accessor {
public:
	Accessor() {};
	void Create(CursorBase* cursor, size_t column_ndx) {m_cursor = cursor; m_column = column_ndx;}
	static const ColumnType type;

protected:
	int Get() const {return m_cursor->m_table.Get(m_column, m_cursor->m_index);}
	void Set(int value) {m_cursor->m_table.Set(m_column, m_cursor->m_index, value);}
	int64_t Get64() const {return m_cursor->m_table.Get64(m_column, m_cursor->m_index);}
	void Set64(int64_t value) {m_cursor->m_table.Set64(m_column, m_cursor->m_index, value);}
	bool GetBool() const {return m_cursor->m_table.GetBool(m_column, m_cursor->m_index);}
	void SetBool(bool value) {m_cursor->m_table.SetBool(m_column, m_cursor->m_index, value);}
	time_t GetDate() const {return m_cursor->m_table.GetDate(m_column, m_cursor->m_index);}
	void SetDate(time_t value) {m_cursor->m_table.SetDate(m_column, m_cursor->m_index, value);}

	const char* GetString() const {return m_cursor->m_table.GetString(m_column, m_cursor->m_index);}
	void SetString(const char* value) {m_cursor->m_table.SetString(m_column, m_cursor->m_index, value);}

	CursorBase* m_cursor;
	size_t m_column;
};

class AccessorInt : public Accessor {
public:
	operator int64_t() const {return Get64();}
	//void operator=(int32_t value) {Set(value);}
	//void operator=(uint32_t value) {Set(value);}
	void operator=(int64_t value) {Set64(value);}
	//void operator=(uint64_t value) {Set64(value);}
	//void operator+=(int32_t value) {Set(Get()+value);}
	//void operator+=(uint32_t value) {Set(Get()+value);}
	void operator+=(int64_t value) {Set64(Get64()+value);}
	//void operator+=(uint64_t value) {Set64(Get64()+value);}
};

class AccessorBool : public Accessor {
public:
	operator bool() const {return GetBool();}
	void operator=(bool value) {SetBool(value);}
	void Flip() {Set(Get() != 0 ? 0 : 1);}
	static const ColumnType type;
};

template<class T> class AccessorEnum : public Accessor {
public:
	operator T() const {return (T)Get();}
	void operator=(T value) {Set((int)value);}
};

class AccessorString : public Accessor {
public:
	operator const char*() const {return GetString();}
	void operator=(const char* value) {SetString(value);}
	bool operator==(const char* value) {return (strcmp(GetString(), value) == 0);}
	static const ColumnType type;
};

class AccessorDate : public Accessor {
public:
	operator time_t() const {return GetDate();}
	void operator=(time_t value) {SetDate(value);}
	static const ColumnType type;
};

class ColumnProxy {
public:
	ColumnProxy() {}
	void Create(Table* table, size_t column) {
		m_table = table;
		m_column = column;
	}
protected:
	Table* m_table;
	size_t m_column;
};

class ColumnProxyInt : public ColumnProxy {
public:
	//size_t Find(int32_t value) const {return m_table->Find(m_column, (int64_t)value);}
	//size_t Find(uint32_t value) const {return m_table->Find(m_column, (int64_t)value);}
	size_t Find(int64_t value) const {return m_table->Find(m_column, value);}
	//size_t Find(uint64_t value) const {return m_table->Find(m_column, (int64_t)value);}
	size_t FindPos(int64_t value) const {return m_table->GetColumn(m_column).FindPos(value);}
	TableView FindAll(int value) {TableView tv(*m_table); m_table->FindAll(tv, m_column, value); return tv;}
	TableView FindAllHamming(uint64_t value, size_t max) {TableView tv(*m_table); m_table->FindAllHamming(tv, m_column, value, max); return tv;}
	int operator+=(int value) {m_table->GetColumn(m_column).Increment64(value); return 0;}
};

class ColumnProxyBool : public ColumnProxy {
public:
	size_t Find(bool value) const {return m_table->FindBool(m_column, value);}
};

class ColumnProxyDate : public ColumnProxy {
public:
	size_t Find(time_t value) const {return m_table->FindDate(m_column, value);}
};

template<class T> class ColumnProxyEnum : public ColumnProxy {
public:
	size_t Find(T value) const {return m_table->Find(m_column, (int64_t)value);}
};

class ColumnProxyString : public ColumnProxy {
public:
	size_t Find(const char* value) const {return m_table->FindString(m_column, value);}
	//void Stats() const {m_table->GetColumnString(m_column).Stats();}
};

template<class T> class TypeEnum {
public:
	TypeEnum(T v) : m_value(v) {};
	operator T() const {return m_value;}
	TypeEnum<T>& operator=(const TypeEnum<T>& v) {m_value = v.m_value;}
private:
	const T m_value;
};
#define TypeInt int64_t
#define TypeBool bool
#define TypeString const char*

// Make all enum types return int type
template<typename T> struct COLUMN_TYPE_Enum {
public:
	COLUMN_TYPE_Enum() {};
	operator ColumnType() const {return COLUMN_TYPE_INT;}
};

class QueryItem {
public:
	QueryItem operator&&(const QueryItem&) {return QueryItem();}
	QueryItem operator||(const QueryItem&) {return QueryItem();}
};

class QueryAccessorBool {
public:
	QueryItem operator==(int) {return QueryItem();}
	QueryItem operator!=(int) {return QueryItem();}
};

class QueryAccessorInt {
public:
	QueryItem operator==(int) {return QueryItem();}
	QueryItem operator!=(int) {return QueryItem();}
	QueryItem operator<(int) {return QueryItem();}
	QueryItem operator>(int) {return QueryItem();}
	QueryItem operator<=(int) {return QueryItem();}
	QueryItem operator>=(int) {return QueryItem();}
	QueryItem Between(int, int) {return QueryItem();}
};

class QueryAccessorString {
public:
	QueryItem operator==(const char*) {return QueryItem();}
	QueryItem operator!=(const char*) {return QueryItem();}
	QueryItem Contains(const char*) {return QueryItem();}
	QueryItem StartsWith(const char*) {return QueryItem();}
	QueryItem EndsWith(const char*) {return QueryItem();}
	QueryItem MatchRegEx(const char*) {return QueryItem();}
};

template<class T> class QueryAccessorEnum {
public:
	QueryItem operator==(T) {return QueryItem();}
	QueryItem operator!=(T) {return QueryItem();}
	QueryItem operator<(T) {return QueryItem();}
	QueryItem operator>(T) {return QueryItem();}
	QueryItem operator<=(T) {return QueryItem();}
	QueryItem operator>=(T) {return QueryItem();}
	QueryItem between(T, T) {return QueryItem();}
};

#endif //__TDB_TABLE__
