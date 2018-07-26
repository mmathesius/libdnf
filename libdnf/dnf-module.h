/*
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef __DNF_MODULE_H
#define __DNF_MODULE_H

#include <vector>

#include "dnf-types.h"

namespace libdnf {

struct ModuleException : public std::runtime_error {
    explicit ModuleException(const std::string &what) : std::runtime_error(what) {}
};

class ModuleExceptionList : public std::exception {
public:
    explicit ModuleExceptionList() {}
    const std::vector<ModuleException> & list() const noexcept { return _list; }
    void add(const ModuleException &what) { _list.push_back(what); }
    void add(const std::string &what) { _list.push_back(ModuleException(what)); }
    bool empty() { return _list.empty(); }

private:
    std::vector<ModuleException> _list;
};

bool dnf_module_dummy(const std::vector<std::string> & module_list);
bool dnf_module_enable(const std::vector<std::string> & module_list, DnfSack *sack, GPtrArray *repos, const char *install_root, const char *platformModule);
bool dnf_module_disable(const std::vector<std::string> & module_list, DnfSack *sack, GPtrArray *repos, const char *install_root, const char *platformModule);

}

#endif /* __DNF_MODULE_H */
