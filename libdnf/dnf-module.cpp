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
 * @include: libdnf.h
 * @stability: Unstable
 *
 * High level interface for managing modules.
 */


#include <glib.h>

#include "dnf-module.h"

/**
 * dnf_module_dummy
 * @module_list: The list of modules
 * @error: a #GError or %NULL.
 *
 * Dummy module method
 *
 * Returns: %TRUE for success, %FALSE otherwise
 *
 * Since: 0.0.0
 **/
gboolean
dnf_module_dummy(GPtrArray *module_list,
                 GError **error)
{
    guint i;
    gchar *module_spec;

    g_debug("*** called dnf_module_dummy()");
    g_debug("*** module_list length = %u", module_list->len);

    if (module_list->len == 0) {
        g_set_error(error,
                    DNF_ERROR,
                    DNF_ERROR_FAILED,
                    "no modules");
        return FALSE;
    }

    for (i = 0; i < module_list->len; i++) {
        module_spec = (gchar *)g_ptr_array_index(module_list, i);
        g_debug("module #%u: %s", i+1, module_spec);
    }

    return TRUE;
}

