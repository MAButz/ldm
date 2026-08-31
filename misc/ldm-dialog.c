/*
 * LDM Dialog
 * Copyright (2009) Ryan Niebur <RyanRyan52@gmail.com>

 * Author: Ryan Niebur <RyanRyan52@gmail.com>

 * 2009, Ryan Niebur <RyanRyan52@gmail.com>

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, you can find it on the World Wide
 * Web at http://www.gnu.org/copyleft/gpl.html, or write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.

 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <locale.h>
#include <libintl.h>
#include <unistd.h>
#define _(text) gettext(text)

#define NOTHING 0
#define MESSAGE 1
#define QUESTION 2
#define PROGRESS 3

#define AUTO_CLOSE_OPTION 100

GtkWidget *progress, *yes, *no;
double current_number;
GIOChannel *g_stdin;
int auto_close;

void
yes_clicked(GtkWidget * widget, GtkWidget * mywin)
{
    gtk_main_quit();
    exit(0);
}

void
no_clicked(GtkWidget * widget, GtkWidget * mywin)
{
    gtk_main_quit();
    exit(1);
}

void
link_clicked(GtkWidget * widget, GtkLabel * mywin)
{
    printf("url: %s\n", gtk_label_get_current_uri(GTK_LABEL(widget)));
    gtk_main_quit();
    exit(0);
}

void
update_progressbar()
{
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress),
                                  current_number / 100.0);
    if (current_number < 100.0) {
        if (yes)
            gtk_widget_set_sensitive(yes, FALSE);
    } else {
        if (auto_close == TRUE)
            yes_clicked(NULL, NULL);
        if (yes) {
            gtk_widget_set_sensitive(yes, TRUE);
            gtk_widget_grab_focus(yes);
        }
    }
}

void
update_progressbar_from_channel(GIOChannel * channel)
{
    GString *buf;
    buf = g_string_new("");
    g_io_channel_read_line_string(channel, buf, NULL, NULL);
    current_number = atof(buf->str);
    update_progressbar();
    g_string_free(buf, TRUE);
    g_io_channel_flush(g_stdin, NULL);
}

void
usage(char *progname)
{
    printf
        ("Usage: %s --message|--question|--progress [mode specific options] \"message\"\n",
         progname);
    printf("Progress mode options: --auto-close\n");
    exit(1);
}

#define USAGE() usage(argv[0])

int
main(int argc, char **argv)
{
    GtkWidget *mywin, *vbox, *hbox, *label, *spacer, *big_hbox, *ospacer,
        *o2spacer;
    int mode;
    int retval;
    char *message;
    int has_no_button, has_yes_button;
    mode = NOTHING;
    has_no_button = TRUE;
    has_yes_button = TRUE;
    auto_close = FALSE;

    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    // Option processing
    struct option longopts[] = {
        {"message", 0, 0, MESSAGE},
        {"question", 0, 0, QUESTION},
        {"progress", 0, 0, PROGRESS},
        {"auto-close", 0, 0, AUTO_CLOSE_OPTION},
        {0, 0, 0, 0}
    };
    while (1) {
        retval = getopt_long(argc, argv, "", longopts, NULL);
        if (retval == -1)
            break;
        switch (retval) {
        case MESSAGE:
        case QUESTION:
        case PROGRESS:
            if (mode != NOTHING)
                USAGE();
            mode = retval;
            break;
        case AUTO_CLOSE_OPTION:
            auto_close = TRUE;
            break;
        default:
            USAGE();
            break;
        }
    }
    if (mode == NOTHING)
        USAGE();
    if ((argc - optind) != 1)
        USAGE();
    if (auto_close == TRUE && mode != PROGRESS)
        USAGE();
    message = argv[optind];
    switch (mode) {
    case MESSAGE:
        has_no_button = FALSE;
        break;
    case QUESTION:
        break;
    case PROGRESS:
        if (auto_close == TRUE)
            has_yes_button = FALSE;
        break;
    }

    // Set up the basic stuff
    gtk_init(&argc, &argv);

    // Theme (GTK3 uses CSS, not the old .gtkrc "style"/"engine" mechanism)
    char *ldm_theme;
    gchar *ldm_css;
    ldm_theme = getenv("LDM_THEME");
    if (ldm_theme) {
        if (*ldm_theme == '/')
            ldm_css = g_strconcat(ldm_theme, "/greeter.css", NULL);
        else
            ldm_css =
                g_strconcat(LDM_THEME_DIR, ldm_theme, "/greeter.css",
                            NULL);
    } else
        ldm_css =
            g_strconcat(LDM_THEME_DIR, "default", "/greeter.css", NULL);
    if (access(ldm_css, R_OK) == 0) {
        GtkCssProvider *css_provider = gtk_css_provider_new();
        GError *css_error = NULL;
        gtk_css_provider_load_from_path(css_provider, ldm_css, &css_error);
        if (css_error) {
            g_error_free(css_error);
        } else {
            gtk_style_context_add_provider_for_screen(
                gdk_screen_get_default(),
                GTK_STYLE_PROVIDER(css_provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
        g_object_unref(css_provider);
    }
    g_free(ldm_css);
    // Finish setting up the basic stuff
    mywin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gdk_window_set_cursor(gdk_get_default_root_window(),
                          gdk_cursor_new_for_display(
                              gdk_display_get_default(), GDK_LEFT_PTR));
    gtk_window_set_title(GTK_WINDOW(mywin), "");
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mywin), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(mywin), 0);
    gtk_window_set_position(GTK_WINDOW(mywin), GTK_WIN_POS_CENTER_ALWAYS);
    g_signal_connect(GTK_WINDOW(mywin), "destroy", G_CALLBACK(no_clicked),
                     mywin);
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    spacer = gtk_label_new("");
    ospacer = gtk_label_new("");
    o2spacer = gtk_label_new("");
    gtk_widget_set_size_request(ospacer, 0, 8);
    gtk_widget_set_size_request(o2spacer, 0, 8);
    gtk_box_pack_start(GTK_BOX(hbox), spacer, TRUE, FALSE, 0);

    // Make the yes button
    if (has_yes_button == TRUE) {
        yes = gtk_button_new_with_mnemonic(_("_OK"));
        g_signal_connect(G_OBJECT(yes), "clicked", G_CALLBACK(yes_clicked),
                         mywin);
        gtk_box_pack_start(GTK_BOX(hbox), yes, FALSE, FALSE, 0);
    }
    // Make the no button
    if (has_no_button == TRUE) {
        no = gtk_button_new_with_mnemonic(_("_Cancel"));
        g_signal_connect(G_OBJECT(no), "clicked", G_CALLBACK(no_clicked),
                         mywin);
        gtk_box_pack_start(GTK_BOX(hbox), no, FALSE, FALSE, 0);
    }
    // Create the label
    label = gtk_label_new(message);
    gtk_label_set_markup(GTK_LABEL(label), message);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(mywin), 300, -1);
    g_signal_connect(GTK_LABEL(label), "activate-link",
                     G_CALLBACK(link_clicked), label);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *blah;
    blah = gtk_alignment_new(0, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(blah), label);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(blah), FALSE, FALSE, 10);

    if (mode == PROGRESS) {
        // Make the progress bar
        progress = gtk_progress_bar_new();
        gtk_box_pack_start(GTK_BOX(vbox), progress, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), o2spacer, FALSE, FALSE, 0);

        // Set the starting value
        current_number = 0.0;
        update_progressbar();

        // Set up the hook that listens for input
        g_stdin = g_io_channel_unix_new(0);
        g_io_add_watch(g_stdin, G_IO_IN,
                       (GIOFunc) update_progressbar_from_channel, g_stdin);
    }
    // More basic GTK stuff
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), ospacer, FALSE, FALSE, 0);
    big_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(big_hbox), vbox, TRUE, TRUE, 10);
    gtk_container_add(GTK_CONTAINER(mywin), big_hbox);
    gtk_widget_show_all(mywin);
    gtk_main();
    return 0;
}
