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
#ifndef PREFS_H
#define PREFS_H

typedef enum {
    PREF_UNDEFINED = 0,
    PREF_CHOICE,
    PREF_STRING
} PrefType;

typedef union {
    gchar* str_val;
    int int_val;
} PrefValue;

typedef struct {
    PrefType type;
    PrefValue value;
    gchar* title;
    gchar* menu;
    gchar* icon;
    GList* choices;
} GreeterPref;


void greeter_pref_init();

GreeterPref* greeter_pref_new(const gchar* name);

void greeter_pref_destroy(const gchar* name);

GreeterPref* greeter_pref_get_pref(const gchar* name);

void greeter_pref_prompt(const gchar* name);

PrefValue greeter_pref_get_value(const gchar* name);

void greeter_pref_foreach(GTraverseFunc func, gpointer user_data);

#endif /* PREFS_H */
