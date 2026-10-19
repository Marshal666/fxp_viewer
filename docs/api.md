# C++ API

Include <fxp_viewer/viewer.hpp> and link fxp_viewer::viewer. Raylib's Vector3,
Quaternion, Color and Matrix types form the public rendering boundary.

## Object lifetime and transforms

Scene::createObject(name, parent) returns an ObjectId. Names are nonempty and
unique. IDs are never reused within a scene; scene replacement invalidates IDs.

- addMesh(name, handle, parent): create geometry.
- addLight(name, light, parent): create a light.
- object(id): reference to change tint, visibility, mesh, animation or transform.
  Adding other objects does not invalidate references.
- find(name): ID or root (zero) when absent.
- contains(id): check existence.
- removeObject(id): remove the whole subtree; invalidate its references.
- setParent(id, parent): preserve LOCAL transform, reject cycles.
- setPosition, setRotation, setRotationDegrees, setScale, translate, rotate:
  validated transform operations.
- worldMatrix(id): compose ancestors and local transform.

Positions and translation deltas use parent coordinates. rotate postmultiplies
a local quaternion delta. Quaternions use x,y,z,w and setters normalize them.
Euler degrees apply X, then Y, then Z (qZ * qY * qX). Scale precedes rotation,
then translation. Nonzero nonuniform/negative object scales are supported.
Direct field writes must maintain finite values, nonzero scale and unit rotations.

An invisible parent hides all descendant geometry and lights. Hidden objects'
animation clocks still advance.

## Resources and animation

Renderer::cube(), sphere() and plane() create a unit cube, diameter-one sphere
and unit XZ plane. Each call creates an asset; reuse its handle to share geometry.
loadModel(path) independently loads OBJ, glTF or GLB.

~~~cpp
auto model = viewer.renderer().loadModel("robot.glb");
auto first = scene.addMesh("robot_a", model);
auto second = scene.addMesh("robot_b", model);
scene.setPosition(second, {3, 0, 0});
if (!model->clips().empty()) {
    scene.object(first).animation.clip = model->clips().front().name;
    scene.object(second).animation.clip = model->clips().front().name;
    scene.object(second).animation.time = 0.5;
}
~~~

Each AnimationClip supplies a name and duration in seconds. Objects have clip,
time, speed, playing and loop fields. Empty clip means bind pose. Pause with
playing=false; scrub with time; restart by setting time=0. Negative speed works,
looped time wraps and non-looped time clamps. YAML rejects nonexistent names;
the low-level API renders bind pose when no clip matches.

Raylib resamples glTF clips at 60 Hz; the viewer interpolates poses and skins
vertices on the GPU. Clip names are limited by raylib to 31 bytes. Instances share
mesh/clip data while maintaining independent animation clocks.

Mesh handles release resources when their last reference disappears. Renderer
shutdown releases any remaining GPU resources before closing the context.
External handles surviving shutdown cannot be used with a new Viewer.

setColorTexture(handle, path) changes every base-color slot on that shared asset;
other users of the handle see the change. Object tint is independent. Missing
or zero normals are generated; textures receive mipmaps and trilinear filtering.

## Custom meshes

~~~cpp
fxp_viewer::MeshData data;
data.vertices = {
    {{-1, 0, 0}, {0, 0, 1}, {0, 0}},
    {{ 1, 0, 0}, {0, 0, 1}, {1, 0}},
    {{ 0, 2, 0}, {0, 0, 1}, {0.5f, 1}}
};
data.indices = {0, 1, 2};
auto triangle = scene.addMesh("triangle", viewer.renderer().createMesh(data));
~~~

Indices are optional; otherwise vertices form a CCW triangle list. 32-bit input
indices are expanded on upload, avoiding raylib's 16-bit index limit. If all
normals are zero, area-weighted normals are generated. Supply explicit normals
for chosen hard/smooth edges. Replace a handle to replace immutable geometry.

## Lighting and loop ownership

Lights follow their object's hierarchy. Directional direction points in the
direction light travels. Point range is a radius in metres, with a smooth cutoff
times 1/(1+distance²). Intensity is nonnegative. Texture, tint and light colors
are treated as sRGB; lighting accumulates in linear RGB and is encoded for display.
All geometry is two-sided. Alpha below 0.5 is discarded; other fragments are opaque.

