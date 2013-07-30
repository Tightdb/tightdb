#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <iostream>

#include "../util/timer.hpp"
#include "../util/mem.hpp"
#include "../util/number_names.hpp"

#ifdef _MSC_VER
#include "../../src/win32/stdint.h"
#else
#include <stdint.h>
#endif

using namespace std;
using namespace tightdb;

namespace {

enum Days {
    Mon,
    Tue,
    Wed,
    Thu,
    Fri,
    Sat,
    Sun
};

struct TestTable {
    int first;
    string second;
    int third;
    Days fourth;
};

class match_second {
public:
    match_second(const string& target) : m_target(target) {}
    bool operator()(const TestTable& v) const {
        return v.second == m_target;
    }
private:
    const string& m_target;
};

class match_third {
public:
    match_third(int target) : m_target(target) {}
    bool operator()(const TestTable& v) const {return v.third == m_target;}
private:
    const int m_target;
};

class match_fourth {
public:
    match_fourth(Days target) : m_target(target) {}
    bool operator()(const TestTable& v) const {return v.fourth == m_target;}
private:
    const Days m_target;
};

// Get and Set are too fast (50ms/M) for normal 64-bit rand*rand*rand*rand*rand (5-10ms/M)
uint64_t rand2()
{
    return (uint64_t)rand() * (uint64_t)rand() * (uint64_t)rand() * (uint64_t)rand() * (uint64_t)rand();


    static int64_t seed = 2862933555777941757ULL;
    static int64_t seed2 = 0;
    seed = (2862933555777941757ULL * seed + 3037000493ULL);
    seed2++;
    return seed * seed2 + seed2;
}

} // anonymous namespace


int main()
{
    const size_t ROWS = 250000;
    const size_t TESTS = 100;

    vector<TestTable> table;

    cout << "Create random content with "<<ROWS<<" rows.\n\n";
    for (size_t i = 0; i < ROWS; ++i) {
        // create random string
        const int n = rand() % 1000;// * 10 + rand();
        const string s = test_util::number_name(n);

        TestTable t = {n, s, 100, Wed};
        table.push_back(t);
    }

    // Last entry for verification
    TestTable t = {0, "abcde", 100, Wed};
    table.push_back(t);

    cout << "Memory usage:\t\t"<<test_util::get_mem_usage()<<" bytes\n";

    test_util::Timer timer;

    // Search small integer column
    {
        timer.reset();

        // Do a search over entire column (value not found)
        for (size_t i = 0; i < TESTS; ++i) {
            vector<TestTable>::const_iterator res = find_if(table.begin(), table.end(), match_fourth(Tue));
            if (res != table.end()) {
                cout << "error\n";
            }
        }

        cout << "Search (small integer):\t"<<timer<<"\n";
    }

    // Search byte-sized integer column
    {
        timer.reset();

        // Do a search over entire column (value not found)
        for (size_t i = 0; i < TESTS; ++i) {
            vector<TestTable>::const_iterator res = find_if(table.begin(), table.end(), match_third(50));
            if (res != table.end()) {
                cout << "error\n";
            }
        }

        cout << "Search (byte-sized int)\t"<<timer<<"\n";
    }

    // Search string column
    {
        timer.reset();

        // Do a search over entire column (value not found)
        const string target = "abcde";
        for (size_t i = 0; i < TESTS; ++i) {
            vector<TestTable>::const_iterator res = find_if(table.begin(), table.end(), match_second(target));
            if (res == table.end()) {
                cout << "error\n";
            }
        }

        cout << "Search (string):\t"<<timer<<"\n";
    }

    // Add index
    multimap<int, TestTable> mapTable;
    {
        timer.reset();

        // Copy data to map
        for (vector<TestTable>::const_iterator p = table.begin(); p != table.end(); ++p) {
            mapTable.insert(pair<int,TestTable>(p->first,*p));
        }

        // free memory used by table
        vector<TestTable>().swap(table);

        cout << "\nAdd index:\t\t"<<timer<<"\n";

        cout << "Memory usage2:\t\t"<<test_util::get_mem_usage()<<" bytes\n";
    }

    // Search with index
    {
        timer.reset();

        for (size_t i = 0; i < TESTS*10; ++i) {
            const size_t n = rand() % 1000;
            multimap<int, TestTable>::const_iterator p = mapTable.find(n);
            if (p->second.fourth == Fri) { // to avoid above find being optimized away
                cout << "error\n";
            }
        }

        cout << "Search index:\t\t"<<timer<<"\n";
    }
    cout << "\nDone.\n";

#ifdef _MSC_VER
    cin.get();
#endif
    return 0;
}
