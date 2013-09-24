#include <vector>
#include <string>
#include <algorithm>
#ifdef _MSC_VER
    #include <win32\stdint.h>
#endif
#include <stdio.h>
#include <tightdb/column_string.hpp>
#include "verified_string.hpp"

using namespace std;
using namespace tightdb;

void VerifiedString::verify_neighbours(size_t ndx)
{
    if (v.size() > ndx)
        TIGHTDB_ASSERT(v[ndx] == u.get(ndx));

    if (ndx > 0)
        TIGHTDB_ASSERT(v[ndx - 1] == u.get(ndx - 1));

    if (v.size() > ndx + 1)
        TIGHTDB_ASSERT(v[ndx + 1] == u.get(ndx + 1));
}

void VerifiedString::add(StringData value)
{
    v.push_back(value);
    u.add(value);
    TIGHTDB_ASSERT(v.size() == u.size());
    verify_neighbours(v.size());
    TIGHTDB_ASSERT(conditional_verify());
}


void VerifiedString::insert(size_t ndx, StringData value)
{
    v.insert(v.begin() + ndx, value);
    u.insert(ndx, value);
    TIGHTDB_ASSERT(v.size() == u.size());
    verify_neighbours(ndx);
    TIGHTDB_ASSERT(conditional_verify());
}


StringData VerifiedString::get(size_t ndx)
{
    TIGHTDB_ASSERT(v[ndx] == u.get(ndx));
    return v[ndx];
}

void VerifiedString::set(size_t ndx, StringData value)
{
    v[ndx] = value;
    u.set(ndx, value);
    verify_neighbours(ndx);
    TIGHTDB_ASSERT(conditional_verify());
}

void VerifiedString::erase(size_t ndx)
{
    v.erase(v.begin() + ndx);
    u.erase(ndx, ndx == u.size());
    TIGHTDB_ASSERT(v.size() == u.size());
    verify_neighbours(ndx);
    TIGHTDB_ASSERT(conditional_verify());
}

void VerifiedString::clear()
{
    v.clear();
    u.clear();
    TIGHTDB_ASSERT(v.size() == u.size());
    TIGHTDB_ASSERT(conditional_verify());
}

size_t VerifiedString::find_first(StringData value)
{
    std::vector<string>::iterator it = std::find(v.begin(), v.end(), value);
    size_t ndx = std::distance(v.begin(), it);
    size_t index2 = u.find_first(value);
    static_cast<void>(index2);
    TIGHTDB_ASSERT(ndx == index2 || (it == v.end() && index2 == size_t(-1)));
    return ndx;
}

size_t VerifiedString::size()
{
    TIGHTDB_ASSERT(v.size() == u.size());
    return v.size();
}

// todo/fixme, end ignored
void VerifiedString::find_all(Array& c, StringData value, size_t start, size_t end)
{
    std::vector<string>::iterator ita = v.begin() + start;
    std::vector<string>::iterator itb = v.begin() + (end == size_t(-1) ? v.size() : end);
    std::vector<size_t> result;
    while (ita != itb) {
        ita = std::find(ita, itb, value);
        size_t ndx = std::distance(v.begin(), ita);
        if (ndx < v.size()) {
            result.push_back(ndx);
            ita++;
        }
    }

    c.clear();

    u.find_all(c, value);
    size_t cs = c.size();
    if (cs != result.size())
        TIGHTDB_ASSERT(false);
    for (size_t t = 0; t < result.size(); ++t) {
        if (result[t] != size_t(c.get(t)))
            TIGHTDB_ASSERT(false);
    }

    return;
}

bool VerifiedString::Verify()
{
    TIGHTDB_ASSERT(u.size() == v.size());
    if (u.size() != v.size())
        return false;

    for (size_t t = 0; t < v.size(); ++t) {
        TIGHTDB_ASSERT(v[t] == u.get(t));
        if (v[t] != u.get(t))
            return false;
    }
    return true;
}

// makes it run amortized the same time complexity as original, even though the row count grows
bool VerifiedString::conditional_verify()
{
    if ((uint64_t(rand()) * uint64_t(rand()))  % (v.size() / 10 + 1) == 0) {
        return Verify();
    }
    else {
        return true;
    }
}

void VerifiedString::destroy()
{
    u.destroy();
}
