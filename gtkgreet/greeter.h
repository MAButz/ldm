/*
 * LTSP Graphical GTK Greeter
 * Copyright (C) 2007 Francis Giraldeau, <francis.giraldeau@revolutionlinux.com>
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


#ifndef GREETER_H
#define GREETER_H

#include "../src/ldminfo.h"

#define MAXSTRSZ 255

/*
 * hostwin.c
 */

extern gchar host[MAXSTRSZ];
extern gint current_host_id;
extern gint selected_host_id;

void update_selected_host();
void populate_host_combo_box(const char *hostname,
                             GtkWidget * host_combo_box);
void hostwin(GtkWidget * widget, GtkWindow * win);

/*
 * greeter.c
 */

extern GList *host_list;
extern gint current_host_id;
extern gint selected_host_id;

void destroy_popup(GtkWidget * widget, GtkWidget * popup);

/*
 * langwin.c
 */

extern gchar language[MAXSTRSZ];
extern GtkWidget *lang_select;
extern gint lang_total;
extern gint lang_selected;
void update_selected_lang();
void populate_lang_combo_box(const char *lang, GtkWidget * lang_combo_box);
void langwin(GtkWidget * widget, GtkWindow * win);

/*
 * sesswin.c
 */

extern gchar session[MAXSTRSZ];
extern GtkWidget *sess_select;  /* session selection combo */
extern gint sess_total;
extern gint sess_selected;

void update_selected_sess();
void populate_sess_combo_box(const char *sess, GtkWidget * sess_combo_box);
void sesswin(GtkWidget * widget, GtkWindow * win);

#endif                          /* GREETER_H */
