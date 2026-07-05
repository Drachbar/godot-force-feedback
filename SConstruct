#!/usr/bin/env python
import os

env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src/", "thirdparty/SDL3/include"])

sources = Glob("src/*.cpp")

if env["platform"] == "windows":
    env.Append(LIBPATH=["thirdparty/SDL3/lib/x64"])
    env.Append(LIBS=["SDL3"])
else:
    # Linux/macOS: link against a system-provided SDL3.
    env.ParseConfig("pkg-config sdl3 --cflags --libs")

lib = env.SharedLibrary(
    "addons/force_feedback/bin/ffb{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)

if env["platform"] == "windows":
    sdl_dll = env.Command(
        "addons/force_feedback/bin/SDL3.dll",
        "thirdparty/SDL3/lib/x64/SDL3.dll",
        Copy("$TARGET", "$SOURCE"),
    )
    env.Depends(lib, sdl_dll)

Default(lib)
