#!/usr/bin/env python
from __future__ import print_function

import atexit
import os
import tempfile
import sys
from subprocess import call
import click

Import("env", "projenv")

import json
import semantic_version

# local import
import ldscript_helper

# ------------------------------------------------------------------------------
# Utils
# ------------------------------------------------------------------------------

class Color(object):
    BLACK = '\x1b[1;30m'
    RED = '\x1b[1;31m'
    GREEN = '\x1b[1;32m'
    YELLOW = '\x1b[1;33m'
    BLUE = '\x1b[1;34m'
    MAGENTA = '\x1b[1;35m'
    CYAN = '\x1b[1;36m'
    WHITE = '\x1b[1;37m'
    LIGHT_GREY = '\x1b[0;30m'
    LIGHT_RED = '\x1b[0;31m'
    LIGHT_GREEN = '\x1b[0;32m'
    LIGHT_YELLOW = '\x1b[0;33m'
    LIGHT_BLUE = '\x1b[0;34m'
    LIGHT_MAGENTA = '\x1b[0;35m'
    LIGHT_CYAN = '\x1b[0;36m'
    LIGHT_WHITE = '\x1b[0;37m'

def clr(color, text):
    return color + str(text) + '\x1b[0m'

def print_warning(message, color=Color.LIGHT_YELLOW):
    print(clr(color, message), file=sys.stderr)

def print_filler(fill, color=Color.WHITE, err=False):
    width, _ = click.get_terminal_size()
    if len(fill) > 1:
        fill = fill[0]

    out = sys.stderr if err else sys.stdout
    print(clr(color, fill * width), file=out)

# ------------------------------------------------------------------------------
# ldscript handling
# ------------------------------------------------------------------------------

def ldscript_scons_include_name():
    include_name = "local.eagle.app.v6.common.ld"

    (elf, ) = env.arg2nodes(["$BUILD_DIR/$PROGNAME$PROGSUFFIX"])
    for child in elf.children():
        path = str(child)
        if path.endswith('.ld'):
            include_name = os.path.basename(path)
            break
    else:
        include_name = ""

    return include_name

def ldscript_deduce_include_name():
    with open(env['PLATFORM_MANIFEST'], 'r') as f:
        manifest = json.load(f)
    platform_version = semantic_version.Version(manifest['version'])

    # @1.5.0 includes ldscripts in the platform package
    if platform_version == semantic_version.Version("1.5.0"):
        include_name = "esp8266.flash.common.ld"
    elif platform_version <= semantic_version.Version("1.8.0"):
        include_name = "eagle.app.v6.common.ld"
    else:
        include_name = "local.eagle.app.v6.common.ld"

    return include_name

ldscript = ""
for flag in env["LINKFLAGS"]:
    if flag.startswith("-Wl,-T"):
        ldscript = flag[6:]

ldscript_dir = tempfile.mkdtemp('_espurna')
ldscript_path = os.path.join(ldscript_dir, ldscript)
env.Append(LIBPATH=ldscript_dir)

@atexit.register
def _delete_tmp():
    os.unlink(ldscript_path)
    os.rmdir(ldscript_dir)

include_name = ldscript_scons_include_name()
if not include_name:
    include_name = ldscript_deduce_include_name()

variant = ldscript.replace(".ld", "").replace("eagle.flash.", "")
ldscript_data = ldscript_helper.render_ldscript(variant, include=include_name)

with open(ldscript_path, "w") as f:
    f.write(ldscript_data)

# TODO remove this
sys.stderr.write('LDSCRIPT include_name: {}\n'.format(include_name))
sys.stderr.write('LDSCRIPT written: {}\n'.format(ldscript_path))
sys.stderr.write('LDSCRIPT variant: {}\n'.format(variant))

# ------------------------------------------------------------------------------
# Callbacks
# ------------------------------------------------------------------------------

def remove_float_support():

    flags = " ".join(env['LINKFLAGS'])
    flags = flags.replace("-u _printf_float", "")
    flags = flags.replace("-u _scanf_float", "")
    newflags = flags.split()

    env.Replace(
        LINKFLAGS = newflags
    )

def cpp_check(source, target, env):
    print("Started cppcheck...\n")
    call(["cppcheck", os.getcwd()+"/espurna", "--force", "--enable=all"])
    print("Finished cppcheck...\n")

def check_size(source, target, env):
    (binary,) = target
    path = binary.get_abspath()
    size = os.stat(path).st_size
    print(clr(Color.LIGHT_BLUE, "Binary size: {} bytes".format(size)))

    # Warn 1MB variants about exceeding OTA size limit
    flash_size = int(env.BoardConfig().get("upload.maximum_size", 0))
    if (flash_size == 1048576) and (size >= 512000):
        print_filler("*", color=Color.LIGHT_YELLOW, err=True)
        print_warning("File is too large for OTA! Here you can find instructions on how to flash it:")
        print_warning("https://github.com/xoseperez/espurna/wiki/TwoStepUpdates", color=Color.LIGHT_CYAN)
        print_filler("*", color=Color.LIGHT_YELLOW, err=True)

# ------------------------------------------------------------------------------
# Hooks
# ------------------------------------------------------------------------------

# Always show warnings for project code
projenv.ProcessUnFlags("-w")

remove_float_support()

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_size)
