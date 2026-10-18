# Third-party dependencies

CMake downloads unmodified pinned sources:

| Dependency | Revision | License |
|---|---|---|
| [raylib](https://github.com/raysan5/raylib) 6.2 | 02566453880e06bec97f5b8160fa6fe1e98397ca | zlib |
| [raygui](https://github.com/raysan5/raygui) 5.0 | 020a61bebcbe288b4414de3416e219ef40af847a | zlib |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) 0.8.0 | f7320141120f720aecc4c32be25586e7da9eb978 | MIT |

Top-level license texts are in LICENSES/. Raylib also bundles GLFW, cgltf, stb
and other libraries with notices in its source tree. Preserve applicable notices
when redistributing those dependencies. Additional bundled notices remain in the fetched raylib source tree.

The demo meshes, animation and checker texture were created for this project.
Their reproducible source is tools/generate-demo-assets.py. No third-party
sample art is used.


