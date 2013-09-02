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
#ifndef TIGHTDB_TABLE_REF_HPP
#define TIGHTDB_TABLE_REF_HPP

#include <cstddef>
#include <algorithm>

#include <tightdb/bind_ptr.hpp>

namespace tightdb {


class Table;
template<class> class BasicTable;


/// A "smart" reference to a table.
///
/// This kind of table reference is often needed when working with
/// subtables. For example:
///
/// \code{.cpp}
///
///   void func(Table& table)
///   {
///     Table& sub1 = *(table.get_subtable(0,0)); // INVALID! (sub1 becomes 'dangeling')
///     TableRef sub2 = table.get_subtable(0,0); // Safe!
///   }
///
/// \endcode
///
/// A restricted notion of move semantics (as defined by C++11) is
/// provided. Instead of calling <tt>std::move()</tt> one must call
/// <tt>move()</tt> without the <tt>std</tt> qualifier. The
/// effectiveness of this 'emulation' relies on 'return value
/// optimization' being enabled in the compiler.
///
/// \note A top-level table (explicitely created or obtained from a
/// group) may not be destroyed until all "smart" table references
/// obtained from it, or from any of its subtables, are destroyed.
template<class T> class BasicTableRef: bind_ptr<T> {
public:
#ifdef TIGHTDB_HAVE_CXX11_CONSTEXPR
    constexpr BasicTableRef() TIGHTDB_NOEXCEPT {}
#else
    BasicTableRef() TIGHTDB_NOEXCEPT {}
#endif
    ~BasicTableRef() TIGHTDB_NOEXCEPT {}

#ifdef TIGHTDB_HAVE_CXX11_RVALUE_REFERENCE

    // Copy construct
    BasicTableRef(const BasicTableRef& r) TIGHTDB_NOEXCEPT: bind_ptr<T>(r) {}
    template<class U> BasicTableRef(const BasicTableRef<U>& r) TIGHTDB_NOEXCEPT: bind_ptr<T>(r) {}

    // Copy assign
    BasicTableRef& operator=(const BasicTableRef&) TIGHTDB_NOEXCEPT;
    template<class U> BasicTableRef& operator=(const BasicTableRef<U>&) TIGHTDB_NOEXCEPT;

    // Move construct
    BasicTableRef(BasicTableRef&& r) TIGHTDB_NOEXCEPT: bind_ptr<T>(std::move(r)) {}
    template<class U> BasicTableRef(BasicTableRef<U>&& r) TIGHTDB_NOEXCEPT: bind_ptr<T>(std::move(r)) {}

    // Move assign
    BasicTableRef& operator=(BasicTableRef&&) TIGHTDB_NOEXCEPT;
    template<class U> BasicTableRef& operator=(BasicTableRef<U>&&) TIGHTDB_NOEXCEPT;

#else // !TIGHTDB_HAVE_CXX11_RVALUE_REFERENCE

    // Copy construct
    BasicTableRef(const BasicTableRef& r) TIGHTDB_NOEXCEPT: bind_ptr<T>(r) {}
    template<class U> BasicTableRef(BasicTableRef<U> r) TIGHTDB_NOEXCEPT: bind_ptr<T>(move(r)) {}

    // Copy assign
    BasicTableRef& operator=(BasicTableRef) TIGHTDB_NOEXCEPT;
    template<class U> BasicTableRef& operator=(BasicTableRef<U>) TIGHTDB_NOEXCEPT;

#endif // !TIGHTDB_HAVE_CXX11_RVALUE_REFERENCE

    // Replacement for std::move() in C++03
    friend BasicTableRef move(BasicTableRef& r) TIGHTDB_NOEXCEPT { return BasicTableRef(&r, move_tag()); }

    // Comparison
    template<class U> bool operator==(const BasicTableRef<U>&) const TIGHTDB_NOEXCEPT;
    template<class U> bool operator!=(const BasicTableRef<U>&) const TIGHTDB_NOEXCEPT;
    template<class U> bool operator<(const BasicTableRef<U>&) const TIGHTDB_NOEXCEPT;

    // Dereference
#ifdef __clang__
    // Clang has a bug that causes it to effectively ignore the 'using' declaration.
    T& operator*() const TIGHTDB_NOEXCEPT { return bind_ptr<T>::operator*(); }
#else
    using bind_ptr<T>::operator*;
#endif
    using bind_ptr<T>::operator->;

#ifdef TIGHTDB_HAVE_CXX11_EXPLICIT_CONV_OPERATORS
    using bind_ptr<T>::operator bool;
#else
    using bind_ptr<T>::operator typename bind_ptr<T>::unspecified_bool_type;
#endif

    void reset() TIGHTDB_NOEXCEPT { bind_ptr<T>::reset(); }

    void swap(BasicTableRef& r) TIGHTDB_NOEXCEPT { this->bind_ptr<T>::swap(r); }
    friend void swap(BasicTableRef& a, BasicTableRef& b) TIGHTDB_NOEXCEPT { a.swap(b); }

    template<class U> friend BasicTableRef<U> unchecked_cast(BasicTableRef<Table>) TIGHTDB_NOEXCEPT;
    template<class U> friend BasicTableRef<const U> unchecked_cast(BasicTableRef<const Table>) TIGHTDB_NOEXCEPT;

private:
    template<class> struct GetRowAccType { typedef void type; };
    template<class Spec> struct GetRowAccType<BasicTable<Spec> > {
        typedef typename BasicTable<Spec>::RowAccessor type;
    };
    template<class Spec> struct GetRowAccType<const BasicTable<Spec> > {
        typedef typename BasicTable<Spec>::ConstRowAccessor type;
    };
    typedef typename GetRowAccType<T>::type RowAccessor;

public:
    /// Same as 'table[i]' where 'table' is the referenced table.
    RowAccessor operator[](std::size_t i) const TIGHTDB_NOEXCEPT { return (*this->get())[i]; }

private:
    friend class ColumnSubtableParent;
    friend class Table;
    template<class> friend class BasicTable;
    template<class> friend class BasicTableRef;

    explicit BasicTableRef(T* t) TIGHTDB_NOEXCEPT: bind_ptr<T>(t) {}

    typedef typename bind_ptr<T>::move_tag move_tag;
    BasicTableRef(BasicTableRef* r, move_tag) TIGHTDB_NOEXCEPT: bind_ptr<T>(r, move_tag()) {}

    typedef typename bind_ptr<T>::casting_move_tag casting_move_tag;
    template<class U> BasicTableRef(BasicTableRef<U>* r, casting_move_tag) TIGHTDB_NOEXCEPT:
        bind_ptr<T>(r, casting_move_tag()) {}
};


typedef BasicTableRef<Table> TableRef;
typedef BasicTableRef<const Table> ConstTableRef;


template<class C, class T, class U>
inline std::basic_ostream<C,T>& operator<<(std::basic_ostream<C,T>& out, const BasicTableRef<U>& p)
{
    out << static_cast<const void*>(&*p);
    return out;
}

template<class T> inline BasicTableRef<T> unchecked_cast(TableRef t) TIGHTDB_NOEXCEPT
{
    return BasicTableRef<T>(&t, typename BasicTableRef<T>::casting_move_tag());
}

template<class T> inline BasicTableRef<const T> unchecked_cast(ConstTableRef t) TIGHTDB_NOEXCEPT
{
    return BasicTableRef<const T>(&t, typename BasicTableRef<T>::casting_move_tag());
}





// Implementation:

#ifdef TIGHTDB_HAVE_CXX11_RVALUE_REFERENCE

template<class T>
inline BasicTableRef<T>& BasicTableRef<T>::operator=(const BasicTableRef& r) TIGHTDB_NOEXCEPT
{
    this->bind_ptr<T>::operator=(r);
    return *this;
}

template<class T> template<class U>
inline BasicTableRef<T>& BasicTableRef<T>::operator=(const BasicTableRef<U>& r) TIGHTDB_NOEXCEPT
{
    this->bind_ptr<T>::operator=(r);
    return *this;
}

template<class T>
inline BasicTableRef<T>& BasicTableRef<T>::operator=(BasicTableRef&& r) TIGHTDB_NOEXCEPT
{
    this->bind_ptr<T>::operator=(std::move(r));
    return *this;
}

template<class T> template<class U>
inline BasicTableRef<T>& BasicTableRef<T>::operator=(BasicTableRef<U>&& r) TIGHTDB_NOEXCEPT
{
    this->bind_ptr<T>::operator=(std::move(r));
    return *this;
}

#else // !TIGHTDB_HAVE_CXX11_RVALUE_REFERENCE

template<class T>
inline BasicTableRef<T>& BasicTableRef<T>::operator=(BasicTableRef r) TIGHTDB_NOEXCEPT
{
    this->bind_ptr<T>::operator=(move(static_cast<bind_ptr<T>&>(r)));
    return *this;
}

template<class T> template<class U>
inline BasicTableRef<T>& BasicTableRef<T>::operator=(BasicTableRef<U> r) TIGHTDB_NOEXCEPT
{
    this->bind_ptr<T>::operator=(move(static_cast<bind_ptr<U>&>(r)));
    return *this;
}

#endif // !TIGHTDB_HAVE_CXX11_RVALUE_REFERENCE

template<class T> template<class U>
inline bool BasicTableRef<T>::operator==(const BasicTableRef<U>& r) const TIGHTDB_NOEXCEPT
{
    return this->bind_ptr<T>::operator==(r);
}

template<class T> template<class U>
inline bool BasicTableRef<T>::operator!=(const BasicTableRef<U>& r) const TIGHTDB_NOEXCEPT
{
    return this->bind_ptr<T>::operator!=(r);
}

template<class T> template<class U>
inline bool BasicTableRef<T>::operator<(const BasicTableRef<U>& r) const TIGHTDB_NOEXCEPT
{
    return this->bind_ptr<T>::operator<(r);
}


} // namespace tightdb

#endif // TIGHTDB_TABLE_REF_HPP
