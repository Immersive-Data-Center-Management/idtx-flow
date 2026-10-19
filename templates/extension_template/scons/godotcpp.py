"""
SCons tool: godotcpp
Builds the godot-cpp library for use as a dependency in the GDExtension build.

The godot-cpp version must match the IDTXFlow SDK build. IDTXFlow currently
uses the immutable godot-4.5-stable tag.

Usage in SConstruct:
    env.BuildGodotCPP()
"""
import os
import subprocess

GODOTCPP_VERSION = 'godot-4.5-stable'

def generate(env):
    env.AddMethod(_build_godot_cpp, 'BuildGodotCPP')

def exists(env):
    return True

def _build_godot_cpp(env):
    godot_cpp_path = "thirdparty/godot-cpp"
    if not os.path.exists(godot_cpp_path):
        print("Cloning godot-cpp...")
        subprocess.run([
            "git", "clone", "-b", GODOTCPP_VERSION, "--depth", "1", "--recursive",
            "https://github.com/godotengine/godot-cpp.git", 
            godot_cpp_path
        ], check=True)

    print("Building godot-cpp...")
    env["use_exceptions"] = "yes"
    env["use_rtti"] = "yes"
    env["use_threads"] = "yes"

    return env.SConscript(f"{godot_cpp_path}/SConstruct", exports=['env'])
