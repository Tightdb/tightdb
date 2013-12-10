#include <ctime>
#include <iostream>

#include <tightdb.hpp>
#include <tightdb/util/file.hpp>

using namespace std;
using namespace tightdb;
using namespace tightdb::util;


namespace {

TIGHTDB_TABLE_2(Alpha,
                foo, Int,
                bar, Int)

} // anonymous namespace


#define DIR "/tmp"

int main()
{
    bool no_create = false;
    SharedGroup::DurabilityLevel dlevel = SharedGroup::durability_Full;

    File::try_remove(DIR "/benchmark-prealloc.tightdb");
    SharedGroup sg(DIR "/benchmark-prealloc.tightdb", no_create, dlevel);

    File::try_remove(DIR "/benchmark-prealloc-interfere1.tightdb");
    SharedGroup sg_interfere1(DIR "/benchmark-prealloc-interfere1.tightdb", no_create, dlevel);

    File::try_remove(DIR "/benchmark-prealloc-interfere2.tightdb");
    SharedGroup sg_interfere2(DIR "/benchmark-prealloc-interfere2.tightdb", no_create, dlevel);

    File::try_remove(DIR "/benchmark-prealloc-interfere3.tightdb");
    SharedGroup sg_interfere3(DIR "/benchmark-prealloc-interfere3.tightdb", no_create, dlevel);

    int n_outer = 100;
    {
        time_t begin = time(0);

        int n_inner = 100;
        for (int i=0; i<n_outer; ++i) {
            cerr << ".";
            for (int j=0; j<n_inner; ++j) {
                {
                    WriteTransaction wt(sg);
                    Alpha::Ref t = wt.get_table<Alpha>("alpha");
                    for (int j=0; j<1000; ++j) t->add(65536,65536);
                    wt.commit();
                }
                // Interference
                for (int k=0; k<2; ++k) {
                    {
                        WriteTransaction wt(sg_interfere1);
                        Alpha::Ref t = wt.get_table<Alpha>("alpha");
                        for (int j=0; j<100; ++j) t->add(65536,65536);
                        wt.commit();
                    }
                    {
                        WriteTransaction wt(sg_interfere2);
                        Alpha::Ref t = wt.get_table<Alpha>("alpha");
                        for (int j=0; j<400; ++j) t->add(65536,65536);
                        wt.commit();
                    }
                    {
                        WriteTransaction wt(sg_interfere3);
                        Alpha::Ref t = wt.get_table<Alpha>("alpha");
                        for (int j=0; j<1600; ++j) t->add(65536,65536);
                        wt.commit();
                    }
                }
            }
        }
        cerr << "\n";

        time_t end = time(0);
        cerr << "Small write transactions per second = " << (( n_outer*n_inner*7 / double(end - begin) )) << endl;
    }

    {
        time_t begin = time(0);

        int n_inner = 10;
        for (int i=0; i<n_outer; ++i) {
            cerr << "x";
            for (int j=0; j<n_inner; ++j) {
                {
                    WriteTransaction wt(sg);
                    Alpha::Ref t = wt.get_table<Alpha>("alpha");
                    t->column().foo += 1;
                    t->column().bar += 1;
                    wt.commit();
                }
            }
        }
        cerr << "\n";

        time_t end = time(0);
        cerr << "Large write transactions per second = " << (( n_outer*n_inner / double(end - begin) )) << endl;
    }
}
