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

#include <glib/gi18n.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "prefs.h"
#include "../src/ldminfo.h"
#include "greeter.h"

static GtkWidget *pref_combo = NULL;
static gint choice_total = 0;

extern GIOChannel *g_stdout;

typedef struct {
    GtkWidget *prefwin;
    const gchar *prefname;
} PrefData;

/*
 * Local functions
 */

static void
prefwin_accept(GtkWidget * widget, PrefData * data)
{
    GreeterPref *pref = greeter_pref_get_pref(data->prefname);
    gchar *notif;

    pref->value.int_val =
        gtk_combo_box_get_active(GTK_COMBO_BOX(pref_combo));
    gtk_widget_destroy(data->prefwin);

    notif = g_strconcat("@", data->prefname, "@\n", NULL);
    g_io_channel_write_chars(g_stdout, notif, -1, NULL, NULL);
    g_io_channel_flush(g_stdout, NULL);
    g_free(notif);

    g_free(data);
}

/*
 * External functions
 */

void
populate_pref_combo_box(const char *choice, GtkWidget * pref_combo_box)
{
    gtk_combo_box_append_text(GTK_COMBO_BOX(pref_combo_box),
                              g_strdup(choice));
    ++choice_total;
}

void
prefwin(GtkWidget * widget, gpointer pref_name)
{

    GreeterPref *pref;
    GtkWidget *prefwin, *label, *vbox, *buttonbox;
    GtkWidget *cancel, *accept, *frame;

    pref_combo = gtk_combo_box_new_text();
    pref = greeter_pref_get_pref(pref_name);

    /*
     * Populate lang with default host hash
     */

    gtk_combo_box_append_text(GTK_COMBO_BOX(pref_combo),
                              g_strdup(_("Default")));
    g_list_foreach(pref->choices,
                   (GFunc) populate_pref_combo_box, pref_combo);

    gtk_combo_box_set_active(GTK_COMBO_BOX(pref_combo),
                             pref->value.int_val);

    /*
     * Build window
     */

    prefwin = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_position((GtkWindow *) prefwin,
                            GTK_WIN_POS_CENTER_ALWAYS);
    gtk_window_set_modal((GtkWindow *) prefwin, TRUE);

    vbox = gtk_vbox_new(FALSE, 0);
    buttonbox = gtk_hbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);

    cancel = gtk_button_new_from_stock("gtk-cancel");
    g_signal_connect(G_OBJECT(cancel), "clicked",
                     G_CALLBACK(destroy_popup), prefwin);

    accept = gtk_button_new_from_stock("gtk-ok");
    PrefData *data = g_malloc0(sizeof(PrefData));
    data->prefwin = prefwin;
    data->prefname = pref_name;
    g_signal_connect(G_OBJECT(accept), "clicked",
                     G_CALLBACK(prefwin_accept), data);

    gtk_box_pack_end((GtkBox *) buttonbox, (GtkWidget *) accept, FALSE,
                     FALSE, 0);
    gtk_box_pack_end((GtkBox *) buttonbox, (GtkWidget *) cancel, FALSE,
                     FALSE, 0);

    label = gtk_label_new("");
    gtk_label_set_markup((GtkLabel *) label, _(pref->title));

    gtk_box_pack_start((GtkBox *) vbox, (GtkWidget *) label, FALSE, FALSE,
                       0);
    gtk_box_pack_start((GtkBox *) vbox, (GtkWidget *) pref_combo, FALSE,
                       FALSE, 5);
    gtk_box_pack_start((GtkBox *) vbox, (GtkWidget *) buttonbox, TRUE,
                       TRUE, 5);

    frame = gtk_frame_new("");
    gtk_frame_set_shadow_type((GtkFrame *) frame, GTK_SHADOW_OUT);
    gtk_frame_set_label_align((GtkFrame *) frame, 1.0, 0.0);

    gtk_container_add(GTK_CONTAINER(frame), vbox);
    gtk_container_add(GTK_CONTAINER(prefwin), frame);

    gtk_widget_show_all(prefwin);

    return;
}
