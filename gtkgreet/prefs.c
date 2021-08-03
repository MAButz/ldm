/*
 * LTSP Graphical GTK Greeter
 * Copyright (C) 2010 Simon Poirier, <simon.poirier@revolutionlinux.com>
 *
 * - Queries servers to get information about them
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include <glib.h>
#include "prefs.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>

static GTree *greeter_prefs = NULL;

static int
g_strcmp(gconstpointer a, gconstpointer b)
{
    return strcmp((char *) a, (char *) b);
}

void
greeter_pref_init()
{
    if (greeter_prefs) {
        g_tree_destroy(greeter_prefs);
        greeter_prefs = NULL;
    }
    greeter_prefs = g_tree_new(g_strcmp);
}

GreeterPref *
greeter_pref_new(const gchar * name)
{
    GreeterPref *pref = g_malloc0(sizeof(GreeterPref));
    g_tree_replace(greeter_prefs, g_strdup(name), pref);
    pref->choices = g_list_alloc();

    return pref;
}

void
greeter_pref_destroy(const gchar * name)
{
    gpointer key;
    gpointer valptr;
    GreeterPref *value;

    g_tree_lookup_extended(greeter_prefs, name, &key, &valptr);
    value = (GreeterPref *) valptr;
    if (value->choices) {
        g_list_foreach(value->choices, (GFunc) g_free, NULL);
        g_list_free(value->choices);
    }
    if (value->title) {
        g_free(value->title);
    }
    if (value->menu) {
        g_free(value->menu);
    }
    if (value->icon) {
        g_free(value->icon);
    }

    g_free(value);
    g_free(value);
    g_free(key);
}

void
greeter_pref_prompt(const gchar * name)
{
}

GreeterPref *
greeter_pref_get_pref(const gchar * name)
{
    return (GreeterPref *) g_tree_lookup(greeter_prefs, name);
}

PrefValue
greeter_pref_get_value(const gchar * name)
{
    GreeterPref *value =
        (GreeterPref *) g_tree_lookup(greeter_prefs, name);
    if (!value) {
        return (PrefValue) "None";
    }
    if (value->value.int_val == 0) {
        return (PrefValue) "None";
    }

    return (PrefValue) (char *) g_list_nth_data(value->choices,
                                                value->value.int_val);
}

void
greeter_pref_foreach(GTraverseFunc func, gpointer user_data)
{
    g_tree_foreach(greeter_prefs, func, user_data);
}
