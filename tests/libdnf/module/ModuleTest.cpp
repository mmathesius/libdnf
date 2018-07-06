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
    std::cout << "called ModuleTest::testDummy()" << std::endl;

    /* call with empty module list should do nothing */
    {
        std::vector<std::string> module_list;
        CPPUNIT_ASSERT(dnf_module_dummy(module_list));
    }

    {
        std::vector<std::string> module_list;
        /* add some modules to the list and try again */
        module_list.push_back(std::string("moduleA"));
        module_list.push_back(std::string("moduleB:streamB"));
        module_list.push_back(std::string("moduleC:streamC/profileC"));

        CPPUNIT_ASSERT(dnf_module_dummy(module_list));
    }
}

void ModuleTest::testEnable()
{
    std::cout << "called ModuleTest::testEnable()" << std::endl;

    /* call with empty module list should throw exception */
    {
        std::vector<std::string> module_list;
        CPPUNIT_ASSERT_THROW(dnf_module_enable(module_list),
                             ModuleCommandException);
    }

    /* call with invalid module spec should throw exception */
    {
        std::vector<std::string> module_list;
        module_list.push_back(std::string("moduleA#wrong"));
        CPPUNIT_ASSERT_THROW(dnf_module_enable(module_list),
                             ModuleException);
    }

    /* call with invalid specs should throw exceptions */
    {
        std::vector<std::string> module_list;
        module_list.push_back(std::string("moduleA#wrong"));
        module_list.push_back(std::string("moduleB:streamB#wrong"));
        module_list.push_back(std::string("moduleC:streamC:versionC#wrong"));
        try {
            dnf_module_enable(module_list);
        } catch (ModuleException & e) {
            CPPUNIT_ASSERT(e.list().size() == 3);
            for (const auto & ex : e.list()) {
                std::cout << ex.what() << std::endl;
            }
        }
    }

    /* call with valid module specs should succeed */
    {
        std::vector<std::string> module_list;
        module_list.push_back(std::string("moduleA"));
        module_list.push_back(std::string("moduleB:streamB"));
        module_list.push_back(std::string("moduleC:streamC/profileC"));
        CPPUNIT_ASSERT(dnf_module_enable(module_list));
    }
}
