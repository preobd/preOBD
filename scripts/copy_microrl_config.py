Import("env")
import shutil
import os

def copy_microrl_config(source, target, env):
    # Source: our custom config (absolute path)
    project_dir = env.get("PROJECT_DIR")
    src_config = os.path.join(project_dir, "lib", "microrl", "config.h")

    # Get the environment-specific library dependencies directory
    # Must use PIOENV to get the actual environment name (teensy41, mega2560, etc)
    pioenv = env.get("PIOENV")
    project_libdeps = os.path.join(project_dir, ".pio", "libdeps", pioenv)
    dst_config = os.path.join(project_libdeps, "microrl", "src", "config.h")

    # Copy if both exist
    if os.path.exists(src_config):
        if os.path.exists(os.path.dirname(dst_config)):
            print(f"[microrl] Copying custom config to {pioenv}: {src_config} -> {dst_config}")
            shutil.copy(src_config, dst_config)
        else:
            print(f"[microrl] Warning: destination not found for {pioenv}: {os.path.dirname(dst_config)}")
    else:
        print(f"[microrl] Warning: source config not found: {src_config}")

env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", copy_microrl_config)
