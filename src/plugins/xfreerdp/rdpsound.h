#ifndef LDM_RDPSOUND_H
#define LDM_RDPSOUND_H

#include <glib.h>

/*
 * Whether an xfreerdp argument asks for sound or microphone redirection:
 * /sound, /audio, /microphone, /mic, bare or with ":options".
 */
gboolean rdp_sound_is_option(const gchar *arg);

/*
 * The PulseAudio-compatible sound server xfreerdp would talk to, as a
 * PULSE_SERVER value ("unix:/path" or whatever PULSE_SERVER already says), or
 * NULL if none answers. Free with g_free(). *looked receives a readable list
 * of the places tried, for the log; free it with g_free() too.
 */
gchar *rdp_sound_find_server(gchar **looked);

#endif
