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
#ifndef TIGHTDB_TABLE_ACCESSORS_HPP
#define TIGHTDB_TABLE_ACCESSORS_HPP

#include <cstring>
#include <utility>

#include <tightdb/mixed.hpp>
#include <tightdb/table.hpp>

namespace tightdb {


/// A convenience base class for Spec classes that are to be used with
/// BasicTable.
///
/// There are two reasons why you might want to derive your spec class
/// from this one. First, it offers short hand names for each of the
/// available column types. Second, it makes it easier when you do not
/// want to specify colum names or convenience methods, since suitable
/// fallbacks are defined here.
struct SpecBase {
    typedef int64_t             Int;
    typedef bool                Bool;
    typedef tightdb::Date       Date;
    typedef const char*         String;
    typedef tightdb::BinaryData Binary;
    typedef tightdb::Mixed      Mixed;

    template<class E> class Enum {
    public:
        typedef E enum_type;
        Enum(E v) : m_value(v) {};
        operator E() const { return m_value; }
    private:
        E m_value;
    };

    template<class T> class Subtable {
    public:
        typedef T table_type;
        Subtable(T* t) : m_table(t) {};
        operator T*() const { return m_table; }
    private:
        T* m_table;
    };

    /// By default, there are no static column names defined for a
    /// BasicTable. One may define a set of column mames as follows:
    ///
    /// \code{.cpp}
    ///
    ///   struct MyTableSpec: SpecBase {
    ///     typedef TypeAppend<void, int>::type Columns1;
    ///     typedef TypeAppend<Columns1, bool>::type Columns;
    ///
    ///     template<template<int> class Col, class Init> struct ColNames {
    ///       typename Col<0>::type foo;
    ///       typename Col<1>::type bar;
    ///       ColNames(Init i) TIGHTDB_NOEXCEPT: foo(i), bar(i) {}
    ///     };
    ///   };
    ///
    /// \endcode
    ///
    /// Note that 'i' in Col<i> links the name that you specify to a
    /// particular column index. You may specify the column names in
    /// any order. Multiple names may refer to the same column, and
    /// you do not have to specify a name for every column.
    template<template<int> class Col, class Init> struct ColNames {
        ColNames(Init) TIGHTDB_NOEXCEPT {}
    };

    /// FIXME: Currently we do not support absence of dynamic column
    /// names.
    static const char* const* dyn_col_names() { return 0; }

    /// This is the fallback class that is used when no convenience
    /// methods are specified in the users Spec class.
    ///
    /// If you would like to add a more convenient add() method, here
    /// is how you could do it:
    ///
    /// \code{.cpp}
    ///
    ///   struct MyTableSpec: SpecBase {
    ///     typedef tightdb::TypeAppend<void, int>::type Columns1;
    ///     typedef tightdb::TypeAppend<Columns1, bool>::type Columns;
    ///
    ///     struct ConvenienceMethods {
    ///       void add(int foo, bool bar)
    ///       {
    ///         BasicTable<MyTableSpec>* const t = static_cast<BasicTable<MyTableSpec>*>(this);
    ///         t->add((tuple(), name1, name2));
    ///       }
    ///     };
    ///   };
    ///
    /// \endcode
    ///
    /// FIXME: Note: Users ConvenienceMethods may not contain any
    /// virtual methods, nor may it contain any data memebers. We
    /// might want to check this by
    /// TIGHTDB_STATIC_ASSERT(sizeof(Derivative of ConvenienceMethods)
    /// == 1)), however, this would not be guaranteed by the standard,
    /// since even an empty class may add to the size of the derived
    /// class. Fortunately, as long as ConvenienceMethods is derived
    /// from, by BasicTable, after deriving from Table, this cannot
    /// become a problem, nor would it lead to a violation of the
    /// strict aliasing rule of C++03 or C++11.
    struct ConvenienceMethods {};
};


template<class> class BasicTable;
template<class> class BasicTableView;


namespace _impl {


/// Get the const qualified type of the table being accessed.
///
/// If T matches 'BasicTableView<T2>' or 'const BasicTableView<T2>',
/// then return T2, else simply return T.
template<class Tab> struct GetTableFromView { typedef Tab type; };
template<class Tab> struct GetTableFromView<BasicTableView<Tab> > { typedef Tab type; };
template<class Tab> struct GetTableFromView<const BasicTableView<Tab> > { typedef Tab type; };


/// Determine whether an accessor has const-only access to a table, so
/// that it is not allowed to modify fields, nor return non-const
/// subtable references.
///
/// Note that for Taboid = 'BasicTableView<const Tab>', a column
/// accessor is still allowed to reorder the rows of the view, as long
/// as it does not modify the contents of the table.
template<class Taboid> struct TableIsConst { static const bool value = false; };
template<class Taboid> struct TableIsConst<const Taboid> { static const bool value = true; };
template<class Tab> struct TableIsConst<BasicTableView<const Tab> > {
    static const bool value = true;
};



/// This class gives access to a field of a row of a table, or a table
/// view.
///
/// \tparam Taboid Either a table or a table view, that is, any of
/// 'BasicTable<S>', 'const BasicTable<S>',
/// 'BasicTableView<BasicTable<S> >', 'const
/// BasicTableView<BasicTable<S> >', 'BasicTableView<const
/// BasicTable<S> >', or 'const BasicTableView<const BasicTable<S>
/// >'. Note that the term 'taboid' is used here for something that is
/// table-like, i.e., either a table of a table view.
///
/// \tparam const_tab Indicates whether the accessor has const-only
/// access to the field, that is, if, and only if Taboid matches
/// 'const T' or 'BasicTableView<const T>' for any T.
template<class Taboid, int col_idx, class Type, bool const_tab> class FieldAccessor;


/// Commmon base class for all field accessor specializations.
template<class Taboid> class FieldAccessorBase {
protected:
    typedef std::pair<Taboid*, std::size_t> Init;
    Taboid* const m_table;
    const std::size_t m_row_idx;
    FieldAccessorBase(Init i) TIGHTDB_NOEXCEPT: m_table(i.first), m_row_idx(i.second) {}
};


/// Field accessor specialization for integers.
template<class Taboid, int col_idx, bool const_tab>
class FieldAccessor<Taboid, col_idx, int64_t, const_tab>: public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;

public:
    int64_t get() const TIGHTDB_NOEXCEPT
    {
        return Base::m_table->get_impl()->get_int(col_idx, Base::m_row_idx);
    }

