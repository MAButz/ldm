/*
 * LTSP Graphical GTK Greeter
 * Copyright (2007) Oliver Grawert <ogra@ubuntu.com>, Canonical Ltd.

 * Author: Oliver Grawert <ogra@canonical.com>

 * 2007, Oliver Grawert <ogra@canonical.com>
 *       Scott Balneaves <sbalneav@ltsp.org>
 * 2008, Ryan Niebur <RyanRyan52@gmail.com>
 *       Warren Togami <wtogami@redhat.com>
 *       Stéphane Graber <stgraber@ubuntu.com>
 *       Scott Balneaves <sbalneav@ltsp.org>
 *       Vagrant Cascadian <vagrant@freegeek.org>
 *       Oliver Grawert <ogra@canonical.com>
 *       John Ellson <john.ellson@comcast.net>
 *       Gideon Romm <gadi@ltsp.org>
 *       Jigish Gohil <cyberorg@opensuse.org>
 *       Wolfgang Schweer <schweer@cityweb.de>
 *       Toshio Kuratomi
 * 2012, Alkis Georgopoulos <alkisg@gmail.com>

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2.

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

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <cairo.h>
#include <config.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "../src/logging.h"
#include "greeter.h"
#include "prefs.h"
#include "prefwin.h"
#define _(text) gettext(text)

#define TOP_BAR_HEIGHT 30
#define BOTTOM_BAR_HEIGHT 30

/*
 *  Make sure that StatusMessages can accommodate many lines of text - auto scrollbars?
 */
GtkWidget *UserPrompt;          /* prompt area before the entry */
GtkWidget *StatusMessages;      /* Status msg area below entry */
GtkWidget *entry;               /* entry box */
GtkWidget *choiceCombo;
GtkListStore *choiceList;

GtkWidget *GuestButton;
GtkWidget *timeoutbox;
gboolean timeout_enabled;

GList *host_list = NULL;
GIOChannel *g_stdout;           /* stdout io channel */
gchar *ldm_theme_dir;

int allowguest;
gint login_timeout;
gint timeout_left;

gchar *
ldm_theme_file(char *file)
{
    gchar *filename;
    gchar *filename_default;
    filename = g_strconcat("/", ldm_theme_dir, "/", file, NULL);
    filename_default =
        g_strconcat("/", LDM_THEME_DIR, "ltsp", "/", file, NULL);
    if (access(g_strconcat(filename, ".png", NULL), F_OK) != -1) {
        filename = g_strconcat(filename, ".png", NULL);
    } else if (access(g_strconcat(filename, ".jpg", NULL), F_OK) != -1) {
        filename = g_strconcat(filename, ".jpg", NULL);
    } else if (access(g_strconcat(filename_default, ".png", NULL), F_OK) !=
               -1) {
        filename = g_strconcat(filename_default, ".png", NULL);
    } else if (access(g_strconcat(filename_default, ".jpg", NULL), F_OK) !=
               -1) {
        filename = g_strconcat(filename_default, ".jpg", NULL);
    }
    return filename;
}

void
get_default_display_size(gint * width, gint * height)
{
    GdkDisplay *display = NULL;
    GdkScreen *screen = NULL;
    GdkRectangle my_rect;

    display = gdk_display_get_default();
    screen = gdk_display_get_default_screen(display);
    gdk_screen_get_monitor_geometry(screen, 0, &my_rect);
    *width = my_rect.width;
    *height = my_rect.height;

    // Setze die Bildschirmnummer als Umgebungsvariable
   // Bildschirmnummer als Zeichenkette konvertieren
    gchar screen_number[10];
    sprintf(screen_number, "%d", gdk_screen_get_number(screen));

    // Setze die Bildschirmnummer als Umgebungsvariable
    setenv("SCREEN_NUMBER", screen_number, 1);

    // Protokolliere die Bildschirmnummer
    log_entry("gtkgreet", 3, "SCREEN_NUMBER: %s", screen_number);
}

