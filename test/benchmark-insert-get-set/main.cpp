#include <cstdlib>
#include <algorithm>
#include <iostream>

#include <tightdb.hpp>

#include "../util/timer.hpp"
#include "../util/benchmark_results.hpp"

using namespace std;
using namespace tightdb;


namespace {

TIGHTDB_TABLE_1(IntTable,
                i, Int)


inline int_fast64_t read(IntTable& table, const vector<size_t> order)
{
    int_fast64_t dummy = 0;
    size_t n = order.size();
    for (size_t i = 0; i != n; ++i)
        dummy += table[order[i]].i;
    return dummy;
}

inline void write(IntTable& table, const vector<size_t> order)
{
    size_t n = order.size();
    for (size_t i = 0; i != n; ++i)
        table[order[i]].i = 125;
}

inline void insert(IntTable& table, const vector<size_t> order)
{
    size_t n = order.size();
    for (size_t i = 0; i != n; ++i)
        table.insert(order[i], 127);
}

inline void erase(IntTable& table, const vector<size_t> order)
{
    size_t n = order.size();
    for (size_t i = 0; i != n; ++i)
        table.remove(order[i]);
}

} // anonymous namepsace


int main()
{
    const size_t target_size = 1100*1000L;
    const int num_tables = 50;
    cout << "Number of tables: " << num_tables << endl;
    cout << "Elements per table: " << target_size << endl;

    vector<size_t> rising_order;
    vector<size_t> falling_order;
    vector<size_t> random_order;
    vector<size_t> random_insert_order;
    vector<size_t> random_erase_order;
    for (size_t i = 0; i != target_size; ++i) {
        rising_order.push_back(i);
        falling_order.push_back(target_size-1-i);
        random_order.push_back(i);
        random_insert_order.push_back(rand() % (i+1));
        random_erase_order.push_back(rand() % (target_size-i));
    }
    random_shuffle(random_order.begin(), random_order.end());

    IntTable tables_1[num_tables], tables_2[num_tables];

    int_fast64_t dummy = 0;

    int max_lead_text_size = 26;
    test_util::BenchmarkResults results(max_lead_text_size);

    test_util::Timer timer_total(test_util::Timer::type_UserTime);
    test_util::Timer timer(test_util::Timer::type_UserTime);
    {
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            insert(tables_1[i], rising_order);
        results.submit(timer, "insert_end_compact", "Insert at end (compact)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            dummy += read(tables_1[i], rising_order);
        results.submit(timer, "read_seq_compact", "Sequential read (compact)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            dummy += read(tables_1[i], random_order);
        results.submit(timer, "read_ran_compact", "Random read (compact)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            write(tables_1[i], rising_order);
        results.submit(timer, "write_seq_compact", "Sequential write (compact)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            write(tables_1[i], random_order);
        results.submit(timer, "write_ran_compact", "Random write (compact)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            erase(tables_1[i], falling_order);
        results.submit(timer, "erase_end_compact", "Erase from end (compact)");
    }
    {
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            insert(tables_2[i], random_insert_order);
        results.submit(timer, "insert_ran_general", "Random insert (general)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            dummy += read(tables_2[0], rising_order);
        results.submit(timer, "read_seq_general", "Sequential read (general)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            dummy += read(tables_2[0], random_order);
        results.submit(timer, "read_ran_general", "Random read (general)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            write(tables_2[i], rising_order);
        results.submit(timer, "write_seq_general", "Sequential write (general)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            write(tables_2[i], random_order);
        results.submit(timer, "write_ran_general", "Random write (general)");
        timer.reset();
        for (int i = 0; i != num_tables; ++i)
            erase(tables_2[i], random_erase_order);
        results.submit(timer, "erase_ran_general", "Random erase (general)");
    }

    results.submit(timer_total, "total_time", "Total time");

    cout << "dummy = "<<dummy<<" (to avoid over-optimization)"<<endl;
}
