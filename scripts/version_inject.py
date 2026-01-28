"""
version_inject.py - PlatformIO pre-build script for version injection

Injects FW_BUILD_NUMBER (git commit count) and FW_GIT_HASH (short commit hash)
as compiler flags before each build.
"""

# pylint: disable=undefined-variable
# pyright: reportUndefinedVariable=false
# Import and env are injected by PlatformIO/SCons at runtime

Import("env")  # type: ignore
import subprocess


def get_git_info():
    """Get build number and git hash from git repository."""
    build_number = 0
    git_hash = "unknown"

    try:
        # Get commit count as build number
        result = subprocess.run(
            ["git", "rev-list", "--count", "HEAD"],
            capture_output=True,
            text=True,
            check=True
        )
        build_number = int(result.stdout.strip())
    except (subprocess.CalledProcessError, ValueError, FileNotFoundError):
        print("Warning: Could not get git commit count, using default build number 0")

    try:
        # Get short commit hash
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            check=True
        )
        git_hash = result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("Warning: Could not get git hash, using 'unknown'")

    return build_number, git_hash


# Get version info
build_number, git_hash = get_git_info()

# Inject as compiler flags
env.Append(CPPDEFINES=[
    ("FW_BUILD_NUMBER", build_number),
    ("FW_GIT_HASH", f'\\"{git_hash}\\"')
])

print(f"Version injection: build={build_number}, hash={git_hash}")
