#include <realm.hpp>
#include <realm/util/file.hpp>

using namespace realm;

REALM_TABLE_1(TestTable,
                value, Int)

int main()
{
    util::File::try_remove("test.realm");
    util::File::try_remove("test.realm.lock");

    // Testing 'async' mode because it has the special requirement of
    // being able to find `realmd` (typically in
    // /usr/local/libexec/).
    bool no_create = false;
    SharedGroup sg("test.realm", no_create, SharedGroup::durability_Async);
    {
        WriteTransaction wt(sg);
        TestTable::Ref test = wt.get_table<TestTable>("test");
        test->add(3821);
        wt.commit();
    }
    {
        ReadTransaction rt(sg);
        TestTable::ConstRef test = rt.get_table<TestTable>("test");
        if (test[0].value != 3821)
            return 1;
    }

    util::File::remove("test.realm");
    util::File::remove("test.realm.lock");
}
