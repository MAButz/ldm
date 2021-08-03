/*
 * Copyright (2010) Stéphane Graber <stgraber@ubuntu.com>, Revolution Linux.

 * Author: Stéphane Graber <stgraber@ubuntu.com>

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.

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

#include <glib.h>
#include <stdio.h>
#include <time.h>
#include <syslog.h>
#include <stdlib.h>

#include "logging.h"

FILE *logfile = NULL;
char *LOG_LEVEL_NAMES[] = { "EMERGENCY", "ALERT", "CRITICAL", "ERROR",
    "WARNING", "NOTICE", "INFO", "DEBUG"
};

int loglevel = 0;

/*
 * log_init
 *  Init log facility
 *  Open log.  Will log locally or back to the server based upon
 *  the boolean LDM_SYSLOG.
 */
void
log_init(int syslog, int level)
{
    if (level < 0 || level > 7) {
        loglevel = LOGLEVEL;
    } else {
        loglevel = level;
    }

    if (syslog) {
        openlog("ldm", LOG_PID | LOG_NOWAIT, LOG_DAEMON);
    } else {
        logfile = fopen(LOGFILE, "a");
        if (!logfile) {
            fprintf(stderr, "Couldn't open logfile " LOGFILE "\n");
            exit(1);
        }
        setbuf(logfile, NULL);                   /* unbuffered writes to the log file */
    }
}

/*
 * log_close
 *  Close logging and clean exit
 */
void
log_close()
{
    if (logfile)
        fclose(logfile);
    else
        closelog();
}


/*
 * log_entry: log messages to file or syslog
 */
void
log_entry(char *component, int level, const char *format, ...)
{
    if (level < 0 || level > 7 || level > loglevel)
        return;

    va_list ap;
    va_start(ap, format);

    if (logfile) {
        // Get current time
        struct tm *ptr;
        time_t lt;
        char timestr[20];
        lt = time(NULL);
        ptr = localtime(&lt);
        strftime(timestr, sizeof(timestr), "%b %e %H:%M:%S", ptr);

        fprintf(logfile, "%s: [%s] %s: ", timestr, component,
                LOG_LEVEL_NAMES[level]);
        vfprintf(logfile, format, ap);
        fprintf(logfile, "\n");
        fflush(logfile);
    } else {
        vsyslog(level, format, ap);
    }

    va_end(ap);
}

/*
 * die()
 *  Close display manager down with an error message.
 *  Shut things down gracefully if we can
 *      msg -- log message
 */
void
die(char *component, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);

    log_entry(component, 2, format, ap);

    va_end(ap);

    /* Stop logging */
    if (logfile)
        fclose(logfile);
    else
        closelog();

    exit(1);
}
