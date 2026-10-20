#include <fxp_viewer/viewer.hpp>
#include <raymath.h>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <iomanip>
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
    auto bottom=sample(20,GetScreenHeight()-100);
    require(top.r>100 && top.b<3 && bottom.b>100 && bottom.r<3,"Skybox colors or horizon are incorrect");
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
void clickPlayback(Viewer& viewer,int button) {
    // Event IDs are from the pinned raylib AutomationEventType.
    const int x=GetScreenWidth()/2-135+button*90,y=GetScreenHeight()-91;
    PlayAutomationEvent({0,7,{x,y,0,0}});
    PlayAutomationEvent({0,6,{MOUSE_BUTTON_LEFT,0,0,0}});
    viewer.frame(0);
    PlayAutomationEvent({0,7,{x,y,0,0}});
    PlayAutomationEvent({0,5,{MOUSE_BUTTON_LEFT,0,0,0}});
    viewer.frame(0);
}
void testConvexColliders(Viewer& viewer) {
    using namespace fxp_physics;
    const auto fixture=std::filesystem::path("test-output/convex-physics.yaml");
    const std::string vertices="[[0.500000001,0.5,0.5],[-0.5,-0.5,0.5],[-0.5,0.5,-0.5],[0.5,-0.5,-0.5]]";
    const std::string faces="[[0,2,1],[0,1,3],[0,3,2],[1,2,3]]";
    const auto hull="shape: convex, vertices: "+vertices+", faces: "+faces;
    auto write=[&](const std::string& collider) {
        std::ofstream f(fixture);
        f<<"version: 1\nphysics: {tick_hz: 20, substeps: 4}\nobjects:\n"
           "  - id: floor\n    transform: {position: [0, -0.5, 0]}\n"
           "    physics: {type: static, collider: {shape: box, half_extents: [5, 0.5, 5]}}\n"
           "  - id: tetra\n    transform: {position: [0, 3, 0]}\n"
           "    physics: {collider: {"<<collider<<"}}\n";
    };
    write(hull);
    require(viewer.loadScene(fixture),"Convex physics fixture failed to load");
    auto document=loadSceneFile(fixture,viewer.renderer());
    require(document.physics->bodies.back().shape->type()==ShapeType::convex,"Convex collider became another shape");
    World expected(document.physics->config,document.physics->seed);
    for (const auto& entry:document.physics->bodies) {
        auto body=entry.body; body.user_data=entry.object;
        ColliderDesc collider;
        collider.body=expected.create_body(body);
        collider.material=entry.material;
        collider.shape=entry.shape;
        if (entry.object==document.scene.find("tetra"))
            collider.shape=Shape::convex({{Real::from_string("0.500000001"),0.5_r,0.5_r},
                {-0.5_r,-0.5_r,0.5_r},{-0.5_r,0.5_r,-0.5_r},{0.5_r,-0.5_r,-0.5_r}},
                {{0,2,1},{0,1,3},{0,3,2},{1,2,3}});
        expected.create_collider(collider);
    }
    auto sim=viewer.simulation();
    require(sim->world().save()==expected.save(),"Convex vertex decimal lost precision");
    for (int i=0;i<120;++i) sim->step(viewer.scene());
    const auto tetra=viewer.scene().find("tetra");
    const auto y=sim->world().body(sim->bodyId(tetra)).properties.pose.position.y;
    require(y>0.2_r && y<1_r,"Convex tetrahedron did not land on the floor");
    const auto settled=sim->world().save();
    const auto repeated=[](const std::string& value,int count) {
        std::string result="[";
        for (int i=0;i<count;++i) { if (i) result+=','; result+=value; }
        return result+"]";
    };
    const std::vector<std::string> invalid={
        hull+", unexpected: true",hull+", faces: "+faces,
        "shape: convex, vertices: {}, faces: "+faces,
        "shape: convex, vertices: [[0,0,0],[1,0,0],[0,1,0]], faces: "+faces,
        "shape: convex, vertices: "+repeated("[0,0,0]",129)+", faces: "+faces,
        "shape: convex, vertices: [[0,0],[1,0,0],[0,1,0],[0,0,1]], faces: "+faces,
        "shape: convex, vertices: [[.nan,0,0],[1,0,0],[0,1,0],[0,0,1]], faces: "+faces,
        "shape: convex, vertices: "+vertices+", faces: {}",
        "shape: convex, vertices: "+vertices+", faces: [[0,2,1],[0,1,3],[0,3,2]]",
        "shape: convex, vertices: "+vertices+", faces: "+repeated("[0,2,1]",257),
        "shape: convex, vertices: "+vertices+", faces: ["+repeated("0",129)+",[0,1,3],[0,3,2],[1,2,3]]"
    };
    for (const auto& collider:invalid) {
        write(collider);
        require(!viewer.loadScene(fixture),"Invalid convex shape accepted");
        require(viewer.simulation()==sim && sim->world().save()==settled,"Invalid convex load discarded current state");
    }
    for (const auto& face: {"[0,2]","[0,1,2]","[0,2,2]","[0,2,-1]","[0,2,4]",
                           "[0,2,1.5]","[0,2,9223372036854775808]","[0,2,[]]","[0,2,true]"}) {
        write("shape: convex, vertices: "+vertices+", faces: ["+face+",[0,1,3],[0,3,2],[1,2,3]]");
        require(!viewer.loadScene(fixture),"Invalid convex face accepted");
        require(viewer.simulation()==sim && sim->world().save()==settled,"Invalid convex face load discarded current state");
    }
    std::cout<<"Convex YAML precision, collision and invalid-hull tests passed\n";
}
void testPhysicsScenes(Viewer& viewer,const std::filesystem::path& project) {
    using namespace fxp_physics;
    if (!viewer.loadScene(project/"examples/physics_playground.yaml")) throw std::runtime_error(viewer.lastError());
    auto sim=viewer.simulation();
    require(sim && !sim->playing() && sim->tick()==0,"Physics scene did not start paused");
    auto initial=sim->world().save();
    auto originalCamera=viewer.camera();
    viewer.frame(0);
    clickPlayback(viewer,0); require(sim->playing(),"Play button failed");
    viewer.frame(1.0/30);
    require(sim->tick()>0,"Viewer did not advance the simulation");
    clickPlayback(viewer,1); require(!sim->playing(),"Pause button failed");
    auto pausedTick=sim->tick();
    viewer.frame(1); require(sim->tick()==pausedTick,"Paused viewer advanced physics");
    clickPlayback(viewer,0);
    clickPlayback(viewer,2);
    require(!sim->playing()&&sim->tick()==pausedTick+1,"Step button did not advance exactly one tick and pause");
    clickPlayback(viewer,3);
    require(sim->world().save()==initial && !sim->playing(),"Reset button did not restore initial state");
    near(viewer.camera().position.x,originalCamera.position.x);
    near(viewer.camera().target.y,originalCamera.target.y);
    auto projectile=sim->bodyId(viewer.scene().find("projectile"));
    bool wallHit=false;
    for (int i=0;i<240;++i) {
        sim->step(viewer.scene());
        for (auto& event:sim->world().events()) {
            auto other=event.body_a==projectile?event.body_b:event.body_b==projectile?event.body_a:0;
            if (other) {
                auto id=sim->world().body(other).properties.user_data;
                if (viewer.scene().object(id).name().find("wall_")==0) wallHit=true;
            }
        }
    }
    require(wallHit,"Demo projectile never hit the wall");
    require(std::abs(viewer.scene().object(viewer.scene().find("wall_0_2")).transform.position.z)>0.5f,
            "Demo wall did not break apart");
    require(viewer.scene().object(viewer.scene().find("tower_7")).transform.position.y<2,
            "Demo tower did not collapse");
    auto finalState=sim->world().save();
    std::cout<<"Physics demo tick 240 checksum "<<sim->world().checksum()<<'\n';
    viewer.frame(0);
    auto image=LoadImageFromScreen();
    require(ExportImage(image,"test-output/physics-result.png"),"Could not export physics screenshot");
    UnloadImage(image);
    sim->reset(viewer.scene());
    sim->play();
    double dt=static_cast<double>(sim->world().config().timestep.raw_value())/4294967296.0;
    for (int i=0;i<480;++i) sim->update(viewer.scene(),dt/2);
    sim->pause(viewer.scene());
    require(sim->world().save()==finalState,"Demo reset/replay was not deterministic");

    auto invalid=std::filesystem::path("test-output/invalid-physics.yaml");
    for (auto yaml: {
            "physics: {tick_hz: 0}\n",
            "physics: {substeps: 0}\n",
            "physics: {gravity: [0, .nan, 0]}\n",
            "physics: {unexpected: true}\n",
            "physics: false\n",
            "physics: {}\nobjects: [{id: bad, physics: {collider: {shape: sphere, radius: -1}}}]\n",
            "physics: {}\nobjects: [{id: bad, physics: {mass: 0, collider: {shape: sphere, radius: 1}}}]\n",
            "physics: {}\nobjects: [{id: bad, physics: {collider: {shape: box, half_extents: [1, 1]}}}]\n",
            "physics: {}\nobjects: [{id: parent, children: [{id: child, physics: {collider: {shape: sphere, radius: 1}}}]}]\n",
            "objects: [{id: bad, physics: {collider: {shape: sphere, radius: 1}}}]\n"}) {
        { std::ofstream f(invalid); f<<"version: 1\n"<<yaml; }
        require(!viewer.loadScene(invalid),"Invalid physics YAML accepted");
        require(viewer.simulation()==sim && sim->world().save()==finalState,"Failed load discarded physics state");
    }
    // Exact decimal import and Euler conversion, without a float round trip.
    { std::ofstream f(invalid);
      f<<"version: 1\nphysics: {}\nobjects:\n  - id: precise\n    transform:\n"
          "      position: [1.000000001, 0, 0]\n      rotation: [25, 40, 65]\n"
          "    physics:\n      collider: {shape: sphere, radius: 0.5}\n"; }
    require(viewer.loadScene(invalid),"Precise physics fixture failed to load");
    auto id=viewer.scene().find("precise");
    auto state=viewer.simulation()->world().body(viewer.simulation()->bodyId(id));
    require(state.properties.pose.position.x==Real::from_string("1.000000001"),"Physics decimal lost precision");
    auto before=viewer.scene().object(id).transform.rotation;
    viewer.simulation()->pause(viewer.scene());
    auto after=viewer.scene().object(id).transform.rotation;
    near(std::abs(Vector4DotProduct(before,after)),1);
    testConvexColliders(viewer);
    require(viewer.loadScene(project/"examples/empty.yaml")&&!viewer.simulation(),"Nonphysics scene retained old simulation");
    clickPlayback(viewer,0); // Disabled controls must be harmless.
    std::cout<<"Physics YAML, controls, wall impact, tower collapse and replay tests passed\n";
}
int benchmarkPhysics(const std::filesystem::path& path) {
    using Clock=std::chrono::steady_clock;
    SetTraceLogLevel(LOG_WARNING);
    ViewerOptions options; options.hidden=true; options.targetFps=0;
    Viewer viewer(options);
    auto document=loadSceneFile(path,viewer.renderer());
    require(document.physics.has_value(),"Benchmark needs a physics scene");
    const auto& setup=*document.physics;
    fxp_physics::World world(setup.config,setup.seed);
    for (const auto& entry:setup.bodies) {
        auto desc=entry.body; desc.user_data=entry.object;
        fxp_physics::ColliderDesc collider;
        collider.body=world.create_body(desc); collider.shape=entry.shape; collider.material=entry.material;
        world.create_collider(collider);
    }
    std::vector<double> times;
    double total=0,batch=0;
    unsigned maxContacts=0;
    for (int i=0;i<240;++i) {
        auto begin=Clock::now();
        world.step();
        double ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        times.push_back(ms); total+=ms; batch+=ms;
        maxContacts=std::max(maxContacts,world.stats().contacts);
        if ((i+1)%60==0) {
            std::cout<<"Ticks "<<i-58<<".."<<i+1<<": "<<batch/60<<" ms/tick\n";
            batch=0;
        }
    }
    std::sort(times.begin(),times.end());
    std::cout<<std::fixed<<std::setprecision(3)
        <<"Physics: mean "<<total/240<<" ms, p95 "<<times[227]<<" ms, max "<<times.back()
        <<" ms; peak contacts "<<maxContacts<<"; checksum "<<world.checksum()<<'\n';
    if (!viewer.loadScene(path)) throw std::runtime_error(viewer.lastError());
    // Paused rendering includes the same objects and GUI, without physics work.
    for (int i=0;i<10;++i) viewer.frame(0);
    auto begin=Clock::now();
    for (int i=0;i<120;++i) viewer.frame(0);
    std::cout<<"Paused rendering: "<<std::chrono::duration<double,std::milli>(Clock::now()-begin).count()/120<<" ms/frame\n";
    viewer.simulation()->play();
    double delta=1.0/60.0,frameTotal=0; std::uint64_t maxTicks=0;
    int frames=0;
    while (frameTotal<5.0 && frames<10000) {
        ++frames;
        auto previous=viewer.simulation()->tick();
        begin=Clock::now();
        viewer.frame(delta);
        delta=std::chrono::duration<double>(Clock::now()-begin).count();
        frameTotal+=delta;
        maxTicks=std::max(maxTicks,viewer.simulation()->tick()-previous);
    }
    std::cout<<"Playback: "<<frames/frameTotal<<" FPS, "<<frameTotal*1000/frames
        <<" ms/frame, "<<viewer.simulation()->tick()<<" ticks, max "<<maxTicks<<" ticks/frame"
        <<", simulation/wall time "<<viewer.simulation()->time()/frameTotal<<"x\n";
    return 0;
}
int main(int argc,char** argv) {
    try {
        if (argc>1 && std::string(argv[1])=="--benchmark") {
            require(argc>2,"--benchmark needs a physics scene YAML path");
            return benchmarkPhysics(std::filesystem::u8path(argv[2]));
        }
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
            testPhysicsScenes(viewer,project);
            std::cout<<"Rendering, asset, animation, reload and failure-preservation tests passed\n";
        }
        return 0;
    } catch (const std::exception& e) { std::cerr<<"TEST FAILURE: "<<e.what()<<'\n'; return 1; }
}