GdkPixmap *root_bg = 0;
void
load_root_background(const gchar * filename, gboolean scale,
                     gboolean reload)
{
    if (root_bg != 0) {
        if (reload) {
            g_object_unref(G_OBJECT(root_bg));
            root_bg = 0;
        } else {
            return;
        }
    }

    GtkWidget *image = gtk_image_new_from_file(filename);
    g_object_ref(G_OBJECT(image));
    GdkPixbuf *pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(image));
    double img_width = (double) gdk_pixbuf_get_width(pixbuf);
    double img_height = (double) gdk_pixbuf_get_height(pixbuf);

    GdkWindow *root = gdk_get_default_root_window();
    gint width, height;
    get_default_display_size(&width, &height);

    // create pixmap
    root_bg = gdk_pixmap_new(GDK_DRAWABLE(root), width, height, -1);
    g_object_ref(G_OBJECT(root_bg));

    // paint pixmap onto bg
    cairo_t *ctx = gdk_cairo_create(GDK_DRAWABLE(root_bg));
    if (scale) {
        cairo_scale(ctx, width / img_width, height / img_height);
    }
    gdk_cairo_set_source_pixbuf(ctx, pixbuf, 0, 0);
    cairo_paint(ctx);
    cairo_destroy(ctx);

    //g_object_unref(G_OBJECT (bg));

    g_object_unref(G_OBJECT(image));
}

void
draw_background(GtkWidget * widget, gpointer data)
{
    GdkWindow *window;
    gint width, height, x, y;

    window = gtk_widget_get_window(widget);
    if (window == NULL) {
        return;
    }
    gdk_drawable_get_size(GDK_DRAWABLE(window), &width, &height);
    gdk_window_get_origin(GDK_WINDOW(window), &x, &y);
    GdkPixmap *new_bg = gdk_pixmap_new(root_bg, width, height, -1);
    g_object_ref(G_OBJECT(new_bg));
    gdk_draw_drawable(GDK_DRAWABLE(new_bg),
                      gdk_gc_new(GDK_DRAWABLE(new_bg)),
                      GDK_DRAWABLE(root_bg), x, y, 0, 0, width, height);
    gdk_window_set_back_pixmap(GDK_WINDOW(window), new_bg, 0);

    gtk_widget_queue_draw(GTK_WIDGET(widget));
}

static void
destroy(GtkWidget * widget, gpointer data)
{
    gtk_main_quit();
}

static void
spawn_command(GtkWidget * widget, const gchar * command)
{
    GError **error = NULL;
    g_spawn_command_line_async(command, error);
}

gboolean
update_time(GtkWidget * label)
{
    time_t timet;
    struct tm *timePtr;
    const gchar *clock_format;
    gchar label_markup[100];

    timet = time(NULL);
    timePtr = localtime(&timet);

    // Allow the users to customize the clock format including the GTK markup,
    // for example in case they want the date non-bold and the time in bold.
    clock_format=ldm_getenv_str_default("LDM_CLOCK_FORMAT", "%x, <b>%H:%M</b>");
    if (strftime(label_markup, sizeof(label_markup), clock_format, timePtr))
        gtk_label_set_markup((GtkLabel *) label, label_markup);

    return TRUE;
}

gboolean
update_timeout(GtkWidget * label)
{
    gchar *string;
    int entry_length;
    entry_length = strlen(gtk_entry_get_text((GtkEntry *) entry));
    if (entry_length == 0 && timeout_enabled) {
        if (timeout_left > 1) {
            timeout_left--;
        } else if (timeout_left == 1) {
            g_io_channel_write_chars(g_stdout, "@GUEST@\n", -1, NULL,
                                     NULL);
            g_io_channel_flush(g_stdout, NULL);
            timeout_left = 0;
            timeout_enabled = FALSE;
        } else if (timeout_left == 0) {
            timeout_left = login_timeout;
        }
        string =
            g_strdup_printf(_("Automatic login in %d seconds"),
                            timeout_left);
        gtk_label_set_markup((GtkLabel *) label, string);
        g_free(string);
        gtk_widget_show(timeoutbox);
    } else {
        timeout_left = 0;
        gtk_widget_hide(timeoutbox);
    }
    return TRUE;
}

void
destroy_popup(GtkWidget * widget, GtkWidget * popup)
{
    gtk_widget_destroy(popup);
    return;
}

gboolean
xproperty_exists(gchar * property)
{
    GdkAtom *actual_property_type = NULL;
    gint *actual_format = 0;
    gint *actual_length = 0;
    guchar **data = NULL;
    gboolean result;

    result = gdk_property_get(gdk_get_default_root_window(),
                              gdk_atom_intern(property, TRUE),
                              GDK_NONE,
                              0,
                              512,
                              FALSE,
                              actual_property_type,
                              actual_format, actual_length, data);
    return result;
}

