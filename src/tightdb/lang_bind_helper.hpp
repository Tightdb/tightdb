/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of TightDB Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to TightDB Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from TightDB Incorporated.
 *
 **************************************************************************/
#ifndef TIGHTDB_LANG_BIND_HELPER_HPP
#define TIGHTDB_LANG_BIND_HELPER_HPP

#include <cstddef>

#include <tightdb/table.hpp>
#include <tightdb/table_view.hpp>
#include <tightdb/group.hpp>

namespace tightdb {


/// These functions are only to be used by language bindings to gain
/// access to certain memebers that are othewise private.
///
/// \note Applications are not supposed to call any of these functions
/// directly.
///
/// All the get_*_ptr() functions as well as new_table() and
/// copy_table() will return a pointer to a Table whose reference
/// count has already been incremented.
///
/// The application must make sure that the unbind_table_ref() function is
/// called to decrement the reference count when it no longer needs
/// access to that table.
class LangBindHelper {
public:
    /// Construct a new freestanding table.
    static Table* new_table();

    /// Construct a new freestanding table as a copy of the specified
    /// one.
    static Table* copy_table(const Table&);

    static Table* get_subtable_ptr(Table*, std::size_t column_ndx, std::size_t row_ndx);
    static const Table* get_subtable_ptr(const Table*, std::size_t column_ndx,
                                         std::size_t row_ndx);

    // FIXME: This is an 'oddball', do we really need it? If we do,
    // please provide a comment that explains why it is needed!
    static Table* get_subtable_ptr_during_insert(Table*, std::size_t col_ndx,
                                                 std::size_t row_ndx);

    static Table* get_subtable_ptr(TableView*, std::size_t column_ndx, std::size_t row_ndx);
    static const Table* get_subtable_ptr(const TableView*, std::size_t column_ndx,
                                         std::size_t row_ndx);
    static const Table* get_subtable_ptr(const ConstTableView*, std::size_t column_ndx,
                                         std::size_t row_ndx);

    static Table* get_table_ptr(Group* grp, StringData name);
    static Table* get_table_ptr(Group* grp, StringData name, bool& was_created);
    static const Table* get_table_ptr(const Group* grp, StringData name);

    static void unbind_table_ref(const Table*);
    static void bind_table_ref(const Table*) TIGHTDB_NOEXCEPT;

    /// Calls parent.insert_subtable(col_ndx, row_ndx, &source). Note
    /// that the source table must have a spec that is compatible with
    /// the target subtable column.
    static void insert_subtable(Table& parent, std::size_t col_ndx, std::size_t row_ndx,
                                const Table& source);


    /// Calls parent.insert_mixed_subtable(col_ndx, row_ndx, &source).
    static void insert_mixed_subtable(Table& parent, std::size_t col_ndx, std::size_t row_ndx,
                                      const Table& source);

    /// Calls parent.set_mixed_subtable(col_ndx, row_ndx, &source).
    static void set_mixed_subtable(Table& parent, std::size_t col_ndx, std::size_t row_ndx,
                                   const Table& source);

    /// This is an alternative to Table::get_spec() that may be
    /// legally called even for a table with shared spec. It is then
    /// the responsibility of the language binding to ensure that
    /// modification is only done through it when it is not shared.
    static Spec& get_spec(Table&) TIGHTDB_NOEXCEPT;

