# YAML scene format, version 1

UTF-8 YAML; asset paths are relative to the scene file. Unknown/duplicate fields
are errors. Errors identify object IDs and YAML line numbers where possible.

~~~yaml
version: 1
camera:
  position: [7, 5, 8]
  target: [0, 1, 0]
  fov: 55
  speed: 5
ambient:
  color: [255, 255, 255]
  intensity: 0.12
meshes:
  box: { primitive: cube }
  robot: { file: assets/robot.glb }
  textured: { file: assets/mesh.obj, texture: assets/color.png }
objects:
  - id: sun
    light:
      type: directional
      direction: [-1, -2, -1]
      color: [255, 244, 230]
      intensity: 1.2
  - id: lamp
    transform:
      position: [0, 3, 0]
    light:
      type: point
      color: [100, 180, 255]
      intensity: 12
      range: 8
  - id: group
    transform:
      position: [2, 0, 0]
      rotation: [0, 30, 0]
      scale: [1, 1, 1]
    children:
      - id: body
        mesh: box
        color: [220, 140, 80]
        visible: true
        transform:
          position: [0, 1, 0]
  - id: character
    mesh: robot
    animation:
      clip: walk
      time: 0
      speed: 1
      playing: true
      loop: true
~~~

The robot/OBJ paths here are illustrative. examples/showcase.yaml runs using the
included assets.

## Defaults and validation

Only version: 1 is mandatory. Root fields are version, camera, ambient, background,
meshes, objects and physics. Missing objects means an empty scene.

Camera defaults are shown above. FOV is vertical, 5..150 degrees; speed is positive
metres/second. Position/target must define a nonzero, nonvertical direction.
Background defaults to black when omitted; see the options below.

Each mesh needs exactly one of file or primitive. Primitives: cube, sphere, plane.
Optional texture replaces all base-color texture slots. Static glTF/GLB needs
no animation data. Imported colors/textures are otherwise preserved.

Every object needs a globally unique string ID. Geometry, light and children may
coexist, or an object may only serve as a transform group.

| Object property | Default |
|---|---|
| transform.position | [0, 0, 0] |
| transform.rotation | [0, 0, 0] degrees |
| transform.scale | [1, 1, 1] |
| color | [255, 255, 255, 255] |
| visible | true |
| children | empty |

Transforms are local. Euler rotation applies X, then Y, then Z. Coordinates are
right handed, Y up, in metres. Scale components must be nonzero; numeric values
must be finite. Maximum hierarchy depth: 64 child levels. Maximum objects: 100,000.

Colors use three or four integer channels in 0..255. Object color multiplies
material/base texture. Alpha is a 0.5 cutoff, with no alpha blending.

Light type is required: point or directional. Defaults: white, intensity 1,
range 10, direction [0,-1,0], enabled true. Intensity cannot be negative, range
must be positive, direction nonzero. Maximum eight light definitions.

An animation mapping needs a skeletal mesh with clips. Omitting clip selects
the first clip; defaults are time 0, speed 1, playing true, loop true. No animation
mapping means bind pose. Clip names must match the importer. Imported glTF node
animation, morph targets, blending and root motion are not supported.

## Background

Choose a solid RGB color:

~~~yaml
background:
  type: color
  color: [24, 28, 36]
~~~

Or a lightweight procedural skybox:

~~~yaml
background:
  type: skybox
  sky_color: [55, 105, 180]
  horizon_color: [190, 215, 235]
  ground_color: [42, 48, 58]
~~~

The colors above are the skybox defaults; only type: skybox is needed to use
them. Colors are opaque sRGB integer triples in 0..255. The skybox blends sky to
horizon to ground based on the camera's world viewing direction. It follows
camera rotation, has no parallax when moving, and requires no textures. It is
a background only and does not provide scene lighting or reflections.

Omitting background preserves the black default. Omitting type selects color;
omitting color also selects black. Fields belonging to another background type
are rejected. The skybox example is examples/skybox.yaml.

## Physics playback

Add a root physics mapping to enable the interactor. The scene opens paused.
Existing scenes without it retain their usual rendering and animation behaviour.

~~~yaml
physics:
  tick_hz: 60
  substeps: 4
  velocity_iterations: 12
  gravity: [0, -9.81, 0]
  seed: 1234

