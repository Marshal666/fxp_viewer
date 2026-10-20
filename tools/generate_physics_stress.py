#!/usr/bin/env python3
"""Generate viewer stress scenes matching the headless physics benchmark."""

import argparse
from pathlib import Path


def scene_text(scene, bodies):
    stack = scene == "stack"
    if stack:
        columns = (bodies + 7) // 8
        rows = (columns + 49) // 50
        center_x, center_z = min(columns, 50) - 50, rows - 50
        span = max(20, 2 * (min(columns, 50) - 1), 2 * (rows - 1))
    else:
        rows = (bodies + 99) // 100
        center_x, center_z = min(bodies, 100) - 1, rows - 1
        span = max(20, 2 * center_x, 2 * center_z)
    yield f"""# {bodies} moving bodies; open in fxp_viewer and press Play.
# Stack scenes also contain one static ground body.
version: 1
background:
  type: color
  color: [24, 28, 36]
camera:
  position: [{center_x + span // 2}, {span}, {center_z + span}]
  target: [{center_x}, {4 if stack else 0}, {center_z}]
  fov: 55
physics:
  tick_hz: 60
  substeps: 4
  velocity_iterations: 12
  gravity: [0, {"-9.81" if stack else "0"}, 0]
  seed: 1234
ambient:
  color: [220, 230, 255]
  intensity: 0.35
meshes:
  cube: {{ primitive: cube }}
  ball: {{ primitive: sphere }}
objects:
  - id: sun
    light:
      type: directional
      direction: [-1, -2, -1]
      color: [255, 239, 215]
      intensity: 1.1
"""
    if stack:
        yield """  - id: ground
    mesh: cube
    color: [70, 80, 90]
    transform:
      position: [0, -0.5, 0]
      scale: [200, 1, 200]
    physics:
      type: static
      collider: { shape: box, half_extents: [100, 0.5, 100] }
"""
    for i in range(bodies):
        color = "[90, 170, 220]" if i % 2 else "[225, 170, 95]"
        if stack:
            column, layer = divmod(i, 8)
            x, z = (column % 50) * 2 - 49, (column // 50) * 2 - 49
            cents = 50 + 101 * layer
            height = f"{cents // 100}.{cents % 100:02d}"
            yield f"""  - id: body_{i}
    mesh: cube
    color: {color}
    transform:
      position: [{x}, {height}, {z}]
    physics:
      type: dynamic
      collider: {{ shape: box, half_extents: [0.5, 0.5, 0.5] }}
"""
        else:
            x, z = (i % 100) * 2, (i // 100) * 2
            yield f"""  - id: body_{i}
    mesh: ball
    color: {color}
    transform:
      position: [{x}, 0, {z}]
      scale: [0.8, 0.8, 0.8]
    physics:
      type: {scene}
      linear_velocity: [0.5, 0, 0]
      linear_damping: 0
      allow_sleep: false
      collider: {{ shape: sphere, radius: 0.4 }}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", required=True, choices=("kinematic", "dynamic", "stack"))
    parser.add_argument("--bodies", required=True, type=int, choices=(100, 500, 1000, 5000, 10000),
                        help="moving body count; stack adds one static ground body")
    parser.add_argument("--output", required=True, type=Path, help="new YAML file; existing files are rejected")
    args = parser.parse_args()
    try:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8", newline="\n") as stream:
            stream.writelines(scene_text(args.scene, args.bodies))
    except OSError as error:
        parser.error(str(error))
    print(f"Wrote {args.output}: {args.scene}, {args.bodies} moving bodies")


if __name__ == "__main__":
    main()
