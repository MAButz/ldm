#include <glib.h>
#include <libintl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldmplugin.h"
#include "ldmutils.h"
#include "ldmgreetercomm.h"
#include "logging.h"

static GPid greeterpid;
static GIOChannel *greeterr;
static GIOChannel *greeterw;

/*
 * set_greeter_pid
 */
void
set_greeter_pid(GPid p)
{
    greeterpid = p;
}

/*
 * set_greeter_read_channel
 */
void
set_greeter_read_channel(GIOChannel * gr)
{
    greeterr = gr;
}

/*
 * set_greeter_read_channel
 */
void
set_greeter_write_channel(GIOChannel * gw)
{
    greeterw = gw;
}

/*
 * ask_greeter
 *  Write command to I/O buffer of the greeter
 *      cmd -- command to write on buffer
 *
 *  Return 1 if fails, else 0
 */
int
ask_greeter(gchar * cmd)
{
    /* Write command to io channel of the greeter */
    GError *ge = NULL;
    if (g_io_channel_write_chars(greeterw, cmd, -1, NULL, &ge) !=
        G_IO_STATUS_NORMAL) {
        log_entry("ldm", 4, "%s", ge->message);
        return 1;
    }

    /* Flush buffer */
    if (g_io_channel_flush(greeterw, &ge) != G_IO_STATUS_NORMAL) {
        log_entry("ldm", 4, "%s", ge->message);
        return 1;
    }
    return 0;
}


/*
 * close_greeter
 *  Close greeter properly
 *      s_ldm -- struct ldm_info
 */
void
close_greeter()
{
    gboolean failed = 0;
    gchar *cmd = "quit\n";

    if (!greeterpid)
        return;

    if (ask_greeter(cmd))
        failed = 1;

    if (failed) {
        log_entry("ldm", 3, "quit command failed. SIGTERM to greeter");
        if (kill(greeterpid, SIGTERM) < 0) {
            log_entry("ldm", 3, "SIGTERM failed. SIGKILL to greeter");
            kill(greeterpid, SIGKILL);
        }
    }

    ldm_wait(greeterpid);
    g_io_channel_shutdown(greeterr, TRUE, NULL);
    g_io_channel_shutdown(greeterw, TRUE, NULL);
    close(g_io_channel_unix_get_fd(greeterr));
    close(g_io_channel_unix_get_fd(greeterw));
    greeterpid = 0;
}

/*
 * set_message
 *  Ask greeter to print a message to UI
 */
int
set_message(gchar * msg)
{
    gchar *cmd;

    if (!greeterpid)
        return 1;

    cmd = g_strconcat("msg ", msg, "\n", NULL);
    if (ask_greeter(cmd))
        return 1;
    g_free(cmd);

    return 0;
}

/*
 * listen_greeter
 *  Read data from I/O buffer of the greeter
 *      buf -- double ptr to buffer where data will be written
 *      buflen -- buffer length
 *      end -- pointer to the end of buffer
 *
 *  Return 1 if fails, else 0 and buffer with data in
 */
int
listen_greeter(gchar ** buffer, gsize * buflen, gsize * end)
{
    while (1) {
        /* Reads data from I/O channel of the greeter */
        GError *ge = NULL;
        if (g_io_channel_read_line(greeterr, buffer, buflen, end, &ge) !=
            G_IO_STATUS_NORMAL) {
            log_entry("ldm", 3, "%s", ge->message);
            return 1;
        }

        g_strstrip(*buffer);
        log_entry("ldm", 7, "Got command: %s", *buffer);

        /* handle callbacks */
        if (**buffer == '@') {
            if (!g_ascii_strncasecmp(*buffer, "@backend@", 9)) {
                ldm_raise_auth_except(AUTH_EXC_RELOAD_BACKEND);
                continue;
            } else if (!g_ascii_strncasecmp(*buffer, "@guest@", 7)) {
                ldm_raise_auth_except(AUTH_EXC_GUEST);
                continue;
            }
        }
        break;
    }
    return 0;
}

/*
 * ask_value_greeter
 *  Send cmd and expect value
 *      s_ldm -- ldm_info struct
 *      cmd -- command to send
 *      buffer -- buffer to write to answer
 */
gchar *
ask_value_greeter(gchar * cmd)
{
    gsize len, end;
    gchar *buffer;
    if (ask_greeter(cmd))
        die("ldm", "%s from greeter failed", cmd);

    if (listen_greeter(&buffer, &len, &end))
        die("ldm", "%s from greeter failed", cmd);

    return buffer;
}
