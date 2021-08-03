#!/usr/bin/python
# -*- coding: utf-8 -*-

#  Copyright 2009 Ryan Niebur <RyanRyan52@gmail.com>
#
#  Author: Ryan Niebur <RyanRyan52@gmail.com>
#
#  2009, Ryan Niebur <RyanRyan52@gmail.com>
#        Vagrant Cascadian <vagrant@freegeek.org>
#        Stéphane Graber <stgraber@ubuntu.com>
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License as
#  published by the Free Software Foundation; either version 2 of the
#  License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, you can find it on the World Wide
#  Web at http://www.gnu.org/copyleft/gpl.html, or write to the Free
#  Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
#  MA 02110-1301, USA.

from subprocess import *
from xml.etree import cElementTree as etree
import locale
import gettext
import re
import os
import sys


def build_language_map():
    map = {}
    for element in etree.parse('/usr/share/xml/iso-codes/iso_639.xml').findall('.//iso_639_entry'):
        if 'iso_639_1_code' in element.attrib:
            map[element.attrib['iso_639_1_code']] = element.attrib['name']
        elif 'iso_639_2B_code' in element.attrib:
            map[element.attrib['iso_639_2B_code']] = element.attrib['name']
        elif 'iso_639_2T_code' in element.attrib:
            map[element.attrib['iso_639_2T_code']] = element.attrib['name']

    return map

def build_territory_map():
    map = {}
    for element in etree.parse('/usr/share/xml/iso-codes/iso_3166.xml').findall('.//iso_3166_entry'):
        if 'alpha_2_code' in element.attrib: map[element.attrib['alpha_2_code']] = element.attrib['name']

    return map

def get_things():
    myarr = []
    if os.path.exists("/etc/debian_version"):
        cmd = ["cat",  "/usr/share/i18n/SUPPORTED"]
    else:
        cmd = ["locale",  "-a"]
    for thing in Popen(cmd, stdout=PIPE).stdout.readlines():
        try:
            thing=str(thing.decode("utf-8").strip())
            myarr.append(thing.split(".")[0].split(" ")[0])
        except UnicodeDecodeError:
            pass
    return myarr

def nice_name(my_locale):
    if my_locale == "":
        return ""
    if not re.search("_", my_locale) and len(my_locale) != 2:
        return ""
    my_locale = my_locale.split("@")[0]
    os.environ["LANGUAGE"] = my_locale
    locale.setlocale(locale.LC_ALL, '')
    split = my_locale.split("_")
    english_name = split[0]
    if len(split) > 1:
        english_territory = split[1]
    else:
        english_territory = ""
    if english_name in langmap:
        english_name = langmap[english_name]
    if english_territory in terrmap:
        english_territory = terrmap[english_territory]
    if len(english_name) > 0:
        try:
            translated_name = gettext.dgettext("iso_639", english_name)
        except:
            sys.stderr.write('error translating language, falling back to english: ' + my_locale + ' ' + english_name + '\n')
            translated_name = english_name
    else:
        translated_name = english_name
    if len(english_territory) > 0:
        try:
            translated_territory = gettext.dgettext("iso_3166", english_territory)
        except:
            sys.stderr.write('error translating territory, falling back to english: ' + my_locale + ' ' + english_territory + '\n')
            translated_territory = english_territory
    else:
        translated_territory = english_territory
    if len(translated_territory) > 0:
        return (translated_name + " (" + translated_territory + ")")
    else:
        return (translated_name)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        sys.stdout = open(sys.argv[1], "w")
    langmap = build_language_map()
    terrmap = build_territory_map()
    my_locales = get_things()
    for my_locale in sorted(list(set(my_locales))):
        try:
            this_nice_name = nice_name(my_locale)
            if len(this_nice_name) > 0 and this_nice_name != my_locale:
                print(my_locale + " " + this_nice_name)
        except UnicodeEncodeError:
            sys.stderr.write('unicode error encountered, skipping locale: ' + my_locale + '\n')
        except UnicodeDecodeError:
            sys.stderr.write('unicode error encountered, skipping locale: ' + my_locale + '\n')
