/*
 * ldmlockout.h - counting rejected sign-ins across a client's screens.
 *
 * Copyright (c) 2026 Marc-Andre Beckmann-Butz <ma@butz.online>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _LDMLOCKOUT_H_
#define _LDMLOCKOUT_H_

#include <glib.h>
#include <time.h>

/*
 * Remember that target rejected these credentials just now. user is only
 * recorded so the log can say who was trying; the count itself is per
 * target, because that is what the thing doing the banning counts.
 */
void ldm_lockout_record(const gchar *target, const gchar *user);

/*
 * How many rejections target has on record within the last window seconds.
 * If newest is not NULL it receives the time of the most recent one, or 0
 * when there is none - a caller that wants to say "try again in N seconds"
 * needs to know when the clock started, not just how many there were.
 */
gint ldm_lockout_count(const gchar *target, gint window, time_t *newest);

/* Forget target's rejections. Call after credentials that were accepted. */
void ldm_lockout_clear(const gchar *target);

#endif /* _LDMLOCKOUT_H_ */