    void set(int64_t value) const
    {
        Base::m_table->get_impl()->set_int(col_idx, Base::m_row_idx, value);
    }

    operator int64_t() const TIGHTDB_NOEXCEPT { return get(); }
    const FieldAccessor& operator=(int64_t value) const { set(value); return *this; }

    const FieldAccessor& operator+=(int64_t value) const
    {
        // FIXME: Should be optimized (can be both optimized and
        // generalized by using a form of expression templates).
        set(get() + value);
        return *this;
    }

    const FieldAccessor& operator-=(int64_t value) const
    {
        // FIXME: Should be optimized (can be both optimized and
        // generalized by using a form of expression templates).
        set(get() - value);
        return *this;
    }

    const FieldAccessor& operator++() const { return *this += 1; }
    const FieldAccessor& operator--() const { return *this -= 1; }

    int64_t operator++(int) const
    {
        // FIXME: Should be optimized (can be both optimized and
        // generalized by using a form of expression templates).
        const int64_t value = get();
        set(value + 1);
        return value;
    }

    int64_t operator--(int) const
    {
        // FIXME: Should be optimized (can be both optimized and
        // generalized by using a form of expression templates).
        const int64_t value = get();
        set(value - 1);
        return value;
    }


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for booleans.
template<class Taboid, int col_idx, bool const_tab>
class FieldAccessor<Taboid, col_idx, bool, const_tab>: public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;

public:
    bool get() const TIGHTDB_NOEXCEPT
    {
        return Base::m_table->get_impl()->get_bool(col_idx, Base::m_row_idx);
    }

    void set(bool value) const
    {
        Base::m_table->get_impl()->set_bool(col_idx, Base::m_row_idx, value);
    }

    operator bool() const TIGHTDB_NOEXCEPT { return get(); }
    const FieldAccessor& operator=(bool value) const { set(value); return *this; }


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for enumerations.
template<class Taboid, int col_idx, class E, bool const_tab>
class FieldAccessor<Taboid, col_idx, SpecBase::Enum<E>, const_tab>:
    public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;

public:
    E get() const TIGHTDB_NOEXCEPT
    {
        return static_cast<E>(Base::m_table->get_impl()->get_int(col_idx, Base::m_row_idx));
    }

    void set(E value) const
    {
        Base::m_table->get_impl()->set_int(col_idx, Base::m_row_idx, value);
    }

    operator E() const TIGHTDB_NOEXCEPT { return get(); }
    const FieldAccessor& operator=(E value) const { set(value); return *this; }


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for dates.
template<class Taboid, int col_idx, bool const_tab>
class FieldAccessor<Taboid, col_idx, Date, const_tab>: public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;

public:
    std::time_t get() const TIGHTDB_NOEXCEPT
    {
        return Base::m_table->get_impl()->get_date(col_idx, Base::m_row_idx);
    }

    void set(const std::time_t& value) const
    {
        Base::m_table->get_impl()->set_date(col_idx, Base::m_row_idx, value);
    }

    operator std::time_t() const TIGHTDB_NOEXCEPT { return get(); }
    const FieldAccessor& operator=(const std::time_t& value) const { set(value); return *this; }


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for strings.
template<class Taboid, int col_idx, bool const_tab>
class FieldAccessor<Taboid, col_idx, const char*, const_tab>: public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;

public:
    const char* get() const TIGHTDB_NOEXCEPT
    {
        return Base::m_table->get_impl()->get_string(col_idx, Base::m_row_idx);
    }

    void set(const char* value) const
    {
        Base::m_table->get_impl()->set_string(col_idx, Base::m_row_idx, value);
    }

