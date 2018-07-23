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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "ConfigModule.hpp"
#include "Const.hpp"

namespace libdnf {

class ConfigModule::Impl {
    friend class ConfigModule;

    Impl(Config & owner) : owner(owner) {}

    Config & owner;

    OptionString name{""};
    OptionBinding nameBinding{owner, name, "name"};

    OptionString stream{""};
    OptionBinding streamBinding{owner, stream, "stream"};

    OptionStringList profiles{std::vector<std::string>()};
    OptionBinding profilesBinding{owner, profiles, "profiles"};

    OptionNumber<std::int32_t> version{-1};
    OptionBinding versionBinding{owner, version, "version"};

    OptionBool enabled{false};
    OptionBinding enabledBinding{owner, enabled, "enabled"};

    OptionBool locked{false};
    OptionBinding lockedBinding{owner, locked, "locked"};
};

ConfigModule::ConfigModule() : pImpl(new Impl(*this)) {}
ConfigModule::ConfigModule(ConfigModule && src) : pImpl(std::move(src.pImpl)) {}
ConfigModule::~ConfigModule() = default;

OptionString & ConfigModule::name() { return pImpl->name; }
OptionString & ConfigModule::stream() { return pImpl->stream; }
OptionStringList & ConfigModule::profiles() { return pImpl->profiles; }
OptionNumber<std::int32_t> & ConfigModule::version() { return pImpl->version; }
OptionBool & ConfigModule::enabled() { return pImpl->enabled; }
OptionBool & ConfigModule::locked() { return pImpl->locked; }

}
