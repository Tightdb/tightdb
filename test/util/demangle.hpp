#ifndef TIGHTDB_TEST_UTIL_DEMANGLE_HPP
#define TIGHTDB_TEST_UTIL_DEMANGLE_HPP

#include <typeinfo>
#include <string>

namespace tightdb {
namespace test_util {


/// Demangle the specified name.
///
/// See for example
/// http://gcc.gnu.org/onlinedocs/libstdc++/latest-doxygen/namespaceabi.html
std::string demangle(const std::string&);


/// Get the demangled name of the specified type.
template<class T> inline std::string get_type_name()
{
    return demangle(typeid(T).name());
}


/// Get the demangled name of the type of the specified argument.
template<typename T> inline std::string get_type_name(T const &)
{
    return get_type_name<T>();
}


} // namespace test_util
} // namespace tightdb

#endif // TIGHTDB_TEST_UTIL_DEMANGLE_HPP
