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

bool
dnf_module_dummy(const std::vector<std::string> & module_list)
{
    for (auto module_spec : module_list) {
        std::cerr << "module " << module_spec << std::endl;
    }

    return true;
}

bool
dnf_module_enable(const std::vector<std::string> & module_list)
{
    if (module_list.empty()) {
        throw std::runtime_error("module_list cannot be null");
    }

    for (const auto& module_spec : module_list) {
        Nsvcap spec;

        std::cout << "Parsing module_spec \"" << module_spec << "\"" << std::endl;
        bool is_spec_valid = false;
        for (std::size_t i = 0;
             HY_MODULE_FORMS_MOST_SPEC[i] != _HY_MODULE_FORM_STOP_;
             ++i) {
            auto form = HY_MODULE_FORMS_MOST_SPEC[i];

            if (spec.parse(module_spec.c_str(), form)) {
                is_spec_valid = true;
                break;
            }
        }

        /* FIXME: throw exception? */
        if (!is_spec_valid) {
            std::cerr << "Invalid spec \"" << module_spec << "\"" << std::endl;
            continue;
        }

        std::cout << "Name: " << spec.getName() << std::endl;
        std::cout << "Stream: " << spec.getStream() << std::endl;
        std::cout << "Profile: " << spec.getProfile() << std::endl;
        std::cout << "Version: " << spec.getVersion() << std::endl;

        /* FIXME: check that module exists */

        /* FIXME: resolve module deps */

        /* FIXME: enable module */

        /* FIXME: apply filter_modules
        dnf_sack_filter_modules(...);
        */

        /* FIXME: throw exception in case of failure */
    }

    return true;
}

}