YAML rejects more than eight lights. If code creates more, the renderer uses the
first eight enabled lights in its hierarchy traversal. Keep active lights within
Renderer::maxLights or disable unused ones.

Viewer::frame(dt) processes input, advances the built-in interactor when playing,
advances visual animation and draws. Use it with shouldClose()/requestClose(), or
run(callback) to execute a callback before each frame. For an external simulation,
use a scene without physics and own its clock inside that callback.

loadScene(path) returns false and sets lastError() on failure, retaining the old
scene. setCamera(settings) also establishes the reset-camera destination.
toggleFullscreen() preserves/restores window position and size.

For an already open raylib window, own a Renderer and Scene directly. Call
updateAnimations(scene,dt) and draw(scene,camera) inside BeginDrawing/EndDrawing.
Renderer::draw clears the target, renders the scene background, and manages its
own 3D mode. Destroy the renderer before closing the context. All operations run
on the context's thread.

## Background

Change scene.background directly, including at runtime:

~~~cpp
scene.background.type = fxp_viewer::BackgroundType::Color;
scene.background.color = {24, 28, 36, 255};

// A procedural, camera-relative sky with a world Y-up horizon:
scene.background.type = fxp_viewer::BackgroundType::Skybox;
scene.background.skyColor = {55, 105, 180, 255};
scene.background.horizonColor = {190, 215, 235, 255};
scene.background.groundColor = {42, 48, 58, 255};
~~~

All background colors are opaque sRGB; alpha is ignored. The default background
is black. Background settings are visual only and do not affect ambient or other
lights. Skybox rendering uses one fullscreen draw and no texture assets.

## Physics interactor

After loading a physics scene, viewer.simulation() returns its PhysicsInteractor;
it returns nullptr for an ordinary scene. The pointer is invalidated by a
successful scene load, and preserved after a failed load.

~~~cpp
if (auto* sim = viewer.simulation()) {
    sim->play();                  // Viewer::frame schedules fixed ticks.
    sim->pause(viewer.scene());
    sim->step(viewer.scene());     // Exactly one tick, then paused.
    sim->reset(viewer.scene());    // Exact initial world, paused.
    auto tick = sim->tick();
    auto body = sim->bodyId(viewer.scene().find("projectile"));
    auto state = sim->world().body(body); // Read-only physics inspection.
}
~~~

For programmatic setup, construct PhysicsScene with the library's Config and
PhysicsBody entries, then own a PhysicsInteractor alongside your Scene:

~~~cpp
using namespace fxp_physics;
fxp_viewer::PhysicsScene setup;
setup.config.timestep = 1_r / 60_r;
fxp_viewer::PhysicsBody entry;
entry.object = scene.addMesh("ball", renderer.sphere());
scene.setPosition(entry.object, {0, 4, 0});
entry.body.pose.position = {0, 4, 0};
entry.shape = Shape::sphere(0.5_r);
setup.bodies.push_back(entry);
fxp_viewer::PhysicsInteractor simulation(scene, setup);
simulation.play();
// In your own frame loop:
simulation.update(scene, frameSeconds);
~~~

The adapter owns the World, stable object/body bindings, fixed-step accumulator
and initial snapshot. world() is read-only; bodyId() returns zero for unbound
objects. tick(), time(), playing() and faulted() expose playback state. No OpenGL
context is required by the adapter itself.

Body poses come from PhysicsBody::body, not from floating-point scene transforms.
Set both initial visual and physics transforms for programmatic setup. YAML does
this automatically. Keep bound objects alive at the scene root for the adapter's
lifetime; reconstruct/reload to change body membership or colliders. Decorative
objects and children may still be added and removed with the normal Scene API.
Manipulating a bound render pose does not teleport its physics body.

Do not call update() yourself on viewer.simulation(): Viewer::frame already does.
Standalone adapters use update(scene, dt). The physics module remains independent
of the renderer; only the adapter converts its output poses into rendering floats.
CMake reuses an existing fxp::physics target when embedding, otherwise builds the
pinned submodule. See the scene-format document for scheduling and reset semantics.
