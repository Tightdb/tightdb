// @@Example: ex_cpp_shared_group_write @@
// @@Fold@@
#include <cassert>
#include <realm.hpp>
#include <realm/util/file.hpp>

using namespace realm;

// Define schema for main table
REALM_TABLE_3(PeopleTable,
                  name,   String,
                  age,    Int,
                  hired,  Bool)

// @@EndFold@@
void some_function_making_it_impossible_to_use_RAII(SharedGroup& g, bool should_commit)
{
    if (should_commit)
        g.commit();
    else
        g.rollback();
}

void func()
{
    // Create a new shared group
    SharedGroup db("shared_db.realm");

    // Do a write transaction
    {
        Group& g = db.begin_write();

        try {

            // Get table (creating it if it does not exist)
            PeopleTable::Ref employees = g.add_table<PeopleTable>("employees");

            // Add initial rows (with sub-tables)
            if (employees->is_empty()) {
                employees->add("joe", 42, false);
                employees->add("jessica", 22, true);
            }
            some_function_making_it_impossible_to_use_RAII(db, true);
        }
        catch (...) {
            db.rollback();
            throw;
        }
    }
}

// @@Fold@@
int main()
{
    func();
    util::File::remove("shared_db.realm");
}
// @@EndFold@@
// @@EndExample@@
