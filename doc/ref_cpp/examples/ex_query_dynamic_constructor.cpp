// @@Example: ex_cpp_dyn_query_constructor @@
#include <tightdb.hpp>

using namespace tightdb;

int main()
{
// @@Show@@ 
    Group group;
    TableRef table = group.get_table("test");

    // Create query with no criterias which will match all rows
    Query q = table->where();
// @@EndShow@@
}
// @@EndExample@@