    operator const char*() const TIGHTDB_NOEXCEPT { return get(); }
    const FieldAccessor& operator=(const char* value) const { set(value); return *this; }

    friend bool operator==(const FieldAccessor& a, const char* b) TIGHTDB_NOEXCEPT
    {
        return std::strcmp(a.get(), b) == 0;
    }

    friend bool operator!=(const FieldAccessor& a, const char* b) TIGHTDB_NOEXCEPT
    {
        return std::strcmp(a.get(), b) != 0;
    }

    friend bool operator==(const char* a, const FieldAccessor& b) TIGHTDB_NOEXCEPT
    {
        return std::strcmp(a, b.get()) == 0;
    }

    friend bool operator!=(const char* a, const FieldAccessor& b) TIGHTDB_NOEXCEPT
    {
        return std::strcmp(a, b.get()) != 0;
    }


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for binary data.
template<class Taboid, int col_idx, bool const_tab>
class FieldAccessor<Taboid, col_idx, BinaryData, const_tab>: public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;

public:
    BinaryData get() const // FIXME: Should be modified so it never throws
    {
        return Base::m_table->get_impl()->get_binary(col_idx, Base::m_row_idx);
    }

    void set(const BinaryData& value) const
    {
        Base::m_table->get_impl()->set_binary(col_idx, Base::m_row_idx, value.pointer, value.len);
    }

    operator BinaryData() const { return get(); } // FIXME: Should be modified so it never throws
    const FieldAccessor& operator=(const BinaryData& value) const { set(value); return *this; }

    friend bool operator==(const FieldAccessor& a, const BinaryData& b) // FIXME: Should be modified so it never throws
    {
        return a.get().compare_payload(b);
    }

    friend bool operator!=(const FieldAccessor& a, const BinaryData& b) // FIXME: Should be modified so it never throws
    {
        return !a.get().compare_payload(b);
    }

    friend bool operator==(const BinaryData& a, const FieldAccessor& b) // FIXME: Should be modified so it never throws
    {
        return a.compare_payload(b.get());
    }

    friend bool operator!=(const BinaryData& a, const FieldAccessor& b) // FIXME: Should be modified so it never throws
    {
        return !a.compare_payload(b.get());
    }

    const char* get_pointer() const { return get().pointer; } // FIXME: Should be modified so it never throws
    std::size_t get_len() const { return get().len; } // FIXME: Should be modified so it never throws


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for subtables of non-const parent.
template<class Taboid, int col_idx, class Subtab>
class FieldAccessor<Taboid, col_idx, SpecBase::Subtable<Subtab>, false>:
    public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;
    // FIXME: Dangerous slicing posibility as long as Cursor is same as RowAccessor.
    // FIXME: Accessors must not be publicly copyable. This requires that Spec::ColNames is made a friend of BasicTable.
    // FIXME: Need BasicTableView::Cursor and BasicTableView::ConstCursor if Cursors should exist at all.
    struct SubtabRowAccessor: Subtab::RowAccessor {
    public:
        SubtabRowAccessor(Subtab* subtab, std::size_t row_idx):
            Subtab::RowAccessor(std::make_pair(subtab, row_idx)),
            m_owner(subtab->get_table_ref()) {}

    private:
        typename Subtab::Ref const m_owner;
    };

public:
    operator typename Subtab::Ref() const
    {
        Subtab* subtab =
            Base::m_table->template get_subtable_ptr<Subtab>(col_idx, Base::m_row_idx);
        return subtab->get_table_ref();
    }

    operator typename Subtab::ConstRef() const
    {
        const Subtab* subtab =
            Base::m_table->template get_subtable_ptr<Subtab>(col_idx, Base::m_row_idx);
        return subtab->get_table_ref();
    }

    typename Subtab::Ref operator->() const
    {
        Subtab* subtab =
            Base::m_table->template get_subtable_ptr<Subtab>(col_idx, Base::m_row_idx);
        return subtab->get_table_ref();
    }

    SubtabRowAccessor operator[](std::size_t row_idx) const
    {
        Subtab* subtab =
            Base::m_table->template get_subtable_ptr<Subtab>(col_idx, Base::m_row_idx);
        return SubtabRowAccessor(subtab, row_idx);
    }


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for subtables of const parent.
template<class Taboid, int col_idx, class Subtab>
class FieldAccessor<Taboid, col_idx, SpecBase::Subtable<Subtab>, true>:
    public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;
    // FIXME: Dangerous slicing posibility as long as Cursor is same as RowAccessor.
    struct SubtabRowAccessor: Subtab::ConstRowAccessor {
    public:
        SubtabRowAccessor(const Subtab* subtab, std::size_t row_idx):
            Subtab::ConstRowAccessor(std::make_pair(subtab, row_idx)),
            m_owner(subtab->get_table_ref()) {}

    private:
        typename Subtab::ConstRef const m_owner;
    };

public:
    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}

    operator typename Subtab::ConstRef() const
    {
        const Subtab* subtab =
            Base::m_table->template get_subtable_ptr<Subtab>(col_idx, Base::m_row_idx);
        return subtab->get_table_ref();
    }

