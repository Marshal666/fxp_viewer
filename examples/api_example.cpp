#include <fxp_viewer/viewer.hpp>
#include <cmath>
#include <iostream>
int main() {
    try {
        fxp_viewer::Viewer viewer;
        auto& scene=viewer.scene();
        auto group=scene.createObject("moving_group");
        auto box=scene.addMesh("box",viewer.renderer().cube(),group);
        scene.setPosition(box,{0,1,0});
        scene.object(box).tint={244,148,77,255};
        // Programmatic geometry: CCW triangles, generated normals.
        fxp_viewer::MeshData triangle;
        triangle.vertices={{{-1,0,0}},{{1,0,0}},{{0,2,0}}};
        auto custom=scene.addMesh("custom",viewer.renderer().createMesh(triangle));
        scene.setPosition(custom,{-2,0,0});
        scene.object(custom).tint={94,203,214,255};
        auto floor=scene.addMesh("floor",viewer.renderer().plane());
        scene.setScale(floor,{12,1,12});
        scene.object(floor).tint={70,77,86,255};
        fxp_viewer::Light sun;
        sun.direction={-1,-2,-1};
        scene.addLight("sun",sun);
        double time=0;
        viewer.run([&](fxp_viewer::Viewer&,double dt) {
            // A physics integration would advance its own fixed timestep here,
            // then publish poses. This example only animates a render object.
            time+=dt;
            scene.setPosition(group,{static_cast<float>(std::sin(time)),0,0});
            scene.setRotationDegrees(box,{0,static_cast<float>(time*35),0});
        });
        // scene.removeObject(group) would remove the group and its box.
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

