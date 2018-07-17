#include "ModuleTest.hpp"

CPPUNIT_TEST_SUITE_REGISTRATION(ModuleTest);

#include "libdnf/dnf-module.hpp"
#include "libdnf/dnf-context.hpp"

using namespace libdnf;

void ModuleTest::setUp()
{
}

void ModuleTest::tearDown()
{
}

void ModuleTest::testDummy()
{
    DnfContext *ctx = dnf_context_new();
    dnf_context_set_install_root(ctx, "/tmp/install-root");

    std::cout << "called ModuleTest::testDummy()" << std::endl;

    /* call with empty module list should do nothing */
    {
        std::vector<std::string> module_list;
        CPPUNIT_ASSERT(dnf_module_dummy(ctx, module_list));
    }

    {
        std::vector<std::string> module_list;
        /* add some modules to the list and try again */
        module_list.push_back(std::string("moduleA"));
        module_list.push_back(std::string("moduleB:streamB"));
        module_list.push_back(std::string("moduleC:streamC/profileC"));

        CPPUNIT_ASSERT(dnf_module_dummy(ctx, module_list));
    }
}

void ModuleTest::testEnable()
{
    DnfContext *ctx = dnf_context_new();
    dnf_context_set_install_root(ctx, "/tmp/install-root");

    std::cout << "called ModuleTest::testEnable()" << std::endl;

    /* call with empty module list should throw exception */
    {
        std::vector<std::string> module_list;
        CPPUNIT_ASSERT_THROW(dnf_module_enable(ctx, module_list),
                             ModuleCommandException);
    }

    /* call with invalid module spec should throw exception */
    {
        std::vector<std::string> module_list;
        module_list.push_back(std::string("moduleA#wrong"));
        CPPUNIT_ASSERT_THROW(dnf_module_enable(ctx, module_list),
                             ModuleException);
    }

    /* call with invalid specs should throw exceptions */
    {
        std::vector<std::string> module_list;
        module_list.push_back(std::string("moduleA#wrong"));
        module_list.push_back(std::string("moduleB:streamB#wrong"));
        module_list.push_back(std::string("moduleC:streamC:versionC#wrong"));
        try {
            dnf_module_enable(ctx, module_list);
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
        CPPUNIT_ASSERT(dnf_module_enable(ctx, module_list));
    }
}

void ModuleTest::testList()
{
    DnfContext *ctx = dnf_context_new();

    std::cout << "called ModuleTest::testModList()" << std::endl;

    {
        CPPUNIT_ASSERT(dnf_module_list(ctx));
    }
}
