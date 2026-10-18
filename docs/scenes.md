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
meshes and objects. Missing objects means an empty scene.

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
