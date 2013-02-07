// @@Example: ex_cpp_dyn_query_contains @@
#include <tightdb.hpp>

using namespace tightdb;
using namespace std;

int main()
{
    Group group;
    TableRef table = group.get_table("test");

    Spec& s = table->get_spec();
    s.add_column(type_String, "Name");
    table->update_from_spec();

    table->add_empty_row(5);
// @@Show@@
    table->set_string(0, 0, "Mary"); 
    table->set_string(0, 1, "Joe"); 
    table->set_string(0, 2, "Jack"); 
    table->set_string(0, 3, "Jill"); 
    table->set_string(0, 4, "Jo"); 

    // Find names (column 0) containing "ac", case sensitive
    TableView view1 = table->where().contains(0, "ac").find_all();
    assert(view1.size() == 1);
    assert(!strcmp(view1.get_string(0, 0), "Jack"));

    // Will find no names (column 0) because it's case sensitive
    TableView view2 = table->where().contains(0, "AC").find_all();
    assert(view2.size() == 0);

#ifdef _MSC_VER
    // Case insensitive search only supported on Windows
    TableView view3 = table->where().contains(0, "AC", false).find_all();

    assert(view1.size() == 1);
    assert(!strcmp(view1.get_string(0, 0), "Jack"));
#endif
// @@EndShow@@
}
// @@EndExample@@
