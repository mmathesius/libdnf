#include "ModuleTest.hpp"

CPPUNIT_TEST_SUITE_REGISTRATION(ModuleTest);

#include "libdnf/dnf-module.h"


void ModuleTest::setUp()
{
}

void ModuleTest::tearDown()
{
}

void ModuleTest::testDummy()
{
    GPtrArray *module_list;
    GError *error;
    gboolean ret;

    g_debug("called ModuleTest::testDummy()");

    module_list = g_ptr_array_new ();

    /* call with empty module list should fail */
    error = nullptr;
    ret = dnf_module_dummy( module_list, &error);
    g_assert(ret == FALSE);
    g_assert_error(error, DNF_ERROR, DNF_ERROR_FAILED);

    /* add some modules to the list and try again */
    g_ptr_array_add(module_list, (gpointer)"moduleA");
    g_ptr_array_add(module_list, (gpointer)"moduleB");
    g_ptr_array_add(module_list, (gpointer)"moduleC");

    error = nullptr;
    ret = dnf_module_dummy( module_list, &error);
    g_assert(ret == TRUE);
    g_assert_no_error(error);

    g_ptr_array_free (module_list, TRUE);
}

