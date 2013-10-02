// @@Example: ex_cpp_shared_group_reserve @@
// @@Fold@@
#include <iostream>
#include <tightdb.hpp>

using namespace std;
using namespace tightdb;

void fill_in_data(SharedGroup&)
{
    // ...
}

void work_on_data(SharedGroup&)
{
    // ...
}

int main()
{
// @@EndFold@@
    SharedGroup sg("new_data_set.tightdb");

    // Set aside disk space
    sg.reserve(128*(1024*1024L)); // = 128MiB

    fill_in_data(sg);

    // Work on data efficiently due to low fragmentation on disk
    work_on_data(sg);
// @@Fold@@
}
// @@EndFold@@
// @@EndExample@@
