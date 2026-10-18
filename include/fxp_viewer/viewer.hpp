#pragma once
#include "renderer.hpp"
#include <functional>
namespace fxp_viewer {
struct ViewerOptions {
    std::string title = "fxp_viewer";
    int width = 1280;
    int height = 800;
    int targetFps = 120;
    bool hidden = false;
};
struct CameraSettings {
    Vector3 position{7,5,8};
    Vector3 target{0,1,0};
    float fov = 55;
    float speed = 5;
};
struct SceneDocument { Scene scene; CameraSettings camera; };
// Throws without modifying an existing scene.
SceneDocument loadSceneFile(const std::filesystem::path& path, Renderer& renderer);
class Viewer {
public:
    explicit Viewer(const ViewerOptions& options = {});
    ~Viewer();
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;
    Scene& scene();
    const Scene& scene() const;
    Renderer& renderer();
    bool loadScene(const std::filesystem::path& path); // Keeps old scene on error.
    const std::string& lastError() const;
    void setCamera(const CameraSettings& camera);
    CameraSettings camera() const;
    void resetCamera();
    void toggleFullscreen();
    bool shouldClose() const;
    void requestClose();
    void frame(double deltaSeconds);
    // Before each frame; own your simulation clock here.
    void run(const std::function<void(Viewer&,double)>& beforeFrame = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