objects:
  - id: ball
    mesh: sphere
    transform:
      position: [0, 2, 5]
      scale: [1.5, 1.5, 1.5]
    physics:
      type: dynamic
      mass: 12
      linear_velocity: [0, 0, -10]
      angular_velocity: [0, 0, 0]
      linear_damping: 0.05
      angular_damping: 0.05
      gravity_scale: 1
      allow_sleep: true
      collider:
        shape: sphere
        radius: 0.75
      material:
        static_friction: 0.6
        dynamic_friction: 0.4
        restitution: 0.1
~~~

The snippet assumes a sphere mesh is declared in meshes. The complete
[physics playground](../examples/physics_playground.yaml) needs no external assets.

Root settings default to the library defaults: tick_hz 30, substeps 2,
velocity_iterations 8, gravity [0, -9.81, 0], seed 0. tick_hz is an integer
10..1000, substeps 1..16, velocity_iterations 1..64, and seed an unsigned 64-bit
integer. Step advances one full tick, including all substeps.

Each object's physics mapping requires a collider. Body type defaults to dynamic;
static and kinematic are also supported. Unconfigured objects are purely visual
and do not collide. A physics object must be at the scene root. It may have
ordinary visual children, which follow its pose. Nested rigid bodies are rejected.

Supported YAML colliders:

~~~yaml
collider: { shape: box, half_extents: [0.5, 0.5, 0.5] }
# Or:
collider: { shape: sphere, radius: 0.5 }
# Or an explicit convex hull, here a regular tetrahedron:
collider:
  shape: convex
  vertices:
    - [0.5, 0.5, 0.5]
    - [-0.5, -0.5, 0.5]
    - [-0.5, 0.5, -0.5]
    - [0.5, -0.5, -0.5]
  faces: [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]]
~~~

Collider dimensions and vertices are in metres in the body's local coordinates
and do not inherit transform.scale. Boxes and spheres are centred at the body's
origin. A unit cube uses half extents [0.5, 0.5, 0.5];
a unit sphere mesh has radius 0.5. For a visual sphere scaled to diameter 1.5,
set radius to 0.75. A floor uses a thin static box. Colliders need no mesh.

Convex hulls need 4..128 vertices and 4..256 faces. Each face lists 3..128
zero-based vertex indices, wound counterclockwise when viewed from outside.
Faces must be planar, convex and form a closed, convex hull with nonzero volume;
every vertex must be used. Vertex components are limited to +/-128 m. Hull
vertices are read directly as fixed-point decimals. To match an imported visual
mesh, use the same vertex coordinates and account for its visual scale explicitly.
The [500-body chaos scene](../examples/physics_chaos_500.yaml) includes tetrahedra
with matching OBJ geometry and convex colliders.

Initial physics position and rotation come from the object's transform.
Rotation is Euler X, then Y, then Z, in degrees. Physics decimals and rotations
are imported directly using fixed-point math, without a float round trip.
Positions must stay within +/-8192 m; initial Euler components are limited to
+/-360000 degrees. Collider radii/half extents are 0.005..128 m.
Mesh scale remains a rendering property.

Body defaults: mass 1 kg, zero linear/angular velocity, linear/angular damping
0.05, gravity_scale 1, allow_sleep true. Mass is 0.01..10000 kg, damping 0..100,
gravity_scale -10..10. Linear velocity is metres/second; angular velocity is
radians/second. Static bodies must have zero velocity. Kinematic bodies move at
their configured velocity and are not affected by gravity or contact impulses.

Material defaults are static_friction 0.6, dynamic_friction 0.4, restitution 0.
The physics library validates solver, velocity, material and shape limits; invalid
configuration fails the load and preserves the current scene and simulation.
Unknown or duplicate fields are rejected. Unsupported library features such as
compound colliders, mesh collision and springs are not exposed in this YAML version.

Play resumes the current state. Pause freezes physics at the latest completed
tick; the camera remains usable. Reset restores the exact initial world snapshot
(including velocities, contacts, sleep state and RNG), restores bound visual
transforms and pauses. It does not reset the camera or skeletal animation clocks.

While playing, rendering interpolates between completed physics poses. Catch-up
is capped at eight ticks per frame, an 8 ms work budget, and 0.25 seconds of incoming
frame time. An owed tick always finishes before checking the work budget, so one
expensive tick can exceed 8 ms. Excess wall time is dropped: an overloaded
simulation slows down instead of batching eight expensive ticks before rendering.
Tick duration and physics results never change to meet the rendering budget.
A physics exception stops playback and requires Reset. Unrelated scene animations
retain their own existing clocks. The library defaults to 30 Hz; the playground
explicitly uses 60 Hz and four substeps per tick. There is no sleep in physics.
