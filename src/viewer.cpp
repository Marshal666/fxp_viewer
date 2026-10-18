#include <fxp_viewer/viewer.hpp>
#include "gui.hpp"
#include <raymath.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
namespace fxp_viewer {
namespace {
struct Window {
    explicit Window(const ViewerOptions& options) {
        if (IsWindowReady()) throw std::runtime_error("Only one Viewer/window may be active");
        if (options.width<320||options.height<240||options.targetFps<0) throw std::invalid_argument("Invalid window options");
        unsigned flags=FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT;
        if (options.hidden) flags|=FLAG_WINDOW_HIDDEN;
        SetConfigFlags(flags);
        InitWindow(options.width,options.height,options.title.c_str());
        if (!IsWindowReady()) throw std::runtime_error("Could not create OpenGL window");
        SetWindowMinSize(640,400);
        SetExitKey(KEY_NULL);
        SetTargetFPS(options.targetFps);
    }
    ~Window() { if (IsCursorHidden()) EnableCursor(); CloseWindow(); }
};
bool finite(Vector3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
Camera3D camera3d(const CameraSettings& c) { return {c.position,c.target,{0,1,0},c.fov,CAMERA_PERSPECTIVE}; }
void rotateCamera(CameraSettings& c,Vector2 mouse,bool orbit) {
    auto forward=Vector3Normalize(Vector3Subtract(c.target,c.position));
    float distance=Vector3Distance(c.position,c.target);
    float yaw=std::atan2(forward.x,forward.z)-mouse.x*0.004f;
    float pitch=std::clamp(std::asin(forward.y)-mouse.y*0.004f,-1.5533f,1.5533f);
    forward={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    if (orbit) c.position=Vector3Subtract(c.target,Vector3Scale(forward,distance));
    else c.target=Vector3Add(c.position,Vector3Scale(forward,distance));
}
}
struct Viewer::Impl {
    Window window; // Construct first, destroy last.
    Renderer renderer;
    Scene scene;
    Gui gui;
    CameraSettings camera,initialCamera;
    std::string error;
    bool close=false,captured=false;
    Vector2 windowPosition{};
    int windowWidth=1280,windowHeight=800;
    explicit Impl(const ViewerOptions& options):window(options) {}
    void cameraInput(double seconds) {
        const bool focused=IsWindowFocused();
        if (captured && (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)||!focused||gui.modalActive())) {
            EnableCursor(); captured=false;
        }
        if (!focused || (captured ? gui.modalActive() : gui.blocksCamera())) return;
        bool started=false;
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            DisableCursor(); captured=true; started=true;
        }
        auto mouse=GetMouseDelta();
        if (captured) {
            if (!started) rotateCamera(camera,mouse,false);
            auto forward=Vector3Normalize(Vector3Subtract(camera.target,camera.position));
            auto right=Vector3Normalize(Vector3CrossProduct(forward,{0,1,0}));
            Vector3 movement{};
            if (IsKeyDown(KEY_W)) movement=Vector3Add(movement,forward);
            if (IsKeyDown(KEY_S)) movement=Vector3Subtract(movement,forward);
            if (IsKeyDown(KEY_D)) movement=Vector3Add(movement,right);
            if (IsKeyDown(KEY_A)) movement=Vector3Subtract(movement,right);
            if (IsKeyDown(KEY_E)) movement.y+=1;
            if (IsKeyDown(KEY_Q)) movement.y-=1;
            camera.speed=std::clamp(camera.speed*std::pow(1.2f,GetMouseWheelMove()),0.05f,1000.0f);
            float speed=camera.speed*(IsKeyDown(KEY_LEFT_SHIFT)?4.0f:1.0f);
            movement=Vector3Scale(Vector3Normalize(movement),speed*static_cast<float>(std::min(seconds,0.1)));
            camera.position=Vector3Add(camera.position,movement); camera.target=Vector3Add(camera.target,movement);
        } else {
            bool alt=IsKeyDown(KEY_LEFT_ALT)||IsKeyDown(KEY_RIGHT_ALT);
            if (alt&&IsMouseButtonDown(MOUSE_BUTTON_LEFT)) rotateCamera(camera,mouse,true);
            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
                auto forward=Vector3Normalize(Vector3Subtract(camera.target,camera.position));
                auto right=Vector3Normalize(Vector3CrossProduct(forward,{0,1,0}));
                auto up=Vector3CrossProduct(right,forward);
                float factor=2*Vector3Distance(camera.position,camera.target)*std::tan(camera.fov*DEG2RAD/2)/GetScreenHeight();
                auto delta=Vector3Scale(Vector3Add(Vector3Scale(right,-mouse.x),Vector3Scale(up,mouse.y)),factor);
                camera.position=Vector3Add(camera.position,delta); camera.target=Vector3Add(camera.target,delta);
            }
            float wheel=GetMouseWheelMove();
            if (wheel!=0) {
                auto offset=Vector3Subtract(camera.position,camera.target);
                float distance=std::clamp(Vector3Length(offset)*std::pow(0.85f,wheel),0.05f,100000.0f);
                camera.position=Vector3Add(camera.target,Vector3Scale(Vector3Normalize(offset),distance));
            }
        }
    }
};
Viewer::Viewer(const ViewerOptions& options):impl_(std::make_unique<Impl>(options)) {}
Viewer::~Viewer()=default;
Scene& Viewer::scene() { return impl_->scene; }
const Scene& Viewer::scene() const { return impl_->scene; }
Renderer& Viewer::renderer() { return impl_->renderer; }
const std::string& Viewer::lastError() const { return impl_->error; }
bool Viewer::loadScene(const std::filesystem::path& path) {
    try {
        auto document=loadSceneFile(path,impl_->renderer);
        impl_->scene=std::move(document.scene);
        setCamera(document.camera);
        impl_->error.clear();
        SetWindowTitle(("fxp_viewer - "+path.filename().u8string()).c_str());
        return true;
    } catch (const std::exception& e) {
        impl_->error=e.what(); TraceLog(LOG_WARNING,"%s",e.what()); return false;
    }
}
void Viewer::setCamera(const CameraSettings& c) {
    auto d=Vector3Subtract(c.target,c.position);
    if (!finite(c.position)||!finite(c.target)||!std::isfinite(c.fov)||c.fov<5||c.fov>150||
        !std::isfinite(c.speed)||c.speed<=0||d.x*d.x+d.z*d.z<1e-8f)
        throw std::invalid_argument("Invalid camera settings");
    impl_->camera=c; impl_->initialCamera=c;
}
CameraSettings Viewer::camera() const { return impl_->camera; }
void Viewer::resetCamera() { impl_->camera=impl_->initialCamera; }
void Viewer::toggleFullscreen() {
    auto& v=*impl_;
    if (!IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
        v.windowPosition=GetWindowPosition(); v.windowWidth=GetScreenWidth(); v.windowHeight=GetScreenHeight();
        ToggleBorderlessWindowed();
    } else {
        ToggleBorderlessWindowed();
        SetWindowSize(v.windowWidth,v.windowHeight);
        SetWindowPosition(static_cast<int>(v.windowPosition.x),static_cast<int>(v.windowPosition.y));
    }
}
bool Viewer::shouldClose() const { return impl_->close||WindowShouldClose(); }
void Viewer::requestClose() { impl_->close=true; }
void Viewer::frame(double dt) {
    auto& v=*impl_;
    if (!std::isfinite(dt)||dt<0) throw std::invalid_argument("Frame delta must be finite and nonnegative");
    if (IsKeyPressed(KEY_F11)) toggleFullscreen();
    if (IsKeyPressed(KEY_ESCAPE)&&IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) toggleFullscreen();
    if ((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL))&&IsKeyPressed(KEY_O))
        v.gui.openPicker(std::filesystem::current_path());
    if (IsKeyPressed(KEY_HOME)&&!v.gui.blocksCamera()) resetCamera();
    if (IsFileDropped()) {
        auto files=LoadDroppedFiles();
        std::filesystem::path selected;
        for (unsigned int i=0;i<files.count;++i) {
            auto p=std::filesystem::u8path(files.paths[i]); auto ext=p.extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if (ext==".yaml"||ext==".yml") { selected=p; break; }
        }
        UnloadDroppedFiles(files);
        if (selected.empty()) v.gui.showError("Drop a .yaml or .yml scene file.");
        else if (!loadScene(selected)) v.gui.showError(lastError());
    }
    v.cameraInput(dt);
    v.renderer.updateAnimations(v.scene,dt);
    BeginDrawing();
    ClearBackground(BLACK);
    v.renderer.draw(v.scene,camera3d(v.camera));
    if (v.scene.objects().empty()) {
        const char* title="Open a scene to begin";
        DrawText(title,(GetScreenWidth()-MeasureText(title,24))/2,GetScreenHeight()/2-24,24,{206,214,224,255});
        const char* hint="File > Open   /   Ctrl+O   /   Drop a YAML file";
        DrawText(hint,(GetScreenWidth()-MeasureText(hint,16))/2,GetScreenHeight()/2+16,16,{125,144,159,255});
    }
    v.gui.draw(*this);
    EndDrawing();
}
void Viewer::run(const std::function<void(Viewer&,double)>& callback) {
    while (!shouldClose()) {
        double dt=GetFrameTime();
        if (callback) callback(*this,dt);
        frame(dt);
    }
}
}


