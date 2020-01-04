from __future__ import print_function

Import("env")

import os
import sys

from SCons.Script import ARGUMENTS


VERBOSE = "1" == ARGUMENTS.get("PIOVERBOSE", "0")
TRAVIS = os.environ.get("TRAVIS")
PIO_PLATFORM = env.PioPlatform()
CONFIG = env.GetProjectConfig()


class ExtraScriptError(Exception):
    pass


def log(msg, file=sys.stderr):
    if VERBOSE:
        print(msg, file=file)


# Most portable way, without depending on platformio internals
def subprocess_libdeps(lib_deps, storage=None, silent=True):
    import subprocess

    args = [env.subst("$PYTHONEXE"), "-mplatformio", "lib"]
    if not storage:
        args.append("-g")
    else:
        args.extend(["-d", storage])
    args.append("install")
    if silent:
        args.append("-s")

    args.extend(lib_deps)

    subprocess.check_call(args)


# Avoid spawning pio lib every time, hook into the LibraryManager API (sort-of internal)
def library_manager_libdeps(lib_deps, storage=None):
    from platformio.managers.lib import LibraryManager
    from platformio.project.helpers import get_project_global_lib_dir

    if not storage:
        manager = LibraryManager(get_project_global_lib_dir())
    else:
        manager = LibraryManager(storage)

    for lib in lib_deps:
        if manager.get_package_dir(*manager.parse_pkg_uri(lib)):
            continue
        log("installing: {}".format(lib))
        manager.install(lib)


def get_shared_libdeps_dir(section, name):

    if not CONFIG.has_option(section, name):
        raise ExtraScriptError("{}.{} is required to be set".format(section, name))

    opt = CONFIG.get(section, name)

    if not opt in env.GetProjectOption("lib_extra_dirs"):
        raise ExtraScriptError(
            "lib_extra_dirs must contain {}.{}".format(section, name)
        )

    return os.path.join(env["PROJECT_DIR"], opt)


# to speed-up build process, install libraries in either global or local shared storage
if TRAVIS:
    storage = None
    log("using global library storage")
else:
    storage = get_shared_libdeps_dir("common", "shared_libdeps_dir")
    log("using shared library storage: {}".format(storage))

subprocess_libdeps(env.GetProjectOption("lib_deps"), storage)
