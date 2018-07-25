/*
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or(at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */
/**
 * SECTION:dnf-module
 * @short_description: Module management
 * @include: dnf-module.h
 * @stability: Unstable
 *
 * High level interface for managing modules.
 */

#include <sstream>
#include <memory>

#include "dnf-module.h"
#include "log.hpp"
#include "nsvcap.hpp"
#include "hy-util.h"
#include "conf/ConfigParser.hpp"
#include "conf/ConfigModule.hpp"
#include "module/ModulePackage.hpp"
#include "module/ModulePackageMaker.hpp"
#include "module/ModulePackageContainer.hpp"
#include "module/modulemd/ModuleDefaultsContainer.hpp"
#include "utils/File.hpp"
#include "utils/utils.hpp"

#include <iostream>
// auto logger(libdnf::Log::getLogger());
class MyLogger : Logger {
public:
    void write(int source, time_t time, pid_t pid, Level level, const std::string & message) {
        std::cout << source << " " << time << " " << pid << " " << levelToCStr(level) << ": " << message << std::endl;
    }

} myLogger;
static Logger * logger((Logger *)&myLogger);

namespace {

// TODO: remove hard-coded path
constexpr const char * MODULESDIR = "/etc/dnf/modules.d";
constexpr const char * MODULEDEFAULTSDIR = "/etc/dnf/modules.defaults.d";

const auto PRIO = libdnf::Option::Priority::DEFAULT;

void dnf_module_parse_spec(const std::string specStr, libdnf::Nsvcap & parsed)
{
    std::ostringstream oss;
    oss << "Parsing module_spec=" << specStr;
    logger->debug(oss.str());

    parsed.clear();

    for (std::size_t i = 0;
         HY_MODULE_FORMS_MOST_SPEC[i] != _HY_MODULE_FORM_STOP_;
         ++i) {
        if (parsed.parse(specStr.c_str(), HY_MODULE_FORMS_MOST_SPEC[i])) {
            std::ostringstream().swap(oss);
            oss << "N:S:V:C:A/P = ";
            oss << parsed.getName();
            oss << parsed.getStream();
            oss << parsed.getVersion();
            oss << parsed.getArch();
            oss << parsed.getProfile();

            logger->debug(oss.str());
            return;
        }
    }

    std::ostringstream().swap(oss);
    oss << "Invalid spec '";
    oss << specStr << "'";
    logger->debug(oss.str());
    throw libdnf::ModuleException(oss.str());
}

std::string getFileContent(const std::string &path)
{
    auto yaml = libdnf::File::newFile(path);
    yaml->open("r");
    const auto &yamlContent = yaml->getContent();
    yaml->close();

    return yamlContent;
}

void enableModuleStreams(ModulePackageContainer &packages,
                         const char *install_root)
{
    std::string dirPath = g_build_filename(install_root, MODULESDIR, NULL);

    libdnf::ConfigParser parser{};
    for (const auto &file : filesystem::getDirContent(dirPath)) {
        parser.read(file);
    }

    for (const auto &iter : parser.getData()) {
        const auto &name = iter.first;
        libdnf::OptionBool enabled{false};

        if (!enabled.fromString(parser.getValue(name, "enabled"))) {
            continue;
        }
        const auto &stream = parser.getValue(name, "stream");
        packages.enable(name, stream);
    }
}

void createConflictBetweenStreams(const std::map<Id,
                                  std::shared_ptr<ModulePackage>> &modules)
{
    // TODO: use libdnf::Query for filtering
    for (const auto &iter : modules) {
        const auto &package = iter.second;

        for (const auto &innerIter : modules) {
            if (package->getName() == innerIter.second->getName() &&
                package->getStream() != innerIter.second->getStream()) {
                package->addStreamConflict(innerIter.second);
            }
        }
    }
}

void readModuleMetadataFromRepo(const GPtrArray *repos,
                                ModulePackageContainer & packages,
                                ModuleDefaultsContainer & defaults,
                                const char *install_root,
                                const char *platformModule)
{
    auto pool = packages.getPool();

    for (unsigned int i = 0; i < repos->len; i++) {
        auto repo = static_cast<DnfRepo *>(g_ptr_array_index(repos, i));
        auto modules_fn = dnf_repo_get_filename_md(repo, "modules");
        if (modules_fn == nullptr)
            continue;
        std::string yamlContent = getFileContent(modules_fn);

        auto modules = ModulePackageMaker::fromString(pool.get(), dnf_repo_get_repo(repo), yamlContent);
        createConflictBetweenStreams(modules);

        packages.add(modules);

        // update defaults from repo
        try {
            defaults.fromString(yamlContent, 0);
        } catch (ModuleDefaultsContainer::ConflictException &e) {
            logger->warning(e.what());
        }
    }
    createPlatformSolvable(pool.get(), "/etc/os-release", install_root, platformModule);
}

void readModuleDefaultsFromDisk(const std::string &path,
                                ModuleDefaultsContainer &defaults)
{
    for (const auto &file : filesystem::getDirContent(path)) {
        const auto &yamlContent = getFileContent(file);

        try {
            defaults.fromString(yamlContent, 1000);
        } catch (ModuleDefaultsContainer::ConflictException &e) {
            logger->warning(e.what());
        }
    }
}

std::string getConfFilepath(const char *install_root, const std::string &name)
{
    std::ostringstream filename;
    filename << name << ".module";
    return g_build_filename(install_root, MODULESDIR, filename.str().c_str(), NULL);
}

/*
 * Load module config if it exists, otherwise returns an empty config with just
 * module name set
 */
libdnf::ConfigModule loadModuleConf(const std::string &path,
                                    const std::string &name)
{
    libdnf::ConfigModule conf;

    conf.name().set(PRIO, name);

    if (!filesystem::exists(path)) {
        logger->debug("config does not exist");
        return conf;
    }

    libdnf::ConfigParser parser{};
    /* FIXME: should we check for exceptions? */
    parser.read(path);

    conf.stream().set(PRIO, parser.getValue(name, "stream"));
    conf.version().set(PRIO, parser.getValue(name, "version"));
    conf.profiles().set(PRIO, parser.getValue(name, "profiles"));
    conf.enabled().set(PRIO, parser.getValue(name, "enabled"));
    conf.locked().set(PRIO, parser.getValue(name, "locked"));

    return conf;
}

void saveModuleConf(const std::string &path, libdnf::ConfigModule &config)
{
    std::ofstream ofs;
    ofs.open(path);

    ofs << "[" << config.name().getValue() << "]" << "\n";
    ofs << "stream = " << config.stream().getValue() << "\n";
    ofs << "version = " << config.version().getValueString() << "\n";
    ofs << "profiles = ";
    /* Remove '[' and ']' from string */
    auto profiles = config.profiles().getValueString();
    if (profiles != "[]")
        ofs << profiles.substr(1, profiles.length() - 1);
    ofs << "\n";
    ofs << "enabled = " << config.enabled().getValueString() << "\n";
    ofs << "locked = " << config.locked().getValueString() << "\n";

    ofs.close();
}

bool module_exists(const std::string &name, const std::string &stream,
                   ModulePackageContainer & packages)
{
    auto modulePackages = packages.getModulePackages();

    for (const auto &module : modulePackages) {
        if (module->getName() == name && module->getStream() == stream)
            return true;
    }

    return false;
}

void enable_module_do(const libdnf::Nsvcap &spec,
                      ModulePackageContainer &packages,
                      ModuleDefaultsContainer &defaults,
                      const char *install_root)
{
    const auto &name = spec.getName();

    const auto cfgFilepath = getConfFilepath(install_root, name);

    /* read module config for current settings */
    auto modconf = loadModuleConf(cfgFilepath, name);

    /* find appropriate stream according to priority */
    std::string stream("");
    if (spec.getStream() != "") {
        stream = spec.getStream();
    } else if (modconf.enabled().getValue()) {
        stream = modconf.stream().getValue();
    } else {
        stream = defaults.getDefaultStreamFor(name);
    }

    if (!module_exists(name, stream, packages)) {
        std::ostringstream oss;
        oss << name << ":" << stream << " does not exist";
        throw libdnf::ModuleException(oss.str());
    }

    if (modconf.enabled().getValue() && modconf.stream().getValue() == stream) {
        std::ostringstream oss;
        oss << name << " already enabled. Nothing to do";
        logger->debug(oss.str());
        return;
    }

    {
        std::ostringstream oss;
        oss << "Enable '" << name << ":" << stream << "'";
        logger->debug(oss.str());
    }

    packages.enable(name, stream);

    modconf.enabled().set(PRIO, true);
    modconf.stream().set(PRIO, stream);
    /*
     * FIXME: if a previous stream was enabled, should we update the profile
     * list and version number?
     */
    saveModuleConf(cfgFilepath, modconf);

    /* FIXME: resolve module deps (TBD) */
}

}