gboolean
handle_command(GIOChannel * io_input)
{
    GString *buf;

    buf = g_string_new("");

    g_io_channel_read_line_string(io_input, buf, NULL, NULL);
    g_strstrip(buf->str);

    log_entry("gtkgreet", 7, "Got command: %s", buf->str);

    if (!g_ascii_strncasecmp(buf->str, "msg", 3)) {
        gchar **split_buf;
        split_buf = g_strsplit(buf->str, " ", 2);
        gtk_label_set_markup((GtkLabel *) StatusMessages, split_buf[1]);
        g_strfreev(split_buf);
    } else if (!g_ascii_strncasecmp(buf->str, "quit", 4)) {
        if (!xproperty_exists("X11VNC_TICKER")) {
            GdkCursor *cursor;
            cursor = gdk_cursor_new(GDK_WATCH);
            gdk_window_set_cursor(gdk_get_default_root_window(), cursor);
        }
        gtk_main_quit();
    } else if (!g_ascii_strncasecmp(buf->str, "prompt", 6)) {
        gchar **split_buf;
        split_buf = g_strsplit(buf->str, " ", 2);
        gtk_label_set_markup((GtkLabel *) UserPrompt, split_buf[1]);
        g_strfreev(split_buf);
    } else if (!g_ascii_strncasecmp(buf->str, "userid", 6)) {
        timeout_enabled = login_timeout > 0;
        if (timeout_enabled) {
            gtk_widget_show(timeoutbox);
        }
        gtk_widget_show(entry);
        gtk_widget_hide(choiceCombo);
        gtk_entry_set_text(GTK_ENTRY(entry), "");
        gtk_entry_set_visibility(GTK_ENTRY(entry), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(entry), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(GuestButton), TRUE);
    } else if (!g_ascii_strncasecmp(buf->str, "passwd", 6)) {
        timeout_enabled = FALSE;
        timeout_left = 0;
        gtk_widget_hide(timeoutbox);
        gtk_entry_set_text(GTK_ENTRY(entry), "");
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(entry), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(GuestButton), FALSE);
    } else if (!g_ascii_strncasecmp(buf->str, "hostname", 8)) {
        gchar *hoststr;
        update_selected_host();
        hoststr = g_strdup_printf("%s\n", host);
        g_io_channel_write_chars(g_stdout, hoststr, -1, NULL, NULL);
        g_io_channel_flush(g_stdout, NULL);
        g_free(hoststr);
        gtk_widget_set_sensitive(GTK_WIDGET(entry), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(GuestButton), FALSE);
    } else if (!g_ascii_strncasecmp(buf->str, "language", 8)) {
        gchar *langstr;
        update_selected_lang();
        langstr = g_strdup_printf("%s\n", language);
        g_io_channel_write_chars(g_stdout, langstr, -1, NULL, NULL);
        g_io_channel_flush(g_stdout, NULL);
        g_free(langstr);
    } else if (!g_ascii_strncasecmp(buf->str, "session", 7)) {
        gchar *sessstr;
        update_selected_sess();
        sessstr = g_strdup_printf("%s\n", session);
        g_io_channel_write_chars(g_stdout, sessstr, -1, NULL, NULL);
        g_io_channel_flush(g_stdout, NULL);
        g_free(sessstr);
    } else if (!g_ascii_strncasecmp(buf->str, "choice", 6)) {
        int i;
        GtkTreeIter iter;

        // strip newline char
        buf->len--;
        buf->str[buf->len] = '\0';

        // choice;choice 1|choice 2|choice 3
        gchar **choices = g_strsplit(buf->str + 7, "|", -1);

        // drop combo box to drop model
        gtk_list_store_clear(choiceList);

        for (i = 0; i < g_strv_length(choices); ++i) {
            gtk_list_store_append(GTK_LIST_STORE(choiceList), &iter);
            gtk_list_store_set(GTK_LIST_STORE(choiceList), &iter, 0,
                               choices[i], -1);
        }

        g_strfreev(choices);

        gtk_widget_hide(entry);
        gtk_widget_show(choiceCombo);
    } else if (!g_ascii_strncasecmp(buf->str, "pref choice", 11)) {
        int i;

        // strip newline char
        buf->len--;
        buf->str[buf->len] = '\0';

        // pref choice;key;title;menu;icon;choice 1|choice 2|choice 3
        gchar **args = g_strsplit(buf->str, ";", 6);
        gchar **choices = g_strsplit(args[5], "|", -1);

        GreeterPref *pref = greeter_pref_new(g_strdup(args[1]));
        pref->type = PREF_CHOICE;
        pref->title = g_strdup(args[2]);
        pref->menu = g_strdup(args[3]);
        pref->icon = g_strdup(args[4]);

        for (i = 0; i < g_strv_length(choices); ++i) {
            pref->choices =
                g_list_append(pref->choices, g_strdup(choices[i]));
        }

        g_strfreev(choices);
        g_strfreev(args);
    } else if (!g_ascii_strncasecmp(buf->str, "value", 5)) {
        gchar *name = buf->str + 6;
        gchar *valstr;

        // strip newline char
        buf->len--;
        buf->str[buf->len] = '\0';

        valstr =
            g_strdup_printf("%s\n", greeter_pref_get_value(name).str_val);
        g_io_channel_write_chars(g_stdout, valstr, -1, NULL, NULL);
        g_io_channel_flush(g_stdout, NULL);
        g_free(valstr);
    } else if (!g_ascii_strncasecmp(buf->str, "allowguest", 10)) {
        gchar *valstr = buf->str + 11;
        allowguest = g_ascii_strncasecmp(valstr, "false", 2);
        if (!allowguest)
            gtk_widget_hide(GTK_WIDGET(GuestButton));
        else
            gtk_widget_show(GTK_WIDGET(GuestButton));
    }

    g_string_free(buf, TRUE);
    return TRUE;
}