    /// Returns the name of the spaceified data type as follows:
    ///
    /// <pre>
    ///
    ///   type_Int     ->  "int"
    ///   type_Bool    ->  "bool"
    ///   type_Float   ->  "float"
    ///   type_Double  ->  "double"
    ///   type_String  ->  "string"
    ///   type_Binary  ->  "binary"
    ///   type_Date    ->  "date"
    ///   type_Table   ->  "table"
    ///   type_Mixed   ->  "mixed"
    ///
    /// </pre>
    static const char* get_data_type_name(DataType) TIGHTDB_NOEXCEPT;
};


// Implementation:

inline Table* LangBindHelper::new_table()
{
    Allocator& alloc = Allocator::get_default();
    std::size_t ref = Table::create_empty_table(alloc); // Throws
    Table* const table = new Table(Table::RefCountTag(), alloc, ref, 0, 0); // Throws
    table->bind_ref();
    return table;
}

inline Table* LangBindHelper::copy_table(const Table& t)
{
    Allocator& alloc = Allocator::get_default();
    std::size_t ref = t.clone(alloc); // Throws
    Table* const table = new Table(Table::RefCountTag(), alloc, ref, 0, 0); // Throws
    table->bind_ref();
    return table;
}

inline Table* LangBindHelper::get_subtable_ptr(Table* t, std::size_t column_ndx,
                                               std::size_t row_ndx)
{
    Table* subtab = t->get_subtable_ptr(column_ndx, row_ndx);
    subtab->bind_ref();
    return subtab;
}

inline const Table* LangBindHelper::get_subtable_ptr(const Table* t, std::size_t column_ndx,
                                                     std::size_t row_ndx)
{
    const Table* subtab = t->get_subtable_ptr(column_ndx, row_ndx);
    subtab->bind_ref();
    return subtab;
}

inline Table* LangBindHelper::get_subtable_ptr(TableView* tv, std::size_t column_ndx,
                                               std::size_t row_ndx)
{
    return get_subtable_ptr(&tv->get_parent(), column_ndx, tv->get_source_ndx(row_ndx));
}

inline const Table* LangBindHelper::get_subtable_ptr(const TableView* tv, std::size_t column_ndx,
                                                     std::size_t row_ndx)
{
    return get_subtable_ptr(&tv->get_parent(), column_ndx, tv->get_source_ndx(row_ndx));
}

inline const Table* LangBindHelper::get_subtable_ptr(const ConstTableView* tv,
                                                     std::size_t column_ndx, std::size_t row_ndx)
{
    return get_subtable_ptr(&tv->get_parent(), column_ndx, tv->get_source_ndx(row_ndx));
}

inline Table* LangBindHelper::get_table_ptr(Group* grp, StringData name)
{
    Table* subtab = grp->get_table_ptr(name);
    subtab->bind_ref();
    return subtab;
}

inline Table* LangBindHelper::get_table_ptr(Group* grp, StringData name, bool& was_created)
{
    Table* subtab = grp->get_table_ptr(name, was_created);
    subtab->bind_ref();
    return subtab;
}

inline const Table* LangBindHelper::get_table_ptr(const Group* grp, StringData name)
{
    const Table* subtab = grp->get_table_ptr(name);
    subtab->bind_ref();
    return subtab;
}

inline void LangBindHelper::unbind_table_ref(const Table* t)
{
   t->unbind_ref();
}

inline void LangBindHelper::bind_table_ref(const Table* t) TIGHTDB_NOEXCEPT
{
   t->bind_ref();
}

inline void LangBindHelper::insert_subtable(Table& parent, std::size_t col_ndx,
                                            std::size_t row_ndx, const Table& source)
{
    parent.insert_subtable(col_ndx, row_ndx, &source);
}


inline void LangBindHelper::insert_mixed_subtable(Table& parent, std::size_t col_ndx,
                                                  std::size_t row_ndx, const Table& source)
{
    parent.insert_mixed_subtable(col_ndx, row_ndx, &source);
}

inline void LangBindHelper::set_mixed_subtable(Table& parent, std::size_t col_ndx,
                                               std::size_t row_ndx, const Table& source)
{
    parent.set_mixed_subtable(col_ndx, row_ndx, &source);
}

inline Spec& LangBindHelper::get_spec(Table& t) TIGHTDB_NOEXCEPT
{
    return t.m_spec_set;
}


} // namespace tightdb

#endif // TIGHTDB_LANG_BIND_HELPER_HPP
