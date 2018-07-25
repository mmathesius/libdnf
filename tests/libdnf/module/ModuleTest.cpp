#include "ModuleTest.hpp"

CPPUNIT_TEST_SUITE_REGISTRATION(ModuleTest);

#include "libdnf/log.hpp"
#include "libdnf/dnf-module.h"
#include "libdnf/dnf-state.h"
#include "libdnf/dnf-repo-loader.h"
#include "libdnf/conf/ConfigParser.hpp"
#include "libdnf/conf/OptionBool.hpp"

#include "libdnf/utils/logger.hpp"
#include <iostream>

// auto logger(libdnf::Log::getLogger());
class MyLogger : Logger {
public:
    void write(int source, time_t time, pid_t pid, Level level, const std::string & message) {
        std::cout << source << " " << time << " " << pid << " " << levelToCStr(level) << ": " << message << std::endl;
    }

} myLogger;
static Logger * logger((Logger *)&myLogger);

void ModuleTest::setUp()
{
    GError *error = nullptr;
    context = dnf_context_new();

    dnf_context_set_release_ver(context, "f26");
    dnf_context_set_arch(context, "x86_64");
    constexpr auto install_root = TESTDATADIR "/modules/";
    dnf_context_set_install_root(context, install_root);
    constexpr auto repos_dir = TESTDATADIR "/modules/yum.repos.d/";
    dnf_context_set_repo_dir(context, repos_dir);
    dnf_context_set_solv_dir(context, "/tmp");
    dnf_context_setup(context, nullptr, &error);
    g_assert_no_error(error);

    dnf_context_setup_sack(context, dnf_state_new(), &error);
    g_assert_no_error(error);

    auto loader = dnf_context_get_repo_loader(context);
    auto repo = dnf_repo_loader_get_repo_by_id(loader, "test", &error);
    g_assert_no_error(error);
    g_assert(repo != nullptr);

    libdnf::Log::setLogger(logger);
}

void ModuleTest::tearDown()
{
    g_object_unref(context);
}

void ModuleTest::testDummy()
{
    std::vector<std::string> module_list;
    bool ret;

    logger->debug("called ModuleTest::testDummy()");

    /* call with empty module list should fail */
    ret = libdnf::dnf_module_dummy(module_list);
    CPPUNIT_ASSERT(ret == false);

    /* add some modules to the list and try again */
    module_list.push_back("moduleA");
    module_list.push_back("moduleB");
    module_list.push_back("moduleC");

    ret = libdnf::dnf_module_dummy(module_list);
    CPPUNIT_ASSERT(ret == true);
}

void ModuleTest::testEnable()
{
    GPtrArray *repos = dnf_context_get_repos(context);
    auto sack = dnf_context_get_sack(context);
    auto install_root = dnf_context_get_install_root(context);
    const char *platformModule = nullptr;

    logger->debug("called ModuleTest::testEnable()");

    /* call with empty module list should throw error */
    {
        std::vector<std::string> module_list;
        CPPUNIT_ASSERT_THROW(libdnf::dnf_module_enable(module_list, sack, repos, install_root, platformModule), libdnf::ModuleExceptionList);
    }

    /* call with invalid specs should fail */
    {
        std::vector<std::string> module_list{"moduleA:", "moduleB#streamB", "moduleC:streamC#profileC"};
        CPPUNIT_ASSERT_THROW(libdnf::dnf_module_enable(module_list, sack, repos, install_root, platformModule), libdnf::ModuleExceptionList);
    }

    /* call with valid specs should succeed */
    {
        std::vector<std::string> module_list{"httpd"};
        CPPUNIT_ASSERT(libdnf::dnf_module_enable(module_list, sack, repos, install_root, platformModule));
        libdnf::ConfigParser parser{};
        libdnf::OptionBool enabled{false};
        parser.read(TESTDATADIR "/modules/etc/dnf/modules.d/httpd.module");
        CPPUNIT_ASSERT(enabled.fromString(parser.getValue("httpd", "enabled")));
    }

    /* call with enabled module should succeed */
    {
        std::vector<std::string> module_list{"httpd:2.4"};
        CPPUNIT_ASSERT(libdnf::dnf_module_enable(module_list, sack, repos, install_root, platformModule));
        libdnf::ConfigParser parser{};
        libdnf::OptionBool enabled{false};
        parser.read(TESTDATADIR "/modules/etc/dnf/modules.d/httpd.module");
        CPPUNIT_ASSERT(enabled.fromString(parser.getValue("httpd", "enabled")));
    }
}

void ModuleTest::testQuery()
{
    std::ostringstream oss;
    GPtrArray *repos = dnf_context_get_repos(context);
    auto sack = dnf_context_get_sack(context);
    auto install_root = dnf_context_get_install_root(context);
    const char *platformModule = nullptr;

    oss << "called ModuleTest::testQuery()" ;
    logger->debug(oss.str());

    {
        std::vector<std::shared_ptr<ModulemdModule>> results;
        HyQuery filters = NULL;

        results = libdnf::dnf_module_query(sack, repos, install_root, platformModule, filters);

        std::ostringstream().swap(oss);
	oss << "query results count = " << results.size() << "\n";
        int i = 0;
        for (const auto &result : results) {
            oss << "result #" << i++ << " " << "\n";
        }
	logger->debug(oss.str());

        CPPUNIT_ASSERT(results.size() != 0);
    }
}
