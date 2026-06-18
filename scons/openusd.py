"""
SCons tool: openusd
Builds the OpenUSD library from source using the provided build scripts.
The built OpenUSD library is a dependency for the IDTXFlow GDExtension. The OpenUSD version
can be configured via the 'openusd_version' variable in the SCons environment. Usually the OpenUSD library is
build without Python support, as the IDTXFlow GDExtension does not require it. However, you can enable Python support
by passing 'with_python_support=True' to the BuildOpenUSD method.

Usage in SConstruct:
    env.BuildOpenUSD(with_python_support=False)  # Set to True to include Python bindings
"""
import os
import subprocess

from SCons.Script import Exit


def generate(env):
    env.AddMethod(_build_open_usd, 'BuildOpenUSD')

def exists(env):
    return True

def _build_open_usd(env, with_python_support=False):
    open_usd_version = env.get('openusd_version', '')
    open_usd_path = f"thirdparty/openusd-{open_usd_version}-src"
    print("USD ROOT" + os.environ.get("USD_ROOT", "thirdparty/openusd"))
    
    # check if we have cloned openUSD already
    if not os.path.exists(open_usd_path):
        print("Cloning openUSD...")
        result = subprocess.run([
            "git", "clone", "-b", "v" + open_usd_version, "--recursive", "--depth", "2",
            "https://github.com/PixarAnimationStudios/OpenUSD.git",
            open_usd_path
        ])
        if result.returncode != 0:
            print(f"Failed to clone openUSD repo.")
            Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode})")              

    platform_name = env["platform_name"]
    build_target = env["target"]

    # check if we have build the openUSD lib already
    open_usd_build_path = f"thirdparty/openusd-{open_usd_version}" if not with_python_support else f"thirdparty/openusd-{open_usd_version}-withPython"
    if platform_name == "windows":
        open_usd_lib = f"{open_usd_build_path}/lib/usd_ms.dll"
    elif platform_name == "macos":
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_ms.dylib"
    else:
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_ms.so"
    
    if not os.path.exists(open_usd_lib):
        print("Building openUSD...")
        openusd_env = {}
        # when building openUSD we need to ensure that proper env-vars are set
        # on Windows
        if platform_name == "windows":
            _get_windows_msvc_env(openusd_env)
        else:
            # ensure the current system path is passed to the openUSD python build process
            openusd_env["PATH"] = os.environ.get("PATH", "")

        # Try python3 first, fallback to python if not available
        python_cmd = "python3"
        try:
            subprocess.run([python_cmd, "--version"], check=True, capture_output=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            python_cmd = "python"

        print(f"Building openUSD without python support = {with_python_support}...")
        result = subprocess.run([
            python_cmd,
            f"{open_usd_path}/build_scripts/build_usd.py",
            f"{open_usd_build_path}",
            "--verbose",
            "--build-variant", "release" if build_target == "template_release" else "relwithdebuginfo", #debug,release,relwithdebuginfo
            "--build-monolithic",
            "--no-python" if not with_python_support else "--python",
            "--no-examples",
            "--no-tutorials",
            "--no-tools" if not with_python_support else "--tools",
            "--no-debug-python",
            "--no-openvdb",
            "--no-usdview",
            "--no-imaging",
            "--no-vulkan",
            "--no-materialx",
            "--onetbb",
            "--cmake-build-args", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_STANDARD=17",
        ], env=openusd_env)
        
        if result.returncode != 0:
            print(f"Failed to build openUSD")
            Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode})")        

def _get_windows_msvc_env(env):
    vswhere_path = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if not os.path.exists(vswhere_path):
        raise RuntimeError("vswhere.exe not found")

    # Step 1: Find the installation path of Visual Studio
    cmd = [
        vswhere_path,
        "-latest",
        "-products", "*",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property", "installationPath"
    ]
    vs_path = subprocess.check_output(cmd, encoding="utf-8").strip()
    if not vs_path:
        raise RuntimeError("No Visual Studio installation with required components found")
    
    """Runs vcvars64.bat and returns its environment as a dict"""
    vcvars_path = os.path.join(vs_path, "VC", "Auxiliary", "Build", "vcvars64.bat")
    
    # Use a cmd trick to output all environment variables after calling vcvars
    cmd = f'"{vcvars_path}" >nul && set'
    
    # Run and capture output
    output = subprocess.check_output(cmd, shell=True, text=True)
    
    # Parse into a dictionary
    for line in output.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            env[key.upper()] = value
            
    return env
