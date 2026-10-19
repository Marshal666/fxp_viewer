# fxp_viewer

A small C++17 3D viewer for physics experiments. Simulation projects can link
its library and publish object poses through a simple C++ API. A small
PhysicsInteractor connects the standalone fxp_physics submodule to the viewer;
the physics solver remains entirely in that module.

- Configurable solid-color or procedural-skybox background (black by default),
  with fly, pan, orbit and dolly camera controls.
- Resizable window, borderless fullscreen, File/Open/Exit and View/Reset Camera.
- YAML scenes with hierarchies, local transforms, meshes, lights and physics.
- Bottom-centre Play, Pause, Step and Reset controls for fixed-tick simulation.
- Static OBJ, glTF and GLB models; limited glTF/GLB skeletal animation.
- Diffuse lighting, eight directional/point lights, ambient light, color textures
  and object tint. GPU skinning, shared mesh assets and mipmaps.
- Open scenes through the file picker, command line or YAML drag-and-drop.

Coordinates are **right handed, Y up, metres**. Rendering and visual animation
use floating point. Simulation owns its fixed timestep and deterministic state.

Initialize the pinned physics/math submodules once after cloning:

~~~sh
git submodule update --init --recursive
~~~

Try [examples/physics_playground.yaml](examples/physics_playground.yaml): a heavy
ball hits a cube wall beside an overbalanced tower. Open it with File > Open,
drag it into the window, or pass its path on the command line, then press Play.
Scenes start paused. Step pauses and advances one whole tick; Reset restores the
initial physics snapshot and body transforms, leaving the camera where it is.
Scenes without physics keep the controls disabled.

## Windows

Install Visual Studio 2026 with Desktop development with C++ and CMake tools.
This helper initializes the developer environment and uses **Visual Studio's
bundled CMake and MSVC**:

~~~powershell
powershell -ExecutionPolicy Bypass -File tools/build-windows.ps1 -Configuration Release
.\build\windows-release\fxp_viewer.exe examples\showcase.yaml
~~~

Or, from the VS x64 Developer PowerShell:

~~~powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
.\build\windows-debug\fxp_viewer.exe examples\showcase.yaml
~~~

The Windows viewer opens no extra console. Tests and the API example retain
console diagnostics.

## Linux

Use GCC or Clang, CMake 3.21+ (for presets), Ninja, and OpenGL 3.3 or newer.
The default backend uses X11, including XWayland on Wayland desktops.

~~~sh
sudo apt-get install build-essential cmake ninja-build pkg-config \
  libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev \
  libgl1-mesa-dev
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/fxp_viewer examples/showcase.yaml
~~~

Ubuntu 20.04's stock CMake is too old: use a newer standalone CMake or Ubuntu.
WSL needs WSLg or an X server to display a window. For headless graphics tests:

~~~sh
sudo apt-get install xvfb
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  xvfb-run -a ./build/linux-debug/fxp_viewer_tests --render "$PWD"
~~~

CMake downloads pinned sources with SHA-256 verification: raylib 6.2, raygui 5.0,
yaml-cpp 0.8.0. Offline builds can set FETCHCONTENT_SOURCE_DIR_RAYLIB,
FETCHCONTENT_SOURCE_DIR_RAYGUI and FETCHCONTENT_SOURCE_DIR_YAML_CPP to unpacked
source directories.

