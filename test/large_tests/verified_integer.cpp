#ifndef VER_INT_H
#define VER_INT_H

#include <vector>
#include <string>
#include <algorithm>
#ifdef _MSC_VER
    #include <win32\stdint.h>
#endif
#include <stdio.h>
#include "verified_integer.hpp"

using namespace std;
using namespace tightdb;

void VerifiedInteger::VerifyNeighbours(size_t ndx)
{
    if (v.size() > ndx)
        TIGHTDB_ASSERT(v[ndx] == u.get(ndx));

    if (ndx > 0)
        TIGHTDB_ASSERT(v[ndx - 1] == u.get(ndx - 1));

    if (v.size() > ndx + 1)
        TIGHTDB_ASSERT(v[ndx + 1] == u.get(ndx + 1));
}

void VerifiedInteger::add(int64_t value)
{
    v.push_back(value);
    u.add(value);
    TIGHTDB_ASSERT(v.size() == u.size());
    VerifyNeighbours(v.size());
    TIGHTDB_ASSERT(ConditionalVerify());
}

void VerifiedInteger::Insert(size_t ndx, int64_t value)
{
    v.insert(v.begin() + ndx, value);
    u.insert(ndx, value);
    TIGHTDB_ASSERT(v.size() == u.size());
    VerifyNeighbours(ndx);
    TIGHTDB_ASSERT(ConditionalVerify());
}

int64_t VerifiedInteger::get(size_t ndx)
{
    TIGHTDB_ASSERT(v[ndx] == u.get(ndx));
    return v[ndx];
}

int64_t VerifiedInteger::Sum(size_t start, size_t end)
{
    int64_t sum = 0;

    if (start == end)
        return 0;

    if (end == size_t(-1))
        end = v.size();

    for (size_t t = start; t < end; ++t)
        sum += v[t];

    TIGHTDB_ASSERT(sum == u.sum(start, end));
    return sum;
}

int64_t VerifiedInteger::maximum(size_t start, size_t end)
{
    if (end == size_t(-1))
        end = v.size();

    if (end == start)
        return 0;

    int64_t max = v[start];

    for (size_t t = start + 1; t < end; ++t)
        if (v[t] > max)
            max = v[t];

    TIGHTDB_ASSERT(max == u.maximum(start, end));
    return max;
}

int64_t VerifiedInteger::minimum(size_t start, size_t end)
{
    if (end == size_t(-1))
        end = v.size();

    if (end == start)
        return 0;

    int64_t min = v[start];

    for (size_t t = start + 1; t < end; ++t)
        if (v[t] < min)
            min = v[t];

    TIGHTDB_ASSERT(min == u.minimum(start, end));
    return min;
}

void VerifiedInteger::set(size_t ndx, int64_t value)
{
    v[ndx] = value;
    u.set(ndx, value);
    VerifyNeighbours(ndx);
    TIGHTDB_ASSERT(ConditionalVerify());
}

void VerifiedInteger::Delete(size_t ndx)
{
    v.erase(v.begin() + ndx);
    u.erase(ndx);
    TIGHTDB_ASSERT(v.size() == u.size());
    VerifyNeighbours(ndx);
    TIGHTDB_ASSERT(ConditionalVerify());
}

void VerifiedInteger::Clear()
{
    v.clear();
    u.clear();
    TIGHTDB_ASSERT(v.size() == u.size());
    TIGHTDB_ASSERT(ConditionalVerify());
}

size_t VerifiedInteger::find_first(int64_t value)
{
    std::vector<int64_t>::iterator it = std::find(v.begin(), v.end(), value);
    size_t ndx = std::distance(v.begin(), it);
    size_t index2 = u.find_first(value);
    TIGHTDB_ASSERT(ndx == index2 || (it == v.end() && index2 == size_t(-1)));
    static_cast<void>(index2);
    return ndx;
}

size_t VerifiedInteger::size()
{
    TIGHTDB_ASSERT(v.size() == u.size());
    return v.size();
}

// todo/fixme, end ignored
void VerifiedInteger::find_all(Array &c, int64_t value, size_t start, size_t end)
{
    std::vector<int64_t>::iterator ita = v.begin() + start;
    std::vector<int64_t>::iterator itb = end == size_t(-1) ? v.end() : v.begin() + (end == size_t(-1) ? v.size() : end);;
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
    if (c.size() != result.size())
        TIGHTDB_ASSERT(false);
    for (size_t t = 0; t < result.size(); ++t) {
        if (result[t] != size_t(c.get(t)))
            TIGHTDB_ASSERT(false);
    }

    return;
}

bool VerifiedInteger::Verify()
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
bool VerifiedInteger::ConditionalVerify()
{
    if ((uint64_t(rand()) * uint64_t(rand()))  % (v.size() / 10 + 1) == 0) {
        return Verify();
    }
    else {
        return true;
    }
}

void VerifiedInteger::Destroy()
{
    u.destroy();
}

#endif
