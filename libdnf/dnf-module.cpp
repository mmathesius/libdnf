/*
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * SECTION:dnf-module
 * @short_description: Module management
 * @include: libdnf.h
 * @stability: Unstable
 *
 * High level interface for managing modules
 */

#include <iostream>

#include "nsvcap.hpp"
#include "hy-subject.h"
#include "dnf-module.hpp"

namespace libdnf {

static bool
dnf_module_parse_spec(const std::string module_spec, Nsvcap & spec)
{
    g_debug("%s(module_spec=\"%s\")", __func__, module_spec.c_str());

    bool is_spec_valid = false;
    spec.clear();
    for (std::size_t i = 0;
	 HY_MODULE_FORMS_MOST_SPEC[i] != _HY_MODULE_FORM_STOP_;
	 ++i) {
	auto form = HY_MODULE_FORMS_MOST_SPEC[i];

	if (spec.parse(module_spec.c_str(), form)) {
	    is_spec_valid = true;
	    break;
	}
    }

    if (!is_spec_valid) {
        g_debug("Invalid module spec: \"%s\"", module_spec.c_str());
        return false;
    }

    g_debug("N:S:V:C:A/P = %s:%s:%lld:%s:%s/%s",
            spec.getName().c_str(), spec.getStream().c_str(),
            spec.getVersion(), spec.getContext().c_str(),
            spec.getArch().c_str(), spec.getProfile().c_str());

    return true;
}

bool
dnf_module_dummy(DnfContext *context, const std::vector<std::string> & module_list)
{
    const gchar *install_root = dnf_context_get_install_root(context);

    std::cerr << "install_root = " << install_root << std::endl;

    for (auto module_spec : module_list) {
        Nsvcap spec;
        std::cerr << "module " << module_spec << std::endl;

        if (!dnf_module_parse_spec(module_spec, spec)) {
            std::cerr << module_spec << " is not a valid spec" << std::endl;
            continue;
        }

        std::cout << "Name: " << spec.getName() << std::endl;
        std::cout << "Stream: " << spec.getStream() << std::endl;
        std::cout << "Version: " << spec.getVersion() << std::endl;
        std::cout << "Context: " << spec.getContext() << std::endl;
        std::cout << "Arch: " << spec.getArch() << std::endl;
        std::cout << "Profile: " << spec.getProfile() << std::endl;
    }

    return true;
}

bool
dnf_module_enable(DnfContext *context, const std::vector<std::string> & module_list)
{
    ModuleExceptionList exceptions;
    const gchar *install_root = dnf_context_get_install_root(context);

    std::cerr << "install_root = " << install_root << std::endl;

    if (module_list.empty()) {
        throw ModuleCommandException("module_list cannot be null");
    }

    for (const auto& module_spec : module_list) {
        Nsvcap spec;

        std::cout << "Parsing module_spec \"" << module_spec << "\"" << std::endl;

        if (!dnf_module_parse_spec(module_spec, spec)) {
            std::ostringstream oss;
            oss << module_spec << " is not a valid spec";
            exceptions.push_back(ModuleCommandException(oss.str()));
            continue;
        }

        std::cout << "Name: " << spec.getName() << std::endl;
        std::cout << "Stream: " << spec.getStream() << std::endl;
        std::cout << "Version: " << spec.getVersion() << std::endl;
        std::cout << "Context: " << spec.getContext() << std::endl;
        std::cout << "Arch: " << spec.getArch() << std::endl;
        std::cout << "Profile: " << spec.getProfile() << std::endl;

        /* FIXME: check that module exists */

        /* FIXME: resolve module deps */

        /* FIXME: enable module */

        /* FIXME: where are we getting sack, repos and install_root from?
        dnf_sack_filter_modules(sack, repos, install_root);
        */
    }

    if (!exceptions.empty())
        throw ModuleException(exceptions);

    return true;
}

std::vector<std::shared_ptr<ModuleMetadata> >
dnf_module_query(DnfContext *context /*, filter options */)
{
    std::vector<std::shared_ptr<ModuleMetadata> > results;

    return results;
}

bool
dnf_module_list(DnfContext *context /*, options */)
{
    std::vector<std::shared_ptr<ModuleMetadata> > results;

    results = dnf_module_query(context);

    return true;
}

}