char *
get_sysname(void)
{
    struct utsname name;
    char *node;

    if (uname(&name) == 0) {
        node = strdup(name.nodename);
        return node;
    }
    return NULL;
}

static void
handle_guestbutton(GtkButton * entry, GdkWindow * window)
{
    g_io_channel_write_chars(g_stdout, "@GUEST@\n", -1, NULL, NULL);
    g_io_channel_flush(g_stdout, NULL);
}

static void
handle_choice(GtkComboBox * combo, GdkWindow * window)
{
    gchar *selection;
    gchar *entrystr;

    selection = gtk_combo_box_get_active_text(GTK_COMBO_BOX(choiceCombo));
    entrystr = g_strdup_printf("%s\n", selection);
    g_io_channel_write_chars(g_stdout, entrystr, -1, NULL, NULL);
    g_io_channel_flush(g_stdout, NULL);
    g_free(entrystr);
}

static void
handle_entry(GtkEntry * entry, GdkWindow * window)
{
    gchar *entrystr;

    entrystr = g_strdup_printf("%s\n", gtk_entry_get_text(entry));
    g_io_channel_write_chars(g_stdout, entrystr, -1, NULL, NULL);
    g_io_channel_flush(g_stdout, NULL);
    g_free(entrystr);
    if (gtk_entry_get_visibility(GTK_ENTRY(entry)))
        gtk_entry_set_text(entry, "");
}

static gboolean
menu_append_pref(gpointer akey, gpointer avalue, gpointer adata)
{
    char *key = (char *) akey;
    GreeterPref *pref = (GreeterPref *) avalue;
    GtkWidget *menu = (GtkWidget *) adata;
    GtkWidget *pref_item, *prefico;

    pref_item = gtk_image_menu_item_new_with_mnemonic(_(pref->menu));
    prefico = gtk_image_new_from_file(ldm_theme_file(pref->icon));
    gtk_image_menu_item_set_image((GtkImageMenuItem *) pref_item, prefico);

    g_signal_connect(G_OBJECT(pref_item), "activate",
                     G_CALLBACK(prefwin), key);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), pref_item);

    return FALSE;
}