See [background settings](docs/scenes.md#background) and the runnable
[skybox example](examples/skybox.yaml).

## Controls

| Input | Action |
|---|---|
| Right mouse + WASD | Look and fly |
| Q / E while flying | Down / up |
| Shift while flying | Four times faster |
| Wheel while flying | Change flight speed |
| Middle mouse drag | Pan |
| Alt + left mouse drag | Orbit the camera target |
| Wheel | Dolly |
| Ctrl+O | Open scene picker |
| Home | Reset to the scene's initial camera |
| F11 | Toggle borderless fullscreen |
| Escape | Close menu/dialog; leave fullscreen |

File > Open provides a portable in-app picker. Select a file and click Open, or
enter an absolute path. Directory entries navigate into folders. Dropping several
files opens the first YAML scene. Failed loads preserve the existing scene and
report an error.

Run without arguments for an empty viewport. The showcase includes original OBJ,
static glTF, animated GLB and texture assets. Python is not required to build or
run; tools/generate-demo-assets.py regenerates the included assets.

## Embed in a simulation

~~~cmake
add_subdirectory(path/to/fxp_viewer)
target_link_libraries(my_simulation PRIVATE fxp_viewer::viewer)
~~~

The app, tests and example default to off when embedded. Override
FXP_VIEWER_BUILD_APP, FXP_VIEWER_BUILD_TESTS or FXP_VIEWER_BUILD_EXAMPLES as needed.

~~~cpp
#include <fxp_viewer/viewer.hpp>

fxp_viewer::Viewer viewer;
auto& scene = viewer.scene();
auto group = scene.createObject("group");
auto box = scene.addMesh("body", viewer.renderer().cube(), group);
scene.setPosition(box, {0, 1, 0});
scene.setRotationDegrees(box, {0, 30, 0});
scene.setScale(box, {2, 1, 1});
scene.object(box).tint = {230, 145, 80, 255};

fxp_viewer::Light sun;
sun.direction = {-1, -2, -1};
scene.addLight("sun", sun);

viewer.run([&](fxp_viewer::Viewer&, double frameSeconds) {
    // Advance simulation with its own fixed-step accumulator.
    // Convert its output poses to floats and call setPosition/setRotation.
    // Never feed interpolated render poses back into the simulation.
});
~~~

See the [runnable example](examples/api_example.cpp), [API notes](docs/api.md)
and [scene format](docs/scenes.md). All viewer, mesh and scene operations belong
on the graphics-context thread. Worker simulations should hand off snapshots.
Only one Viewer/window can be alive at a time.

## Verification

Normal CTest checks require no window. They cover scene transforms and IDs plus
physics playback, fixed-tick scheduling, collisions, exact reset/replay, frame
cadence independence, bounded catch-up and recovery after a simulation error.

The graphics suite needs a desktop/OpenGL context (or the Xvfb command above):

~~~powershell
.\build\windows-release\fxp_viewer_tests.exe --render .
~~~

It checks model formats, custom meshes, changes in rendered skeletal poses,
independent animation clocks, reload, failure preservation and fullscreen/window
restoration. It also clicks the playback buttons, validates physics YAML, checks
wall impact/tower collapse, and prints the demo's tick-240 physics checksum for
cross-platform comparison.

For a reproducible screenshot:

~~~sh
fxp_viewer examples/showcase.yaml --frames 60 --screenshot showcase.png --hidden
~~~

The frame-limited mode advances at 1/60 second per frame. Add --play to start a
physics scene automatically, including for a reproducible screenshot:

~~~sh
fxp_viewer examples/physics_playground.yaml --play --frames 241 --screenshot physics.png --hidden
~~~

Interactive playback uses elapsed frame time to schedule fixed physics ticks.
Render interpolation never feeds back into the simulation.

## Initial limits

Opaque/cutout diffuse rendering only: no shadows, normal maps, PBR, alpha
blending, post-processing, scene editing, animation blending or root motion.
Core triangle-based glTF 2.0 is supported; required extensions, Draco compression,
morph targets and multiple skeletons per file are rejected. Skeletal assets
support 128 bones and four weights per vertex; export unit/uniform bone scale.
The GPU needs vertex-uniform capacity for the 128-bone shader.

YAML places complete model assets; it does not expose imported glTF nodes as
editable objects or play their non-skeletal node animations. Mesh handles share
geometry, but drawing is one call per mesh/material, without automatic instancing
or frustum culling. Camera and GPU precision are ordinary floating point.

[Third-party notices](THIRD_PARTY_NOTICES.md).

