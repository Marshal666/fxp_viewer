#pragma once
#include "scene.hpp"
#include <filesystem>
namespace fxp_viewer {
struct Vertex {
    Vector3 position;
    Vector3 normal{0,0,0}; // All-zero normals are generated automatically.
    Vector2 uv{0,0};
};
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices; // Optional CCW triangle indices.
};
struct AnimationClip { std::string name; double duration; };
class MeshAsset {
public:
    ~MeshAsset();
    MeshAsset(const MeshAsset&) = delete;
    MeshAsset& operator=(const MeshAsset&) = delete;
    const std::vector<AnimationClip>& clips() const;
    BoundingBox bounds() const;
private:
    friend class Renderer;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MeshAsset();
};
class Renderer {
public:
    static constexpr int maxLights = 8;
    Renderer(); // Requires an open window/context.
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    MeshHandle loadModel(const std::filesystem::path& path);
    MeshHandle createMesh(const MeshData& data);
    MeshHandle cube(); // Unit cube, centred at origin.
    MeshHandle sphere(); // Radius 0.5m.
    MeshHandle plane(); // Unit XZ plane, normal +Y.
    // Overrides all material slots of this shared asset.
    void setColorTexture(const MeshHandle& mesh, const std::filesystem::path& path);
    void updateAnimations(Scene& scene, double deltaSeconds);
    void draw(const Scene& scene, const Camera3D& camera);
private:
    MeshHandle adoptModel(Model model);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}


