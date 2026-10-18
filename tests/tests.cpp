#include <fxp_viewer/viewer.hpp>
#include <raymath.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace fxp_viewer;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void near(float a,float b) { require(std::abs(a-b)<0.0002f,"Transform mismatch"); }
template<class F> void rejects(F f) { bool threw=false; try { f(); } catch (const std::exception&) { threw=true; } require(threw,"Expected rejection"); }
Color sample(int x,int y) {
    auto image=LoadImageFromScreen();
    auto pixel=GetImageColor(image,x,y);
    UnloadImage(image);
    return pixel;
}
bool sameColor(Color a,Color b) {
    return std::abs(int(a.r)-int(b.r))<=2 && std::abs(int(a.g)-int(b.g))<=2 && std::abs(int(a.b)-int(b.b))<=2;
}
void testBackgrounds(Viewer& viewer,const std::filesystem::path& project) {
    std::filesystem::create_directories("test-output");
    auto fixture=std::filesystem::path("test-output/background.yaml");
    auto load=[&](const char* background) {
        { std::ofstream f(fixture); f<<"version: 1\nbackground: "<<background<<"\n"; }
        return viewer.loadScene(fixture);
    };
    require(load("{type: color, color: [24, 28, 36]}"),"Solid background failed to load");
    viewer.frame(0);
    require(sameColor(sample(20,100),{24,28,36,255}),"Solid background did not render");
    for (auto invalid: {"{type: invalid}","{color: [256, 0, 0]}","{color: [-1, 0, 0]}",
                       "{color: [0, 0]}","{color: [0, 0, 0, 255]}","black",
                       "{type: skybox, color: [0, 0, 0]}","{type: color, sky_color: [0, 0, 0]}",
                       "{type: color, type: skybox}","{type: skybox, sky_color: [0, 0, 999]}"}) {
        require(!load(invalid),"Invalid background accepted");
        require(viewer.scene().background.type==BackgroundType::Color &&
                sameColor(viewer.scene().background.color,{24,28,36,255}),"Failed load changed background");
    }
    require(load("{type: skybox, sky_color: [255, 0, 0], horizon_color: [0, 255, 0], ground_color: [0, 0, 255]}"),
            "Skybox failed to load");
    CameraSettings camera; camera.position={0,0,3}; camera.target={0,0,0};
    viewer.setCamera(camera); viewer.frame(0);
    auto top=sample(GetScreenWidth()/2,100);
    auto bottom=sample(GetScreenWidth()/2,GetScreenHeight()-100);
    require(top.r>100 && top.b<3 && bottom.b>200 && bottom.r<3,"Skybox colors or horizon are incorrect");
    camera.target={0,10,2};
    viewer.setCamera(camera); viewer.frame(0);
    require(sameColor(sample(GetScreenWidth()/2,100),{255,0,0,255}),"Skybox did not follow camera pitch");
    auto pitched=sample(20,100);
    camera.position=Vector3Add(camera.position,{100,50,-80});
    camera.target=Vector3Add(camera.target,{100,50,-80});
    viewer.setCamera(camera); viewer.frame(0);
    require(sameColor(sample(20,100),pitched),"Skybox moved with camera translation");
    camera.position={0,0,3}; camera.target={0,0,0};
    viewer.setCamera(camera);
    viewer.scene().ambient=WHITE; viewer.scene().ambientIntensity=1;
    viewer.scene().addMesh("foreground",viewer.renderer().cube());
    viewer.frame(0);
    require(sameColor(sample(GetScreenWidth()/2,GetScreenHeight()/2),WHITE),"Skybox obscured foreground geometry");
    require(load("{color: [36, 28, 24]}"),"Skybox to color reload failed");
    viewer.frame(0);
    require(sameColor(sample(20,100),{36,28,24,255}),"Skybox shader leaked into solid background");
    require(viewer.loadScene(project/"examples/empty.yaml"),"Empty scene failed to load");
    viewer.frame(0);
    require(viewer.scene().background.type==BackgroundType::Color && sameColor(sample(20,100),BLACK),
            "Omitted background did not restore black");
    require(viewer.loadScene(project/"examples/skybox.yaml"),"Skybox example failed to load");
    viewer.frame(0);
    std::cout<<"Background validation, rendering, camera and reload tests passed\n";
}
int main(int argc,char** argv) {
    try {
        Scene scene;
        auto parent=scene.createObject("parent"),child=scene.createObject("child",parent);
        scene.setPosition(parent,{3,0,0}); scene.setRotationDegrees(parent,{0,0,90}); scene.setScale(parent,{2,2,2});
        scene.setPosition(child,{1,0,0});
        auto world=Vector3Transform({0,0,0},scene.worldMatrix(child));
        near(world.x,3); near(world.y,2); near(world.z,0);
        rejects([&]{scene.setParent(parent,child);});
        require(scene.object(child).parent()==parent,"Cycle rejection changed hierarchy");
        rejects([&]{scene.createObject("child");});
        rejects([&]{scene.setParent(child,999);});
        rejects([&]{scene.setScale(child,{1,0,1});});
        rejects([&]{scene.setRotation(child,{0,0,0,0});});
        rejects([&]{scene.setPosition(child,{std::numeric_limits<float>::infinity(),0,0});});
        scene.setParent(child,root);
        near(Vector3Transform({0,0,0},scene.worldMatrix(child)).x,1);
        auto grandchild=scene.createObject("grandchild",child);
        scene.removeObject(child);
        require(!scene.contains(child)&&!scene.contains(grandchild)&&scene.find("child")==root,"Subtree removal failed");
        auto another=scene.createObject("another"); require(another>grandchild,"Object ID reused");
        scene.rotate(another,rotationDegrees({0,90,0}));
        auto direction=Vector3Transform({1,0,0},scene.worldMatrix(another));
        near(direction.x,0); near(direction.z,-1);
        std::cout<<"Scene API tests passed\n";
        if (argc>1 && std::string(argv[1])=="--render") {
            require(argc>2,"--render needs project root");
            auto project=std::filesystem::u8path(argv[2]);
            ViewerOptions options; options.hidden=true; options.targetFps=0;
            Viewer viewer(options);
            if (!viewer.loadScene(project/"examples/showcase.yaml")) throw std::runtime_error(viewer.lastError());
            auto count=viewer.scene().objects().size();
            auto arm=viewer.scene().find("animated_arm");
            require(arm!=root,"Missing animated object");
            require(!viewer.scene().object(arm).mesh->clips().empty(),"Animation did not load");
            auto second=viewer.scene().addMesh("second_animation",viewer.scene().object(arm).mesh);
            viewer.scene().object(second).animation=viewer.scene().object(arm).animation;
            viewer.scene().object(second).animation.time=1;
            viewer.scene().setPosition(second,{-4,0,-2});
            for (int i=0;i<20;++i) viewer.frame(1.0/60);
            near(static_cast<float>(viewer.scene().object(second).animation.time-viewer.scene().object(arm).animation.time),1);
            auto& animation=viewer.scene().object(arm).animation;
            animation.playing=false; animation.time=0;
            viewer.frame(0);
            auto pose0=LoadImageFromScreen();
            auto pixels0=LoadImageColors(pose0);
            animation.time=1;
            viewer.frame(0);
            auto screenshot=LoadImageFromScreen();
            auto pixels=LoadImageColors(screenshot);
            int lit=0,changed=0;
            for (int y=80;y<screenshot.height-80;++y) for (int x=0;x<screenshot.width;++x) {
                auto p=pixels[y*screenshot.width+x], q=pixels0[y*screenshot.width+x];
                if (p.r>20||p.g>20||p.b>20) ++lit;
                if (std::abs(int(p.r)-int(q.r))+std::abs(int(p.g)-int(q.g))+std::abs(int(p.b)-int(q.b))>30) ++changed;
            }
            UnloadImageColors(pixels); UnloadImage(screenshot);
            UnloadImageColors(pixels0); UnloadImage(pose0);
            require(changed>100,"Skeletal animation did not change rendered geometry");
            require(lit>10000,"Rendered viewport is empty");
            viewer.scene().removeObject(second);
            require(!viewer.loadScene(project/"examples/does-not-exist.yaml"),"Missing scene accepted");
            require(viewer.scene().objects().size()==count,"Failed load discarded scene");
            std::filesystem::create_directories("test-output");
            auto bad=std::filesystem::path("test-output/invalid.yaml");
            {std::ofstream f(bad); f<<"version: 1\nobjects:\n  - id: bad\n    transform:\n      scale: [1, 0, 1]\n";}
            require(!viewer.loadScene(bad),"Invalid transform accepted");
            require(viewer.scene().objects().size()==count,"Validation failure changed scene");
            {std::ofstream f(bad); f<<"version: 1\nunknown: true\n";}
            require(!viewer.loadScene(bad),"Unknown YAML field accepted");
            MeshData mesh; mesh.vertices={{{0,0,0}},{{1,0,0}},{{0,1,0}}}; mesh.indices={0,1,9};
            rejects([&]{viewer.renderer().createMesh(mesh);});
            mesh.indices={0,1,2};
            auto custom=viewer.scene().addMesh("custom_test",viewer.renderer().createMesh(mesh));
            viewer.frame(0); viewer.scene().removeObject(custom);
            for (int i=0;i<3;++i) require(viewer.loadScene(project/"examples/showcase.yaml"),"Reload failed");
            viewer.frame(0);
            int width=GetScreenWidth(),height=GetScreenHeight();
            viewer.toggleFullscreen(); viewer.frame(0);
            require(IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE),"Fullscreen did not activate");
            viewer.toggleFullscreen(); viewer.frame(0);
            require(!IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE),"Fullscreen did not deactivate");
            require(GetScreenWidth()==width&&GetScreenHeight()==height,"Window dimensions were not restored");
            testBackgrounds(viewer,project);
            std::cout<<"Rendering, asset, animation, reload and failure-preservation tests passed\n";
        }
        return 0;
    } catch (const std::exception& e) { std::cerr<<"TEST FAILURE: "<<e.what()<<'\n'; return 1; }
}



