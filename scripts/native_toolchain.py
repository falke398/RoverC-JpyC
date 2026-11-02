import os
import sys
from pathlib import Path

Import("env")

if sys.platform == "win32":
    compiler = Path(os.environ["LOCALAPPDATA"]) / "M5RoverTools/zig-0.13.0/ziglang/zig.exe"
    if not compiler.is_file():
        compiler = Path(sys.prefix) / "Lib/site-packages/ziglang/zig.exe"
    if not compiler.is_file():
        raise RuntimeError("Install ziglang==0.13.0 into the PlatformIO Python environment")
    executable = '"' + str(compiler) + '"'
    build_program = env.BuildProgram

    def build_with_zig(build_env):
        build_env.Replace(CC=executable + " cc", CXX=executable + " c++",
                          LINK=executable + " c++", AR=executable + " ar",
                          RANLIB=executable + " ranlib", ARFLAGS=["rc"])
        build_env["ENV"]["ZIG_GLOBAL_CACHE_DIR"] = os.path.join(
            build_env.subst("$PROJECT_DIR"), ".pio", "zig-cache")
        return build_program()

    env.AddMethod(build_with_zig, "BuildProgram")