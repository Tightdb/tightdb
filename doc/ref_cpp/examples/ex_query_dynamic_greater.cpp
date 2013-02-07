// @@Example: ex_cpp_dyn_query_less @@
#include <tightdb.hpp>

using namespace tightdb;
using namespace std;

int main()
{
    // Create following table dynamically:

// @@Show@@
    // name    age
    // ------------
    // Alice    27
    // Bob      50
    // Peter    44

// @@EndShow@@
    Group group;
    TableRef table = group.get_table("test");

    Spec& s = table->get_spec();
    s.add_column(type_String, "name");
    s.add_column(type_Int,    "age");
    table->update_from_spec();

    table->add_empty_row();
    table->set_string(0, 0, "Alice");
    table->insert_int(1, 0, 27);

    table->add_empty_row();
    table->set_string(0, 1, "Bob");
    table->insert_int(1, 1, 50);

    table->add_empty_row();
    table->set_string(0, 2, "Peter");
    table->insert_int(1, 2, 44);

// @@Show@@
    // Find rows where age (column 1) > 20
    Query query = table->where().greater(1, 30);
    TableView view = query.find_all();

    assert(view.size() == 2);
    assert(!strcmp(view.get_string(0, 0), string("Bob")));
    assert(!strcmp(view.get_string(0, 1), string("Peter")));
// @@EndShow@@
}
// @@EndExample@@
