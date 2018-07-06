#include "ModuleTest.hpp"

CPPUNIT_TEST_SUITE_REGISTRATION(ModuleTest);

#include "libdnf/dnf-module.hpp"

using namespace libdnf;

void ModuleTest::setUp()
{
}

void ModuleTest::tearDown()
{
}

void ModuleTest::testDummy()
{
    std::vector<std::string> module_list;

    std::cout << "called ModuleTest::testDummy()" << std::endl;

    /* call with empty module list should do nothing */
    {
        bool ret = dnf_module_dummy(module_list);
        g_assert(ret);
    }

    /* add some modules to the list and try again */
    module_list.push_back(std::string("moduleA"));
    module_list.push_back(std::string("moduleB:streamB"));
    module_list.push_back(std::string("moduleC:streamC/profileC"));

    {
        bool ret = dnf_module_dummy(module_list);
        CPPUNIT_ASSERT(ret);
    }
}

void ModuleTest::testEnable()
{
    std::vector<std::string> module_list;

    std::cout << "called ModuleTest::testDummy()" << std::endl;

    /* call with empty module list should throw exception */
    {
        CPPUNIT_ASSERT_THROW(dnf_module_enable(module_list),
                             std::runtime_error);
    }

    /* add some modules to the list and try again */
    module_list.push_back(std::string("moduleA"));
    module_list.push_back(std::string("moduleB:streamB"));
    module_list.push_back(std::string("moduleC:streamC/profileC"));

    {
        bool ret = dnf_module_enable(module_list);
        CPPUNIT_ASSERT(ret);
    }
}