static void
popup_menu(GtkWidget * widget, GtkWindow * window)
{
    GtkWidget *menu, *lang_item, *sess_item, *host_item, *custom_item,
        *quit_item, *reboot_item;
    GtkWidget *sep, *langico, *sessico, *hostico, *customico, *rebootico,
        *haltico;
    gchar custom_env_var[20], *custom_mnemonic, *custom_command;
    int i;

    menu = gtk_menu_new();

    if (getenv("LDM_FORCE_LANGUAGE") == NULL) {
        lang_item =
            gtk_image_menu_item_new_with_mnemonic(_
                                                  ("Select _Language ..."));
        langico = gtk_image_new_from_file(ldm_theme_file("language"));
        gtk_image_menu_item_set_image((GtkImageMenuItem *) lang_item,
                                      langico);
        g_signal_connect_swapped(G_OBJECT(lang_item), "activate",
                                 G_CALLBACK(langwin), window);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), lang_item);
    }

    if (getenv("LDM_FORCE_SESSION") == NULL) {
        sess_item =
            gtk_image_menu_item_new_with_mnemonic(_
                                                  ("Select _Session ..."));
        sessico = gtk_image_new_from_file(ldm_theme_file("session"));
        gtk_image_menu_item_set_image((GtkImageMenuItem *) sess_item,
                                      sessico);
        g_signal_connect_swapped(G_OBJECT(sess_item), "activate",
                                 G_CALLBACK(sesswin), window);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sess_item);
    }

    if (ldminfo_size() > 1) {
        host_item =
            gtk_image_menu_item_new_with_mnemonic(_("Select _Host ..."));
        hostico = gtk_image_new_from_file(ldm_theme_file("host"));
        gtk_image_menu_item_set_image((GtkImageMenuItem *) host_item,
                                      hostico);
        g_signal_connect_swapped(G_OBJECT(host_item), "activate",
                                 G_CALLBACK(hostwin), window);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), host_item);
    }

    for (i = 0; i <= 9; i++) {
        g_sprintf(custom_env_var, "LDM_MENU_ITEM_%d", i);
        custom_mnemonic = getenv(custom_env_var);
        if (custom_mnemonic == NULL)
            continue;
        custom_item =
            gtk_image_menu_item_new_with_mnemonic(custom_mnemonic);
        g_sprintf(custom_env_var, "LDM_MENU_COMMAND_%d", i);
        custom_command = getenv(custom_env_var);
        if (custom_command == NULL) {
            gtk_widget_destroy(custom_item);
            continue;
        }
        customico = gtk_image_new_from_file(ldm_theme_file("backend"));
        gtk_image_menu_item_set_image((GtkImageMenuItem *) custom_item,
                                      customico);
        g_signal_connect(G_OBJECT(custom_item), "activate",
                         G_CALLBACK(spawn_command),
                         g_strdup(custom_command));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), custom_item);
    }
    greeter_pref_foreach(menu_append_pref, menu);

    sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

    reboot_item = gtk_image_menu_item_new_with_mnemonic(_("_Reboot"));
    rebootico = gtk_image_new_from_file(ldm_theme_file("reboot"));
    gtk_image_menu_item_set_image((GtkImageMenuItem *) reboot_item,
                                  rebootico);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), reboot_item);
    g_signal_connect(G_OBJECT(reboot_item), "activate",
                     G_CALLBACK(spawn_command), "/sbin/reboot");

    quit_item = gtk_image_menu_item_new_with_mnemonic(_("Shut_down"));
    haltico = gtk_image_new_from_file(ldm_theme_file("shutdown"));
    gtk_image_menu_item_set_image((GtkImageMenuItem *) quit_item, haltico);
    g_signal_connect(G_OBJECT(quit_item), "activate",
                     G_CALLBACK(spawn_command), "/sbin/poweroff");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                   0, gtk_get_current_event_time());

    gtk_widget_show_all(menu);
    gtk_menu_reposition(GTK_MENU(menu));

    return;
}

/*
 * scopy()
 *
 * Copy a string.  Used to move data in and out of our ldminfo structure.
 * Note: if the source string is null, or points to a valid string of '\0',
 * both result in a dest string length of 0.
 */

char *
scopy(char *dest, char *source)
{
    if (!source)
        *dest = '\0';
    else {
        strncpy(dest, source, MAXSTRSZ - 1);
        *(dest + MAXSTRSZ - 1) = '\0';           /* ensure null termination */
    }

    return dest;
}

/*
 * Remap GDK_Tab to behaves as enter
 * Inspired by gdm/gui/gdmlogin.c
 */
static gboolean
key_press_event(GtkWidget * widget, GdkEventKey * event, gpointer window)
{
    if ((event->keyval == GDK_Tab ||
         event->keyval == GDK_KP_Tab) &&
        (event->
         state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK)) ==
        0) {
        handle_entry(GTK_ENTRY(entry), window);
        return TRUE;
    }

    return FALSE;
}

