# Every system dependency this build has, in one place.
#
# Three things used to state this list independently and disagreed:
# `raylib-check` probed five pkg-config names and named four packages, README
# gave a different install command, and CI installed a superset that included
# two packages this project does not use (`libasound2-dev` -- raylib's audio
# backend is miniaudio, vendored -- and `libglu1-mesa-dev` -- libraylib.a has
# no GLU reference). A new contributor following README hit missing X11
# extension headers that only CI happened to install.
#
# The lists below are derived from what the vendored raylib actually compiles:
# GLFW's `x11_platform.h` includes Xcursor, Xrandr, Xinerama, XInput2 and
# Xext/shape unconditionally when the X11 backend is enabled, and this build
# enables it alongside Wayland.
#
# `make prerequisites` prints install commands from these variables, and
# `tests/test_build_prerequisites.sh` fails if README or CI drift from them.

# pkg-config modules that must resolve before raylib can compile. Kept as the
# .pc names rather than distro packages so the probe is distro-neutral.
RAYLIB_PKG_CONFIG_DEPS := \
	wayland-client wayland-cursor wayland-egl xkbcommon x11 \
	xrandr xinerama xi xcursor xext

# Non-pkg-config programs the raylib build invokes. wayland-scanner turns the
# protocol XML into C and is packaged separately from the libraries on some
# distributions.
RAYLIB_REQUIRED_PROGRAMS := wayland-scanner

# Install commands, one line each, printed by `make prerequisites`.
RAYLIB_ARCH_PACKAGES := \
	wayland wayland-protocols libxkbcommon libx11 libxrandr libxinerama \
	libxi libxcursor libxext mesa
RAYLIB_DEBIAN_PACKAGES := \
	build-essential curl pkg-config \
	libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev \
	libxinerama-dev libxi-dev libxcursor-dev libxext-dev libgl1-mesa-dev

# Development-time tools, not needed to build the game. Each one is optional in
# the sense that only some targets use it, but a target that needs it fails
# with a bare "command not found" today, so they are listed here to be printed
# by `make prerequisites` and checked by the target that needs them.
#
# node  -- check-docs, check-docs-truth, docs-probe, docs-record
# uv    -- test-animal-scenarios-*, and two e2e scripts that post-process
#          reports (uv run --offline also needs a writable cache directory)
# gcovr -- test-coverage
# inotify-tools -- docs-watch
DEV_TOOL_DEPS := node uv gcovr inotifywait
