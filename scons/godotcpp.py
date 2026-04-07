"""
SCons tool: godotcpp
Builds the godot-cpp library for use as a dependency in the GDExtension build.

The godot-cpp version need to match the targeted Godot version. For Godot 4.5, we use the 4.5 branch of godot-cpp.

Usage in SConstruct:
    env.BuildGodotCPP()
"""
import os
import subprocess

def generate(env):
    env.AddMethod(_build_godot_cpp, 'BuildGodotCPP')

def exists(env):
    return True

def _build_godot_cpp(env):
    godot_cpp_path = "thirdparty/godot-cpp"
    if not os.path.exists(godot_cpp_path):
        print("Cloning godot-cpp...")
        subprocess.run([
            "git", "clone", "-b", "4.5", "--recursive", 
            "https://github.com/godotengine/godot-cpp.git", 
            godot_cpp_path
        ])
        
    print("Building godot-cpp...")
    env["use_exceptions"] = "yes"
    env["use_rtti"] = "yes"
    env["use_threads"] = "yes"

    # godot-cpp natively supports platform=android; forward the required settings
    if env.get('is_android', False):
        env["platform"] = "android"
        env["android_api_level"] = str(env.get('android_api_level', 24))
        # godot-cpp's get_android_ndk_root() first tries to construct a path
        # from ANDROID_HOME + a hardcoded ndk_version (e.g. 28.1.13356709).
        # When a different NDK version is in use we must bypass that logic so
        # godot-cpp falls through to os.environ["ANDROID_NDK_ROOT"] instead.
        ndk_root = env.get('android_ndk_root', os.environ.get('ANDROID_NDK_ROOT', ''))
        os.environ["ANDROID_NDK_ROOT"] = ndk_root
        env["ENV"]["ANDROID_NDK_ROOT"] = ndk_root
        # Clear ANDROID_HOME so godot-cpp does not construct a wrong NDK path
        env["ANDROID_HOME"] = ""
        os.environ.pop("ANDROID_HOME", None)
        os.environ.pop("ANDROID_SDK_ROOT", None)

    return env.SConscript(f"{godot_cpp_path}/SConstruct", exports=['env'])
