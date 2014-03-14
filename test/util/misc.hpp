#ifndef TIGHTDB_TEST_UTIL_MISC_HPP
#define TIGHTDB_TEST_UTIL_MISC_HPP

#include <string>

namespace tightdb {
namespace test_util {

void replace_all(std::string& str, const std::string& from, const std::string& to);
bool equal_without_cr(std::string s1, std::string s2);
template <class T> bool almost_equal(T a, T b, double epsilon = 0.0001)
{
    return a < b + epsilon && a > b - epsilon;
}

} // namespace test_util
} // namespace tightdb

#endif // TIGHTDB_TEST_UTIL_MISC_HPP
