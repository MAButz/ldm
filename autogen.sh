#!/bin/sh -v

set -e

touch ChangeLog
aclocal
autoheader
touch stamp-h
autoconf
libtoolize --copy --force --automake
automake --add-missing --copy --force-missing
glib-gettextize --copy --force
intltoolize --automake --copy --force