    typename Subtab::ConstRef operator->() const
    {
        const Subtab* subtab =
            Base::m_table->template get_subtable_ptr<Subtab>(col_idx, Base::m_row_idx);
        return subtab->get_table_ref();
    }

    SubtabRowAccessor operator[](std::size_t row_idx) const
    {
        const Subtab* subtab =
            Base::m_table->template get_subtable_ptr<Subtab>(col_idx, Base::m_row_idx);
        return SubtabRowAccessor(subtab, row_idx);
    }
};


/// Base for field accessor specializations for mixed type.
template<class Taboid, int col_idx, class FieldAccessor>
class MixedFieldAccessorBase: public FieldAccessorBase<Taboid> {
private:
    typedef FieldAccessorBase<Taboid> Base;

public:
    Mixed get() const // FIXME: Should be modified so it never throws
    {
        return Base::m_table->get_impl()->get_mixed(col_idx, Base::m_row_idx);
    }

    void set(const Mixed& value) const
    {
        Base::m_table->get_impl()->set_mixed(col_idx, Base::m_row_idx, value);
    }

    operator Mixed() const { return get(); } // FIXME: Should be modified so it never throws

    const FieldAccessor& operator=(const Mixed& value) const
    {
        set(value);
        return static_cast<FieldAccessor&>(*this);
    }

    ColumnType get_type() const TIGHTDB_NOEXCEPT
    {
        return Base::m_table->get_impl()->get_mixed_type(col_idx, Base::m_row_idx);
    }

    int64_t get_int() const { return get().get_int(); } // FIXME: Should be modified so it never throws

    bool get_bool() const { return get().get_bool(); } // FIXME: Should be modified so it never throws

    std::time_t get_date() const { return get().get_date(); } // FIXME: Should be modified so it never throws

    const char* get_string() const { return get().get_string(); } // FIXME: Should be modified so it never throws

    BinaryData get_binary() const { return get().get_binary(); } // FIXME: Should be modified so it never throws

    bool is_subtable() const TIGHTDB_NOEXCEPT { return get_type() == COLUMN_TYPE_TABLE; }

    /// Checks whether this value is a subtable of the specified type.
    ///
    /// FIXME: Consider deleting this function. It is mostly
    /// redundant, and it is inefficient if you want to also get a
    /// reference to the table, or if you want to check for multiple
    /// table types.
    template<class T> bool is_subtable() const
    {
        // FIXME: Conversion from TableRef to ConstTableRef is relatively expensive, or is it? Check whether it involves access to the reference count!
        const ConstTableRef t = static_cast<const FieldAccessor*>(this)->get_subtable();
        return t && T::matches_dynamic_spec(&t->get_spec());
    }

    /// Generally more efficient that get_subtable()->size().
    std::size_t get_subtable_size() const TIGHTDB_NOEXCEPT
    {
        return Base::m_table->get_impl()->get_subtable_size(col_idx, Base::m_row_idx);
    }

    template<class T> friend bool operator==(const FieldAccessor& a, const T& b) // FIXME: Should be modified so it never throws
    {
        return a.get() == b;
    }

    template<class T> friend bool operator!=(const FieldAccessor& a, const T& b) // FIXME: Should be modified so it never throws
    {
        return a.get() != b;
    }

    template<class T> friend bool operator==(const T& a, const FieldAccessor& b) // FIXME: Should be modified so it never throws
    {
        return a == b.get();
    }