int
main(int argc, char *argv[])
{
    gint lw, lh;

    GtkWidget *loginWindow, *prefBar;
    GdkCursor *normcursor, *busycursor;
    GtkWidget *syslabel, *timelabel;
    GtkWidget *logo, *EntryBox;
    GtkWidget *timeoutspacer1, *timeoutspacer2, *timeoutlabel;
    GtkWidget *entryspacer1, *entryspacer2;
    GtkWidget *optionico;
    GtkWidget *optionbutton;
    GdkWindow *root;
    GdkPixbuf *pix;
    gint width, height;
    GIOChannel *g_stdin;
    char *ldm_theme;

#ifdef ENABLE_NLS
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif

    /* Init log settings */
    log_init(ldm_getenv_bool("LDM_SYSLOG"),
             ldm_getenv_int("LDM_LOGLEVEL", -1));

    gtk_init(&argc, &argv);
    ldm_theme = getenv("LDM_THEME");

    if (ldm_theme) {
        if (*ldm_theme == '/')
            ldm_theme_dir = g_strdup(ldm_theme);
        else
            ldm_theme_dir = g_strconcat(LDM_THEME_DIR, ldm_theme, NULL);
    } else {
        ldm_theme_dir = g_strconcat(LDM_THEME_DIR, "default", NULL);
    }

    allowguest = ldm_getenv_bool("LDM_GUESTLOGIN");
    gtk_rc_add_default_file(ldm_theme_file("/greeter-gtkrc"));

    /* Initialize information about hosts */
    ldminfo_init(&host_list, getenv("LDM_SERVER"));

    normcursor = gdk_cursor_new(GDK_LEFT_PTR);
    busycursor = gdk_cursor_new(GDK_WATCH);


    root = gdk_get_default_root_window();
    gdk_window_set_cursor(root, busycursor);

    greeter_pref_init();

    /* Set the background */
    load_root_background(ldm_theme_file("bg"), TRUE, FALSE);

    get_default_display_size(&width, &height);

    /* Setup the time and system labels */
    {
        const char *hoststring = 0;
        syslabel = gtk_label_new("");
        timelabel = gtk_label_new("");
#ifdef K12LINUX
        hoststring =
            g_strdup_printf("<b>%s</b> (%s)", get_sysname(),
                            getenv("LDMINFO_IPADDR"));
#else
        hoststring =
            g_strdup_printf("<b>%s</b> (%s) -", get_sysname(),
                            getenv("LDMINFO_IPADDR"));
#endif
        gtk_label_set_markup((GtkLabel *) syslabel, hoststring);
        update_time(timelabel);

        g_timeout_add(30000, (GSourceFunc) update_time, timelabel);
    }

    /**** Create the login window ****/
    loginWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    /* Create the options button */
    {


        optionbutton = gtk_button_new_with_mnemonic(_("_Preferences"));
        optionico = gtk_image_new_from_file(ldm_theme_file("preferences"));
        gtk_button_set_image((GtkButton *) optionbutton, optionico);

        gtk_button_set_relief((GtkButton *) optionbutton, GTK_RELIEF_NONE);
        gtk_button_set_focus_on_click((GtkButton *) optionbutton, FALSE);

        g_signal_connect(G_OBJECT(optionbutton), "clicked",
                         G_CALLBACK(popup_menu), loginWindow);
    }

    /**** Create the login window ****/
    {
        GtkWidget *guestbox, *guestspacer1, *guestspacer2, *vbox, *vbox2,
            *vbox2spacer, *hbox;
        GtkCellRenderer *renderer;

        g_signal_connect(G_OBJECT(loginWindow), "destroy",
                         G_CALLBACK(destroy), NULL);

        gtk_widget_set_app_paintable(loginWindow, TRUE);
        g_signal_connect(loginWindow, "configure-event",
                         G_CALLBACK(draw_background), NULL);
        gtk_widget_set_size_request(loginWindow, width, height);
        gtk_widget_realize(loginWindow);
        gtk_window_set_decorated(GTK_WINDOW(loginWindow), FALSE);

        logo = gtk_image_new_from_file(ldm_theme_file("logo"));
#ifdef K12LINUX
        if (access(ldm_theme_file("bottom_right"), R_OK) == 0) {
            bottom_right =
                gtk_image_new_from_file(ldm_theme_file("bottom_right"));
            has_bottom_right_image = TRUE;
        } else
            has_bottom_right_image = FALSE;
#endif

        pix = gtk_image_get_pixbuf((GtkImage *) logo);
        lw = gdk_pixbuf_get_width(pix);
        lh = gdk_pixbuf_get_height(pix);


        vbox = gtk_vbox_new(FALSE, 5);
        vbox2 = gtk_vbox_new(FALSE, ((height / 2) - lh));
        vbox2spacer = gtk_vbox_new(FALSE, 5);
        EntryBox = gtk_hbox_new(FALSE, 5);
        hbox = gtk_hbox_new(FALSE, 0);

        UserPrompt = gtk_label_new("");

        if (lw < 180)
            lw = 180;

        gtk_misc_set_alignment((GtkMisc *) UserPrompt, 1, 0.5);
        gtk_widget_set_size_request(UserPrompt, (lw / 2), 0);

        StatusMessages = gtk_label_new("");
        entry = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(entry), 20);
        g_signal_connect(G_OBJECT(entry), "activate",
                         G_CALLBACK(handle_entry), root);
        // Remap tab on entry to behave as enter
        g_signal_connect(G_OBJECT(entry), "key_press_event",
                         G_CALLBACK(key_press_event), loginWindow);

        choiceList = gtk_list_store_new(1, G_TYPE_STRING);
        choiceCombo =
            gtk_combo_box_new_with_model(GTK_TREE_MODEL(choiceList));
        renderer = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(choiceCombo),
                                   GTK_CELL_RENDERER(renderer), TRUE);
        gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(choiceCombo),
                                      GTK_CELL_RENDERER(renderer),
                                      "text", 0);
        g_signal_connect(G_OBJECT(choiceCombo), "changed",
                         G_CALLBACK(handle_choice), root);

        if (getenv("LDM_LOGIN_TIMEOUT") != NULL) {
            login_timeout = atoi(getenv("LDM_LOGIN_TIMEOUT"));
        } else {
            login_timeout = 0;
        }
        timeout_enabled = login_timeout > 0;
        timeout_left = 0;
        timeoutspacer1 = gtk_label_new("");
        timeoutspacer2 = gtk_label_new("");
        timeoutlabel = gtk_label_new("");
        timeoutbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), timeoutbox, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(timeoutbox), timeoutspacer1, TRUE,
                           FALSE, 0);
        gtk_box_pack_start(GTK_BOX(timeoutbox), timeoutlabel, FALSE, FALSE,
                           0);
        gtk_box_pack_start(GTK_BOX(timeoutbox), timeoutspacer2, TRUE,
                           FALSE, 0);
        g_timeout_add(1000, (GSourceFunc) update_timeout, timeoutlabel);

        guestspacer1 = gtk_label_new("");
        guestspacer2 = gtk_label_new("");
        GuestButton = gtk_button_new_with_label(_("Login as Guest"));
        g_signal_connect(G_OBJECT(GuestButton), "clicked",
                         G_CALLBACK(handle_guestbutton), root);
        gtk_button_set_focus_on_click((GtkButton *) GuestButton, FALSE);
        guestbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(guestbox), guestspacer1, TRUE, FALSE,
                           0);
        gtk_box_pack_start(GTK_BOX(guestbox), GuestButton, FALSE, FALSE,
                           0);
        gtk_box_pack_start(GTK_BOX(guestbox), guestspacer2, TRUE, FALSE,
                           0);

        entryspacer1 = gtk_label_new("");
        entryspacer2 = gtk_label_new("");
        gtk_box_pack_start(GTK_BOX(EntryBox), entryspacer1, TRUE, FALSE,
                           0);
        gtk_box_pack_start(GTK_BOX(EntryBox), UserPrompt, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(EntryBox), entry, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(EntryBox), choiceCombo, FALSE, FALSE,
                           0);
        gtk_box_pack_start(GTK_BOX(EntryBox), entryspacer2, TRUE, FALSE,
                           0);

        gtk_box_pack_start(GTK_BOX(vbox), logo, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), EntryBox, TRUE, FALSE, 0);
        if (allowguest)
            gtk_box_pack_start(GTK_BOX(vbox), guestbox, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), timeoutbox, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), StatusMessages, TRUE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), vbox2spacer, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(loginWindow), vbox2);
    }

    /**** Create the preference bar ****/
    prefBar = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    {
        GtkWidget *BottomBarBox;

        gtk_widget_set_app_paintable(GTK_WIDGET(prefBar), TRUE);
        g_signal_connect(prefBar, "configure-event",
                         G_CALLBACK(draw_background), NULL);
        gtk_window_set_decorated(GTK_WINDOW(prefBar), FALSE);
        gtk_widget_set_size_request(prefBar, width, BOTTOM_BAR_HEIGHT);

        BottomBarBox = gtk_hbox_new(FALSE, 0);
#ifndef K12LINUX
        gtk_box_pack_start(GTK_BOX(BottomBarBox),
                           GTK_WIDGET(optionbutton), FALSE, FALSE, 5);

        gtk_box_pack_end(GTK_BOX(BottomBarBox),
                         GTK_WIDGET(timelabel), FALSE, FALSE, 5);

        gtk_box_pack_end(GTK_BOX(BottomBarBox),
                         GTK_WIDGET(syslabel), FALSE, FALSE, 0);
#else
        optionbutton_box = gtk_vbox_new(FALSE, 0);
        optionbutton_spacer = gtk_label_new("");
        gtk_box_pack_start(GTK_BOX(optionbutton_box),
                           GTK_WIDGET(optionbutton_spacer), TRUE, FALSE,
                           0);
        gtk_box_pack_end(GTK_BOX(optionbutton_box),
                         GTK_WIDGET(optionbutton), FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(BottomBarBox),
                           GTK_WIDGET(optionbutton_box), FALSE, FALSE, 5);

        if (has_bottom_right_image == TRUE) {
            bottom_right_box = gtk_vbox_new(FALSE, 0);
            bottom_right_spacer = gtk_label_new("");
            gtk_box_pack_start(GTK_BOX(bottom_right_box),
                               GTK_WIDGET(bottom_right_spacer), TRUE,
                               FALSE, 0);
            gtk_box_pack_end(GTK_BOX(bottom_right_box),
                             GTK_WIDGET(bottom_right), FALSE, FALSE, 0);
            gtk_box_pack_end(GTK_BOX(BottomBarBox),
                             GTK_WIDGET(bottom_right_box), FALSE, FALSE,
                             0);
        }
#endif
        gtk_container_add(GTK_CONTAINER(prefBar), BottomBarBox);
    }