namespace libdnf {

/**
 * dnf_module_dummy
 * @module_list: The list of modules
 *
 * Dummy module method
 *
 * Returns: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.0.0
 **/
bool dnf_module_dummy(const std::vector<std::string> & module_list)
{
    logger->debug("*** called dnf_module_dummy()");

    if (module_list.size() == 0) {
        return false;
    }

    int i = 0;
    for (const auto &module_spec : module_list) {
        std::ostringstream oss;
        oss << "module #" << i++ << " " << module_spec;
        logger->debug(oss.str());
    }

    return true;
}

/**
 * dnf_module_enable
 * @module_list: The list of module specs to enable
 * @sack: DnfSack instance
 * @repos: the list of repositories where to load modules from
 * @install_root
 *
 * Enable module method
 *
 * Returns: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.0.0
 */
bool dnf_module_enable(const std::vector<std::string> & module_list,
                       DnfSack *sack, GPtrArray *repos,
                       const char *install_root, const char *platformModule)
{
    ModuleExceptionList exList;

    if (module_list.empty())
        exList.add("module list cannot be empty");

    char *arch;
    hy_detect_arch(&arch);

    /* FIXME:
     * we should get this information from somewhere else so that we don't have
     * to gather the list of existing modules and their state every time this
     * API is called.
     */
    ModulePackageContainer packages{std::shared_ptr<Pool>(pool_create(), &pool_free), arch};
    ModuleDefaultsContainer defaults;

    readModuleMetadataFromRepo(repos, packages, defaults, install_root, platformModule);

    const std::string defaultsDirPath = g_build_filename(install_root, MODULEDEFAULTSDIR, NULL);
    readModuleDefaultsFromDisk(defaultsDirPath, defaults);

    try {
        defaults.resolve();
    } catch (ModuleDefaultsContainer::ResolveException &e) {
        exList.add(e.what());
    }

    enableModuleStreams(packages, install_root);

    for (const auto & spec : module_list) {
        libdnf::Nsvcap specParsed;

        try {
            dnf_module_parse_spec(spec, specParsed);

            std::ostringstream oss;
            oss << "Name = " << specParsed.getName() << "\n";
            oss << "Stream = " << specParsed.getStream() << "\n";
            oss << "Profile = " << specParsed.getProfile() << "\n";
            oss << "Version = " << specParsed.getVersion() << "\n";
            logger->debug(oss.str());
        } catch (ModuleException &e) {
            exList.add(e);
            continue;
        }

        try {
            enable_module_do(specParsed, packages, defaults, install_root);
        } catch (ModuleException &e) {
            exList.add(e);
        }
    }

    dnf_sack_filter_modules(sack, repos, install_root, platformModule);

    free(arch);

    if (!exList.empty())
        throw exList;

    return true;
}

/**
 * dnf_module_query
 * @sack: DnfSack instance
 * @repos: the list of repositories from which to load modules
 * @install_root: directory relative to which all files are read/written
 * @filters: query filtering options
 *
 * Query module method
 *
 * Returns: list of modules
 *
 * Since: 999999.0.0
 */
std::vector<std::shared_ptr<ModulemdModule> >
dnf_module_query(DnfSack *sack,
                 GPtrArray *repos,
                 const char *install_root,
                 const char *platformModule,
                 HyQuery filters)
{
    ModuleExceptionList exList;

    std::ostringstream oss;
    oss << "dnf_module_query()\n";
    oss << "install_root = " << install_root << "\n";
    oss << "repo count = " << repos->len << "\n";
    for (guint i = 0; i < repos->len; i++) {
        auto repo = static_cast<DnfRepo *>(g_ptr_array_index(repos, i));
        oss << "repo #" << i << " " << dnf_repo_get_id(repo) << "\n";
    }
    logger->debug(oss.str());

    char *arch;
    hy_detect_arch(&arch);

    /* FIXME:
     * we should get this information from somewhere else so that we don't have
     * to gather the list of existing modules and their state every time this
     * API is called.
     */
    ModulePackageContainer packages{std::shared_ptr<Pool>(pool_create(), &pool_free), arch};
    ModuleDefaultsContainer defaults;

    readModuleMetadataFromRepo(repos, packages, defaults, install_root, platformModule);

    const std::string defaultsDirPath = g_build_filename(install_root, MODULEDEFAULTSDIR, NULL);
    readModuleDefaultsFromDisk(defaultsDirPath, defaults);

    try {
        defaults.resolve();
    } catch (ModuleDefaultsContainer::ResolveException &e) {
        exList.add(e.what());
    }

    enableModuleStreams(packages, install_root);

    auto modulePackages = packages.getModulePackages();

    // list defined modules for debugging
    std::ostringstream().swap(oss);
    for (const auto &module : modulePackages) {
        oss << "Got module " << module->getFullIdentifier() << "\n";
        auto moduleProfiles = module->getProfiles();
        oss << "  " << moduleProfiles.size() << " profiles:" << "\n";
        for (const auto &profile : moduleProfiles) {
            oss << "    " << profile->getName() << "\n";
        }
    }
    logger->debug(oss.str());

    // TODO: filter modules

    std::vector<std::shared_ptr<ModulemdModule>> results;
    for (const auto &module : modulePackages) {
        std::shared_ptr<ModulemdModule> modulemd;
        // TODO: extract Modulemd.Module data from module
        //   (is this even possible?)
        results.push_back(modulemd);
    }

    return results;
}

}
