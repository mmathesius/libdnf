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
#include <fnmatch.h>
#include <regex/regex.hpp>

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

namespace {

// TODO: remove hard-coded path
constexpr const char * MODULESDIR = "/etc/dnf/modules.d";
constexpr const char * MODULEDEFAULTSDIR = "/etc/dnf/modules.defaults.d";

const auto PRIO = libdnf::Option::Priority::DEFAULT;

static const bool DEBUG_DUMPS = false;

void dnf_module_parse_spec(const std::string specStr, libdnf::Nsvcap & parsed)
{
    auto logger = libdnf::Log::getLogger();
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
            oss << ":" << parsed.getStream();
            oss << ":" << parsed.getVersion();
            oss << ":" << parsed.getContext();
            oss << ":" << parsed.getArch();
            oss << "/" << parsed.getProfile();

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

#define WILD_NAME    "([^:/]*)"
#define WILD_STREAM  "([^:/]*)"
#define WILD_VERSION "([^:/]*)"
#define WILD_CONTEXT "([^:/]*)"
#define WILD_ARCH    "([^/]*)"
#define WILD_PROFILE "(.*)"

static const Regex WILD_FORM_REGEX[]{
    Regex("^" WILD_NAME ":" WILD_STREAM ":" WILD_VERSION ":" WILD_CONTEXT "::?" WILD_ARCH "\\/"  WILD_PROFILE "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM ":" WILD_VERSION ":" WILD_CONTEXT "::?" WILD_ARCH "\\/?" "()"         "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM ":" WILD_VERSION     "()"         "::"  WILD_ARCH "\\/"  WILD_PROFILE "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM ":" WILD_VERSION     "()"         "::"  WILD_ARCH "\\/?" "()"         "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM     "()"             "()"         "::"  WILD_ARCH "\\/"  WILD_PROFILE "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM     "()"             "()"         "::"  WILD_ARCH "\\/?" "()"         "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM ":" WILD_VERSION ":" WILD_CONTEXT       "()"      "\\/"  WILD_PROFILE "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM ":" WILD_VERSION     "()"               "()"      "\\/"  WILD_PROFILE "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM ":" WILD_VERSION ":" WILD_CONTEXT       "()"      "\\/?" "()"         "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM ":" WILD_VERSION     "()"               "()"      "\\/?" "()"         "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM     "()"             "()"               "()"      "\\/"  WILD_PROFILE "$", REG_EXTENDED),
    Regex("^" WILD_NAME ":" WILD_STREAM     "()"             "()"               "()"      "\\/?" "()"         "$", REG_EXTENDED),
    Regex("^" WILD_NAME     "()"            "()"             "()"         "::"  WILD_ARCH "\\/"  WILD_PROFILE "$", REG_EXTENDED),
    Regex("^" WILD_NAME     "()"            "()"             "()"         "::"  WILD_ARCH "\\/?" "()"         "$", REG_EXTENDED),
    Regex("^" WILD_NAME     "()"            "()"             "()"               "()"      "\\/"  WILD_PROFILE "$", REG_EXTENDED),
    Regex("^" WILD_NAME     "()"            "()"             "()"               "()"      "\\/?" "()"         "$", REG_EXTENDED)
};
static const std::size_t NUM_WILD_FORM_REGEX = sizeof(WILD_FORM_REGEX)/sizeof(WILD_FORM_REGEX[0]);

bool wildparse(libdnf::Nsvcap &parsed, const std::string &nsvcapStr, const std::size_t form)
{
    enum { NAME = 1, STREAM = 2, VERSION = 3, CONTEXT = 4, ARCH = 5, PROFILE = 6, _LAST_ };
    auto matchResult = WILD_FORM_REGEX[form].match(nsvcapStr.c_str(), false, _LAST_);
    if (!matchResult.isMatched() || matchResult.getMatchedLen(NAME) == 0)
        return false;
    parsed.setName(matchResult.getMatchedString(NAME));
    if (matchResult.getMatchedLen(VERSION) > 0
        && matchResult.getMatchedString(VERSION) != "*")
        parsed.setVersion(atoll(matchResult.getMatchedString(VERSION).c_str()));
    else
        parsed.setVersion(libdnf::Nsvcap::VERSION_NOT_SET);
    parsed.setStream(matchResult.getMatchedString(STREAM));
    parsed.setContext(matchResult.getMatchedString(CONTEXT));
    parsed.setArch(matchResult.getMatchedString(ARCH));
    parsed.setProfile(matchResult.getMatchedString(PROFILE));
    return true;
}

// parse module wildcard pattern matching spec
void dnf_module_parse_spec_wildcard(const std::string specStr, libdnf::Nsvcap & parsed)
{
    auto logger = libdnf::Log::getLogger();
    std::ostringstream oss;
    oss << "Parsing wildcard module_spec=" << specStr;
    logger->debug(oss.str());

    parsed.clear();

    for (std::size_t i = 0; i < NUM_WILD_FORM_REGEX; ++i) {
        if (wildparse(parsed, specStr, i)) {
            std::ostringstream().swap(oss);
            oss << "N:S:V:C:A/P = ";
            oss << parsed.getName();
            oss << ":" << parsed.getStream();
            oss << ":" << parsed.getVersion();
            oss << ":" << parsed.getContext();
            oss << ":" << parsed.getArch();
            oss << "/" << parsed.getProfile();

            logger->debug(oss.str());
            return;
        }
    }

    std::ostringstream().swap(oss);
    oss << "Invalid wildcard spec '";
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
    g_autofree gchar *dirPath = g_build_filename(install_root, MODULESDIR, NULL);

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
                                  ModulePackagePtr> &modules)
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
    auto logger = libdnf::Log::getLogger();
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
    auto logger = libdnf::Log::getLogger();

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
    g_autofree gchar *fp = g_build_filename(install_root, MODULESDIR, filename.str().c_str(), NULL);
    return std::string(fp);
}

/*
 * Load module config if it exists, otherwise returns an empty config with just
 * module name set
 */
libdnf::ConfigModule loadModuleConf(const std::string &path,
                                    const std::string &name)
{
    auto logger = libdnf::Log::getLogger();
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
    auto logger = libdnf::Log::getLogger();
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

void disable_module_do(const libdnf::Nsvcap &spec,
                       ModulePackageContainer &packages,
                       ModuleDefaultsContainer &defaults,
                       const char *install_root)
{
    auto logger = libdnf::Log::getLogger();
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

    if (!modconf.enabled().getValue() && modconf.stream().getValue() == stream) {
        std::ostringstream oss;
        oss << name << " already disabled. Nothing to do";
        logger->debug(oss.str());
        return;
    }

    {
        std::ostringstream oss;
        oss << "Disable '" << name << ":" << stream << "'";
        logger->debug(oss.str());
    }

    packages.disable(name, stream);

    modconf.enabled().set(PRIO, false);
    modconf.stream().set(PRIO, "");
    //modconf.version().set(PRIO, -1);
    saveModuleConf(cfgFilepath, modconf);

    /* FIXME: resolve module deps (TBD) */
}

static void
debug_dump_modules(const std::string message,
                   const std::vector<ModulePackagePtr> &modules,
                   ModuleDefaultsContainer &defaults)
{
    auto logger = libdnf::Log::getLogger();
    std::ostringstream oss;

    if (!DEBUG_DUMPS)
	return;

    if (message != "")
        logger->debug(message);

    oss << "Dumping " << modules.size() << " module(s)";
    logger->debug(oss.str());

    int i = 0;
    for (const auto &module : modules) {
        std::ostringstream().swap(oss);
        oss << " Module #" << ++i << ": " << module->getFullIdentifier();

        const auto defstream = defaults.getDefaultStreamFor(module->getName());
        if (module->getStream() == defstream)
            oss << " [d]";
        if (module->isEnabled())
            oss << " [e]";
        if (module->isDisabled())
            oss << " [DIS]";

        auto profiles = module->getProfiles();
        oss << " with " << profiles.size() << " profile(s):";
        for (const auto &profile : profiles) {
            oss << " " << profile->getName();
        }
        logger->debug(oss.str());
    }
}

static void
debug_dump_module_artifacts(const ModulePackagePtr &module)
{
    auto logger = libdnf::Log::getLogger();
    std::ostringstream oss;

    if (!DEBUG_DUMPS)
	return;

    auto artifacts = module->getArtifacts();

    oss << "Dumping " << artifacts.size() << " artifacts";
    oss << " for module " << module->getFullIdentifier() << ":";
    logger->debug(oss.str());

    for (const auto &artifact : artifacts) {
        std::ostringstream().swap(oss);
	oss << " " << artifact.c_str();
        logger->debug(oss.str());
    }
}

static void
debug_dump_module_default_streams(ModuleDefaultsContainer &defaults)
{
    auto logger = libdnf::Log::getLogger();
    std::ostringstream oss;

    if (!DEBUG_DUMPS)
	return;

    logger->debug("Dumping default stream(s)");

    const auto defaultStreams = defaults.getDefaultStreams();
    for (const auto &iter : defaultStreams) {
        std::ostringstream().swap(oss);
        oss << " " << iter.first << ":" << iter.second;
        logger->debug(oss.str());
    }
}

bool
cstring_match(const int cmpType, const char *name, const char *match)
{
    if (cmpType & HY_ICASE) {
        if (cmpType & HY_SUBSTR) {
            return strcasestr(name, match) != NULL;
        }
        if (cmpType & HY_EQ) {
            return strcasecmp(name, match) == 0;
        }
        if (cmpType & HY_GLOB) {
            return fnmatch(match, name, FNM_CASEFOLD) == 0;
        }
        return false;
    }

    if (cmpType & HY_SUBSTR) {
        return strstr(name, match) != NULL;
    }
    if (cmpType & HY_EQ) {
        return strcmp(name, match) == 0;
    }
    if (cmpType & HY_GLOB) {
        return fnmatch(match, name, 0) == 0;
    }
    return false;

}

std::vector<ModulePackagePtr>
filterModulesSpec(const libdnf::Filter &filter,
                  const std::vector<ModulePackagePtr> &modules)
{
    // auto logger = libdnf::Log::getLogger();
    const int cmpType = filter.getCmpType();
    std::vector<ModulePackagePtr> results;
    std::vector<libdnf::Nsvcap> parsedMatches;
    libdnf::ModuleExceptionList exList;

    // parse each of the matches once at the start
    for (const auto &match_union : filter.getMatches()) {
        const char *match = match_union.str;
        libdnf::Nsvcap parsedMatch;
        try {
            dnf_module_parse_spec_wildcard(match, parsedMatch);

            std::ostringstream oss;
            oss << "Parsed Match:\n";
            oss << "  Name    = " << parsedMatch.getName() << "\n";
            oss << "  Stream  = " << parsedMatch.getStream() << "\n";
            oss << "  Version = " << parsedMatch.getVersion() << "\n";
            oss << "  Context = " << parsedMatch.getContext() << "\n";
            oss << "  Arch    = " << parsedMatch.getArch() << "\n";
            oss << "  Profile = " << parsedMatch.getProfile() << "\n";
            // logger->debug(oss.str());
        } catch (libdnf::ModuleException &e) {
            exList.add(e);
            continue;
        }
        parsedMatches.push_back(parsedMatch);
    }

    for (const auto &module : modules) {

       for (const auto &parsedMatch : parsedMatches) {

            if (parsedMatch.getName() != "" && parsedMatch.getName() != "*" &&
                !cstring_match(cmpType,
                               module->getName().c_str(),
                               parsedMatch.getName().c_str()))
                continue;
            if (parsedMatch.getStream() != "" && parsedMatch.getStream() != "*" &&
                !cstring_match(cmpType,
                               module->getStream().c_str(),
                               parsedMatch.getStream().c_str()))
                continue;
            if (parsedMatch.getVersion() != libdnf::Nsvcap::VERSION_NOT_SET &&
                parsedMatch.getVersion() != std::stoll(module->getVersion()))
                continue;
            if (parsedMatch.getContext() != "" && parsedMatch.getContext() != "*" &&
                !cstring_match(cmpType,
                               module->getContext().c_str(),
                               parsedMatch.getContext().c_str()))
                continue;
            if (parsedMatch.getArch() != "" && parsedMatch.getArch() != "*" &&
                !cstring_match(cmpType,
                               module->getArchitecture(),
                               parsedMatch.getArch().c_str()))
                continue;
            if (parsedMatch.getProfile() != "" && parsedMatch.getProfile() != "*") {
                bool matched = false;
                for (const auto profile : module->getProfiles()) {
                    if (cstring_match(cmpType,
                                      profile->getName().c_str(),
                                      parsedMatch.getProfile().c_str())) {
                        matched = true;
                        break;
                    }
                }
                if (!matched)
                    continue;
            }
            results.push_back(module);
            break;
        } // for parsedMatch
    } // for module

    if (!exList.empty())
        throw exList;

    return results;
}

std::vector<ModulePackagePtr>
filterModulesLatest(const libdnf::Filter &filter,
                    const std::vector<ModulePackagePtr> &modules)
{
    std::map<std::string, ModulePackagePtr> latest;

    for (const auto &module : modules) {
        std::ostringstream oss;
        oss << module->getName() << ":" << module->getStream() << ":" << module->getContext()
            << ":" << module->getArchitecture();
        const std::string modkey = oss.str();
        const std::string modversion = module->getVersion();

        const auto & it = latest.find(modkey);
        if ( it == latest.end() ) {
            latest.insert(std::make_pair(modkey, module));
        } else {
            // replace if later version than saved module
            const std::string v = it->second->getVersion();
            if ( std::stoul(modversion) > std::stoul(v) )
                it->second =  module;
        }
    }

    std::vector<ModulePackagePtr> results;
    for (const auto &iter : latest) {
        results.push_back(iter.second);
    }

    return results;
}

std::vector<ModulePackagePtr>
filterModulesEnabled(const libdnf::Filter &filter,
                     const std::vector<ModulePackagePtr> &modules)
{
    std::vector<ModulePackagePtr> results;

    for (const auto &module : modules) {
        if (module->isEnabled())
            results.push_back(module);
    }

    return results;
}

std::vector<ModulePackagePtr>
filterModulesDisabled(const libdnf::Filter &filter,
                      const std::vector<ModulePackagePtr> &modules)
{
    std::vector<ModulePackagePtr> results;

    for (const auto &module : modules) {
        if (module->isDisabled())
            results.push_back(module);
    }

    return results;
}

std::vector<ModulePackagePtr>
filterModulesDefault(const libdnf::Filter &filter,
                     const std::vector<ModulePackagePtr> &modules,
                     ModuleDefaultsContainer &defaults)
{
    std::vector<ModulePackagePtr> results;

    for (const auto &module : modules) {
        const std::string defstream = defaults.getDefaultStreamFor(module->getName());
        if (module->getStream() == defstream)
            results.push_back(module);
    }

    return results;
}

std::vector<ModulePackagePtr>
filterModulesProfileRpmName(const libdnf::Filter &filter,
                     const std::vector<ModulePackagePtr> &modules)
{
    const int cmpType = filter.getCmpType();
    std::vector<ModulePackagePtr> results;

    for (const auto &module : modules) {
	bool matched = false;

	for (const auto &match_union : filter.getMatches()) {
	    const char *match = match_union.str;
	    for (const auto &profile : module->getProfiles()) {
		for (const auto &rpm : profile->getContent()) {
		    if (cstring_match(cmpType, rpm.c_str(), match)) {
		        matched = true;
			break;
                    }
		} // for rpm
                if (matched)
                    break;
	    } // for profile
            if (matched)
                break;
	} // for match
	if (matched)
	    results.push_back(module);
    } // for module

    return results;
}

std::vector<ModulePackagePtr>
filterModulesRpmNevra(const libdnf::Filter &filter,
                      const std::vector<ModulePackagePtr> &modules)
{
    // auto logger = libdnf::Log::getLogger();
    std::vector<ModulePackagePtr> results;

    for (const auto &module : modules) {
        debug_dump_module_artifacts(module);

	bool matched = false;
	for (const auto &match_union : filter.getMatches()) {
	    const char *match = match_union.str;


	    for (const auto &artifact : module->getArtifacts()) {
		if (cstring_match(HY_EQ, artifact.c_str(), match)) {
		    matched = true;
		    break;
		}
	    } // for artifact
            if (matched)
                break;
	} // for match
	if (matched)
	    results.push_back(module);
    } // for module

    return results;
}

std::vector<ModulePackagePtr>
filterModules(const std::vector<ModulePackagePtr> &modules,
              ModuleDefaultsContainer &defaults,
              const std::vector<libdnf::Filter> &filters)
{
    auto logger = libdnf::Log::getLogger();
    std::ostringstream oss;
    oss << "filterModules: " << modules.size() << " module(s), " << filters.size() << " filter(s)";
    logger->debug(oss.str());

    if (modules.empty() || filters.empty()) {
        // nothing to do
        return modules;
    }

    std::vector<ModulePackagePtr> results = modules;

    for (const auto &f : filters) {
        switch (f.getKeyname()) {
        case HY_MOD_LATEST:
            results = filterModulesLatest(f, results);
            break;
        case HY_MOD_ENABLED:
            results = filterModulesEnabled(f, results);
            break;
        case HY_MOD_DISABLED:
            results = filterModulesDisabled(f, results);
            break;
        case HY_MOD_DEFAULT:
            results = filterModulesDefault(f, results, defaults);
            break;
        case HY_MOD_NSVCAP:
            results = filterModulesSpec(f, results);
            break;
        case HY_MOD_PROFILE_RPMNAME:
            results = filterModulesProfileRpmName(f, results);
            break;
        case HY_MOD_ARTIFACT_NEVRA:
            results = filterModulesRpmNevra(f, results);
            break;
        default:
            throw libdnf::ModuleException("unknown filter");
            break;
        }
    }

    return results;
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
    auto logger = libdnf::Log::getLogger();
    logger->debug("*** called dnf_module_dummy()");

    if (module_list.size() == 0) {
        return false;
    }

    int i = 0;
    for (const auto &module_spec : module_list) {
        std::ostringstream oss;
        oss << "module #" << ++i << " " << module_spec;
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
    auto logger = libdnf::Log::getLogger();
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

    g_autofree gchar *defaultsDirPath = g_build_filename(install_root, MODULEDEFAULTSDIR, NULL);
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
 * dnf_module_disable
 * @module_list: The list of module specs to disable
 * @sack: DnfSack instance
 * @repos: the list of repositories where to load modules from
 * @install_root
 *
 * Disable module method
 *
 * Returns: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.0.0
 */
bool dnf_module_disable(const std::vector<std::string> & module_list,
                        DnfSack *sack, GPtrArray *repos,
                        const char *install_root,
                        const char *platformModule)
{
    auto logger = libdnf::Log::getLogger();
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

    g_autofree gchar *defaultsDirPath = g_build_filename(install_root, MODULEDEFAULTSDIR, NULL);
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
            disable_module_do(specParsed, packages, defaults, install_root);
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
std::vector<ModulePackagePtr>
dnf_module_query(DnfSack *sack,
                 GPtrArray *repos,
                 const char *install_root,
                 const char *platformModule,
                 const std::vector<Filter> filters)
{
    ModuleExceptionList exList;

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

    g_autofree gchar *defaultsDirPath = g_build_filename(install_root, MODULEDEFAULTSDIR, NULL);
    readModuleDefaultsFromDisk(defaultsDirPath, defaults);

    try {
        defaults.resolve();
    } catch (ModuleDefaultsContainer::ResolveException &e) {
        exList.add(e.what());
    }

    debug_dump_module_default_streams(defaults);

    enableModuleStreams(packages, install_root);

    const auto modulePackages = packages.getModulePackages();

    debug_dump_modules("BEFORE FILTERING", modulePackages, defaults);

    std::vector<ModulePackagePtr> filteredModulePackages;
    try {
        filteredModulePackages = filterModules(modulePackages, defaults, filters);
    } catch (ModuleException &e) {
        exList.add(e);
    }

    debug_dump_modules("AFTER FILTERING", filteredModulePackages, defaults);

    free(arch);

    if (!exList.empty())
        throw exList;

    return filteredModulePackages;
}

}