#ifdef K12LINUX
    /**** Create the TopBarBox ****/
    topBar = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    {
        GtkWidget *TopBarBox;

        gtk_window_set_decorated(GTK_WINDOW(topBar), FALSE);
        gtk_widget_set_size_request(topBar, width, TOP_BAR_HEIGHT);

        TopBarBox = gtk_hbox_new(FALSE, 5);
        gtk_box_pack_start(GTK_BOX(TopBarBox),
                           GTK_WIDGET(syslabel), FALSE, FALSE, 5);
        gtk_box_pack_end(GTK_BOX(TopBarBox),
                         GTK_WIDGET(timelabel), FALSE, FALSE, 5);
    }
#endif
    /* Show everything */

    gtk_widget_show_all(loginWindow);
    gtk_widget_show_all(prefBar);
    gtk_window_move(GTK_WINDOW(loginWindow), 0, 0);
    gtk_window_move(GTK_WINDOW(prefBar), 0, height - BOTTOM_BAR_HEIGHT);
#ifdef K12LINUX
    gtk_widget_show_all(topBar);
    gtk_window_move(GTK_WINDOW(topBar), 0, 0);
#endif

    // Center the mouse pointer on the default display
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen *screen = gdk_display_get_default_screen(display);
    GdkRectangle bbox;
    gdk_screen_get_monitor_geometry(screen, 0, &bbox);
    gdk_display_warp_pointer(display, screen, bbox.width/2, bbox.height/2);
    gdk_window_set_cursor(root, normcursor);

#ifdef K12LINUX
    gtk_widget_grab_focus(GTK_WIDGET(entry));
#endif

    /*
     * Start listening to stdin
     */

    g_stdin = g_io_channel_unix_new(STDIN_FILENO);      /* listen to stdin */
    g_stdout = g_io_channel_unix_new(STDOUT_FILENO);
    g_io_add_watch(g_stdin, G_IO_IN, (GIOFunc) handle_command, g_stdin);

    gtk_main();

    log_close();
    return 0;
}