    template<class T> friend bool operator!=(const T& a, const FieldAccessor& b) // FIXME: Should be modified so it never throws
    {
        return a != b.get();
    }

protected:
    MixedFieldAccessorBase(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for mixed type of non-const parent.
template<class Taboid, int col_idx>
class FieldAccessor<Taboid, col_idx, Mixed, false>:
    public MixedFieldAccessorBase<Taboid, col_idx, FieldAccessor<Taboid, col_idx, Mixed, false> > {
private:
    typedef FieldAccessor<Taboid, col_idx, Mixed, false> This;
    typedef MixedFieldAccessorBase<Taboid, col_idx, This> Base;

public:
    /// Returns null if the current value is not a subtable.
    TableRef get_subtable() const
    {
        return Base::m_table->get_impl()->get_subtable(col_idx, Base::m_row_idx);
    }

    /// Overwrites the current value with an empty subtable and
    /// returns a reference to it.
    TableRef set_subtable() const
    {
        Base::m_table->get_impl()->clear_subtable(col_idx, Base::m_row_idx);
        return get_subtable();
    }

    /// This function makes the following assumption: If the current
    /// value is a subtable, then it is a subtable of the specified
    /// type. If this is not the case, your computer may catch fire.
    ///
    /// To safely and efficiently check whether the current value is a
    /// subtable of any of a set of specific table types, you may do
    /// as follows:
    ///
    /// \code{.cpp}
    ///
    ///   if (TableRef subtable = my_table[i].mixed.get_subtable()) {
    ///     if (subtable->is_a<MyFirstSubtable>()) {
    ///       MyFirstSubtable::Ref s = unchecked_cast<MyFirstSubtable>(move(subtable))) {
    ///       // ...
    ///     }
    ///     else if (subtable->is_a<MySecondSubtable>()) {
    ///       MySecondSubtable::Ref s = unchecked_cast<MySecondSubtable>(move(subtable))) {
    ///       // ...
    ///     }
    ///   }
    ///
    /// \endcode
    ///
    /// \return Null if the current value is not a subtable.
    ///
    /// \note This function is generally unsafe because it does not
    /// check that the specified table type matches the actual table
    /// type.
    ///
    /// FIXME: Consider deleting this function, since it is both
    /// unsafe and superfluous.
    template<class T> BasicTableRef<T> get_subtable() const
    {
        TIGHTDB_ASSERT(!Base::is_subtable() || Base::template is_subtable<T>());
        return unchecked_cast<T>(get_subtable());
    }

    /// Overwrites the current value with an empty subtable and
    /// returns a reference to it.
    ///
    /// \tparam T The subtable type. It must not be const-qualified.
    template<class T> BasicTableRef<T> set_subtable() const
    {
        BasicTableRef<T> t = unchecked_cast<T>(set_subtable());
        t->set_dynamic_spec();
        return move(t);
    }


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};


/// Field accessor specialization for mixed type of const parent.
template<class Taboid, int col_idx>
class FieldAccessor<Taboid, col_idx, Mixed, true>:
    public MixedFieldAccessorBase<Taboid, col_idx, FieldAccessor<Taboid, col_idx, Mixed, true> > {
private:
    typedef FieldAccessor<Taboid, col_idx, Mixed, true> This;
    typedef MixedFieldAccessorBase<Taboid, col_idx, This> Base;

public:
    ConstTableRef get_subtable() const
    {
        return Base::m_table->get_impl()->get_subtable(col_idx, Base::m_row_idx);
    }

    /// FIXME: Consider deleting this function, since it is both
    /// unsafe and superfluous.
    template<class T> BasicTableRef<const T> get_subtable() const
    {
        TIGHTDB_ASSERT(!Base::is_subtable() || Base::template is_subtable<T>());
        return unchecked_cast<const T>(get_subtable());
    }


    explicit FieldAccessor(typename Base::Init i) TIGHTDB_NOEXCEPT: Base(i) {}
};




/// This class gives access to a column of a table.
///
/// \tparam Taboid Either a table or a table view. Constness of access
/// is controlled by what is allowed to be done with/on a 'Taboid*'.
template<class Taboid, int col_idx, class Type> class ColumnAccessor;


/// Commmon base class for all column accessor specializations.
template<class Taboid, int col_idx, class Type> class ColumnAccessorBase {
protected:
    typedef typename GetTableFromView<Taboid>::type RealTable;
    typedef FieldAccessor<Taboid, col_idx, Type, TableIsConst<Taboid>::value> Field;

public:
    Field operator[](std::size_t row_idx) const
    {
        return Field(std::make_pair(m_table, row_idx));
    }

    bool has_index() const { return m_table->get_impl()->has_index(col_idx); }
    void set_index() const { m_table->get_impl()->set_index(col_idx); }

    BasicTableView<RealTable> get_sorted_view(bool ascending=true) const
    {
        return m_table->get_impl()->get_sorted_view(col_idx, ascending);
    }

    void sort(bool ascending = true) const { m_table->get_impl()->sort(col_idx, ascending); }

protected:
    Taboid* const m_table;

    explicit ColumnAccessorBase(Taboid* t) TIGHTDB_NOEXCEPT: m_table(t) {}
};


/// Column accessor specialization for integers.
template<class Taboid, int col_idx>
class ColumnAccessor<Taboid, col_idx, int64_t>:
    public ColumnAccessorBase<Taboid, col_idx, int64_t> {
private:
    typedef ColumnAccessorBase<Taboid, col_idx, int64_t> Base;

public:
    explicit ColumnAccessor(Taboid* t) TIGHTDB_NOEXCEPT: Base(t) {}

    std::size_t find_first(int64_t value) const
    {
        return Base::m_table->get_impl()->find_first_int(col_idx, value);
    }

    // FIXME: What does this function do? It is used by SlabAlloc. Table::find_pos_int() is protected. Something is not right!
    std::size_t find_pos(int64_t value) const TIGHTDB_NOEXCEPT
    {
        return Base::m_table->find_pos_int(col_idx, value);
    }

    BasicTableView<typename Base::RealTable> find_all(int64_t value) const
    {
        return Base::m_table->get_impl()->find_all_int(col_idx, value);
    }

    size_t count(int64_t target) const
    {
        return Base::m_table->get_impl()->count_int(col_idx, target);
    }

    int64_t sum() const
    {
        return Base::m_table->get_impl()->sum(col_idx);
    }

    int64_t maximum() const
    {
        return Base::m_table->get_impl()->maximum(col_idx);
    }

    int64_t minimum() const
    {
        return Base::m_table->get_impl()->minimum(col_idx);
    }

    const ColumnAccessor& operator+=(int64_t value) const
    {
        Base::m_table->get_impl()->add_int(col_idx, value);
        return *this;
    }
};


/// Column accessor specialization for booleans.
template<class Taboid, int col_idx>
class ColumnAccessor<Taboid, col_idx, bool>: public ColumnAccessorBase<Taboid, col_idx, bool> {
private:
    typedef ColumnAccessorBase<Taboid, col_idx, bool> Base;

public:
    explicit ColumnAccessor(Taboid* t) TIGHTDB_NOEXCEPT: Base(t) {}

    std::size_t find_first(bool value) const
    {
        return Base::m_table->get_impl()->find_first_bool(col_idx, value);
    }

    BasicTableView<typename Base::RealTable> find_all(bool value) const
    {
        return Base::m_table->get_impl()->find_all_bool(col_idx, value);
    }
};


/// Column accessor specialization for enumerations.
template<class Taboid, int col_idx, class E>
class ColumnAccessor<Taboid, col_idx, SpecBase::Enum<E> >:
    public ColumnAccessorBase<Taboid, col_idx, SpecBase::Enum<E> > {
private:
    typedef ColumnAccessorBase<Taboid, col_idx, SpecBase::Enum<E> > Base;

public:
    explicit ColumnAccessor(Taboid* t) TIGHTDB_NOEXCEPT: Base(t) {}

    std::size_t find_first(E value) const
    {
        return Base::m_table->get_impl()->find_first_int(col_idx, int64_t(value));
    }

    BasicTableView<typename Base::RealTable> find_all(E value) const
    {
        return Base::m_table->get_impl()->find_all_int(col_idx, int64_t(value));
    }
};


/// Column accessor specialization for dates.
template<class Taboid, int col_idx>
class ColumnAccessor<Taboid, col_idx, Date>: public ColumnAccessorBase<Taboid, col_idx, Date> {
private:
    typedef ColumnAccessorBase<Taboid, col_idx, Date> Base;

public:
    explicit ColumnAccessor(Taboid* t) TIGHTDB_NOEXCEPT: Base(t) {}

    std::size_t find_first(std::time_t value) const
    {
        return Base::m_table->get_impl()->find_first_date(col_idx, value);
    }

    BasicTableView<typename Base::RealTable> find_all(std::time_t value) const
    {
        return Base::m_table->get_impl()->find_all_date(col_idx, value);
    }
};


/// Column accessor specialization for strings.
template<class Taboid, int col_idx>
class ColumnAccessor<Taboid, col_idx, const char*>:
    public ColumnAccessorBase<Taboid, col_idx, const char*> {
private:
    typedef ColumnAccessorBase<Taboid, col_idx, const char*> Base;

public:
    explicit ColumnAccessor(Taboid* t) TIGHTDB_NOEXCEPT: Base(t) {}

    size_t count(const char* target) const
    {
        return Base::m_table->get_impl()->count_string(col_idx, target);
    }

    std::size_t find_first(const char* value) const
    {
        return Base::m_table->get_impl()->find_first_string(col_idx, value);
    }

    BasicTableView<typename Base::RealTable> find_all(const char* value) const
    {
        return Base::m_table->get_impl()->find_all_string(col_idx, value);
    }

    BasicTableView<typename Base::RealTable> distinct() const
    {
        return Base::m_table->get_impl()->distinct(col_idx);
    }
};


/// Column accessor specialization for binary data.
template<class Taboid, int col_idx>
class ColumnAccessor<Taboid, col_idx, BinaryData>:
    public ColumnAccessorBase<Taboid, col_idx, BinaryData> {
private:
    typedef ColumnAccessorBase<Taboid, col_idx, BinaryData> Base;

public:
    explicit ColumnAccessor(Taboid* t) TIGHTDB_NOEXCEPT: Base(t) {}

    std::size_t find_first(const BinaryData &value) const
    {
        return Base::m_table->get_impl()->find_first_binary(col_idx, value.pointer, value.len);
    }

    BasicTableView<typename Base::RealTable> find_all(const BinaryData &value) const
    {
        return Base::m_table->get_impl()->find_all_date(col_idx, value.pointer, value.len);
    }
};


/// Column accessor specialization for subtables.
template<class Taboid, int col_idx, class Subtab>
class ColumnAccessor<Taboid, col_idx, SpecBase::Subtable<Subtab> >:
    public ColumnAccessorBase<Taboid, col_idx, SpecBase::Subtable<Subtab> > {
private:
    typedef ColumnAccessorBase<Taboid, col_idx, SpecBase::Subtable<Subtab> > Base;

public:
    explicit ColumnAccessor(Taboid* t) TIGHTDB_NOEXCEPT: Base(t) {}
};


/// Column accessor specialization for mixed type.
template<class Taboid, int col_idx>
class ColumnAccessor<Taboid, col_idx, Mixed>: public ColumnAccessorBase<Taboid, col_idx, Mixed> {
private:
    typedef ColumnAccessorBase<Taboid, col_idx, Mixed> Base;

public:
    explicit ColumnAccessor(Taboid* t) TIGHTDB_NOEXCEPT: Base(t) {}
};




/// This class implements a column of a table as used in a table query.
///
/// \tparam Taboid Matches either 'BasicTable<Spec>' or
/// 'BasicTableView<Tab>'. Neither may be const-qualified.
///
/// FIXME: These do not belong in this file!
template<class Taboid, int col_idx, class Type> class QueryColumn;


/// Commmon base class for all query column specializations.
template<class Taboid, int col_idx, class Type> class QueryColumnBase {
protected:
    typedef typename Taboid::Query Query;
    Query* const m_query;
    explicit QueryColumnBase(Query* q) TIGHTDB_NOEXCEPT: m_query(q) {}

    Query& equal(const Type& value) const
    {
        m_query->m_impl.equal(col_idx, value);
        return *m_query;
    }

    Query& not_equal(const Type& value) const
    {
        m_query->m_impl.not_equal(col_idx, value);
        return *m_query;
    }
};


/// QueryColumn specialization for integers.
template<class Taboid, int col_idx>
class QueryColumn<Taboid, col_idx, int64_t>: public QueryColumnBase<Taboid, col_idx, int64_t> {
private:
    typedef QueryColumnBase<Taboid, col_idx, int64_t> Base;
    typedef typename Taboid::Query Query;

public:
    explicit QueryColumn(Query* q) TIGHTDB_NOEXCEPT: Base(q) {}
    using Base::equal;
    using Base::not_equal;

    Query& greater(int64_t value) const
    {
        Base::m_query->m_impl.greater(col_idx, value);
        return *Base::m_query;
    }

    Query& greater_equal(int64_t value) const
    {
        Base::m_query->m_impl.greater_equal(col_idx, value);
        return *Base::m_query;
    }

    Query& less(int64_t value) const
    {
        Base::m_query->m_impl.less(col_idx, value);
        return *Base::m_query;
    }

    Query& less_equal(int64_t value) const
    {
        Base::m_query->m_impl.less_equal(col_idx, value);
        return *Base::m_query;
    }

    Query& between(int64_t from, int64_t to) const
    {
        Base::m_query->m_impl.between(col_idx, from, to);
        return *Base::m_query;
    };

    int64_t sum(std::size_t* resultcount=NULL, std::size_t start=0,
                std::size_t end = std::size_t(-1), std::size_t limit=std::size_t(-1)) const
    {
        return Base::m_query->m_impl.sum(col_idx, resultcount, start, end, limit);
    }

    int64_t maximum(std::size_t* resultcount=NULL, std::size_t start=0,
                    std::size_t end = std::size_t(-1), std::size_t limit=std::size_t(-1)) const
    {
        return Base::m_query->m_impl.maximum(col_idx, resultcount, start, end, limit);
    }

    int64_t minimum(std::size_t* resultcount=NULL, std::size_t start=0,
                    std::size_t end = std::size_t(-1), std::size_t limit=std::size_t(-1)) const
    {
        return Base::m_query->m_impl.minimum(col_idx, resultcount, start, end, limit);
    }

    double average(std::size_t* resultcount=NULL, std::size_t start=0,
                   std::size_t end=std::size_t(-1), std::size_t limit=std::size_t(-1)) const
    {
        return Base::m_query->m_impl.average(col_idx, resultcount, start, end, limit);
    }
};


/// QueryColumn specialization for booleans.
template<class Taboid, int col_idx>
class QueryColumn<Taboid, col_idx, bool>: public QueryColumnBase<Taboid, col_idx, bool> {
private:
    typedef QueryColumnBase<Taboid, col_idx, bool> Base;
    typedef typename Taboid::Query Query;

public:
    explicit QueryColumn(Query* q) TIGHTDB_NOEXCEPT: Base(q) {}
    using Base::equal;
    using Base::not_equal;
};


/// QueryColumn specialization for enumerations.
template<class Taboid, int col_idx, class E>
class QueryColumn<Taboid, col_idx, SpecBase::Enum<E> >:
    public QueryColumnBase<Taboid, col_idx, SpecBase::Enum<E> > {
private:
    typedef QueryColumnBase<Taboid, col_idx, SpecBase::Enum<E> > Base;
    typedef typename Taboid::Query Query;

public:
    explicit QueryColumn(Query* q) TIGHTDB_NOEXCEPT: Base(q) {}
    using Base::equal;
    using Base::not_equal;
};


/// QueryColumn specialization for dates.
template<class Taboid, int col_idx>
class QueryColumn<Taboid, col_idx, Date>: public QueryColumnBase<Taboid, col_idx, Date> {
private:
    typedef QueryColumnBase<Taboid, col_idx, Date> Base;
    typedef typename Taboid::Query Query;

public:
    explicit QueryColumn(Query* q) TIGHTDB_NOEXCEPT: Base(q) {}

    Query& equal(std::time_t value) const
    {
        Base::m_query->m_impl.equal(col_idx, value); // fixme, missing equal_date
        return *Base::m_query;
    }

    Query& not_equal(std::time_t value) const
    {
        Base::m_query->m_impl.not_equal(col_idx, value); // fixme, missing not_equal_date
        return *Base::m_query;
    }

    Query& greater(std::time_t value) const
    {
        Base::m_query->m_impl.greater(col_idx, value); // fixme, missing greater_date
        return *Base::m_query;
    }

    Query& greater_equal(std::time_t value) const
    {
        Base::m_query->m_impl.greater_equal(col_idx, value); // fixme, todo, missing greater_equal_date
        return *Base::m_query;
    }

    Query& less(std::time_t value) const
    {
        Base::m_query->m_impl.less(col_idx, value); // fixme, todo, missing less_date
        return *Base::m_query;
    }

    Query& less_equal(std::time_t value) const
    {
        Base::m_query->m_impl.less_equal(col_idx, value);
        return *Base::m_query;
    }

    Query& between(std::time_t from, std::time_t to) const
    {
        Base::m_query->m_impl.between(col_idx, from, to);
        return *Base::m_query;
    };

    std::time_t maximum(const Taboid& tab, std::size_t* resultcount=NULL, std::size_t start=0,
                        std::size_t end = std::size_t(-1), std::size_t limit=std::size_t(-1)) const
    {
        return Base::m_query->m_impl.maximum_date(tab, col_idx, resultcount, start, end, limit);
    }

    std::time_t minimum(const Taboid& tab, std::size_t* resultcount=NULL, std::size_t start=0,
                        std::size_t end = std::size_t(-1), std::size_t limit=std::size_t(-1)) const
    {
        return Base::m_query->m_impl.minimum_date(tab, col_idx, resultcount, start, end, limit);
    }
};


/// QueryColumn specialization for strings.
template<class Taboid, int col_idx>
class QueryColumn<Taboid, col_idx, const char*>:
    public QueryColumnBase<Taboid, col_idx, const char*> {
private:
    typedef QueryColumnBase<Taboid, col_idx, const char*> Base;
    typedef typename Taboid::Query Query;

public:
    explicit QueryColumn(Query* q) TIGHTDB_NOEXCEPT: Base(q) {}

    Query& equal(const char* value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.equal(col_idx, value, case_sensitive);
        return *Base::m_query;
    }

    Query& not_equal(const char* value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.not_equal(col_idx, value, case_sensitive);
        return *Base::m_query;
    }

    Query& begins_with(const char* value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.begins_with(col_idx, value, case_sensitive);
        return *Base::m_query;
    }

    Query& ends_with(const char* value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.ends_with(col_idx, value, case_sensitive);
        return *Base::m_query;
    }

    Query& contains(const char* value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.contains(col_idx, value, case_sensitive);
        return *Base::m_query;
    }
};


/// QueryColumn specialization for binary data.
template<class Taboid, int col_idx>
class QueryColumn<Taboid, col_idx, BinaryData>:
    public QueryColumnBase<Taboid, col_idx, BinaryData> {
private:
    typedef QueryColumnBase<Taboid, col_idx, BinaryData> Base;
    typedef typename Taboid::Query Query;

public:
    explicit QueryColumn(Query* q) TIGHTDB_NOEXCEPT: Base(q) {}

    Query& equal(const BinaryData& value) const
    {
        Base::m_query->m_impl.equal_binary(col_idx, value.pointer, value.len);
        return *Base::m_query;
    }

    Query& not_equal(const BinaryData& value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.not_equal_binary(col_idx, value.pointer, value.len, case_sensitive);
        return *Base::m_query;
    }

    Query& begins_with(const BinaryData& value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.begins_with_binary(col_idx, value.pointer, value.len,
                                                 case_sensitive);
        return *Base::m_query;
    }

    Query& ends_with(const BinaryData& value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.ends_with_binary(col_idx, value.pointer, value.len, case_sensitive);
        return *Base::m_query;
    }

    Query& contains(const BinaryData& value, bool case_sensitive=true) const
    {
        Base::m_query->m_impl.contains_binary(col_idx, value.pointer, value.len, case_sensitive);
        return *Base::m_query;
    }
};


/// QueryColumn specialization for subtables.
template<class Taboid, int col_idx, class Subtab>
class QueryColumn<Taboid, col_idx, SpecBase::Subtable<Subtab> > {
private:
    typedef typename Taboid::Query Query;
    Query* const m_query;

public:
    explicit QueryColumn(Query* q) TIGHTDB_NOEXCEPT: m_query(q) {}

    Query& subtable()
    {
        m_query->m_impl.subtable(col_idx);
        return *m_query;
    }
};


/// QueryColumn specialization for mixed type.
template<class Taboid, int col_idx> class QueryColumn<Taboid, col_idx, Mixed> {
private:
    typedef typename Taboid::Query Query;

public:
    explicit QueryColumn(Query*) TIGHTDB_NOEXCEPT {}
};


} // namespace _impl
} // namespaced tightdb

#endif // TIGHTDB_TABLE_ACCESSORS_HPP
