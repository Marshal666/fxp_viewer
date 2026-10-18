#pragma once
#include <raylib.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace fxp_viewer {
using ObjectId = std::uint64_t;
constexpr ObjectId root = 0;
class MeshAsset;
using MeshHandle = std::shared_ptr<MeshAsset>;
// Right handed, Y up, metres; quaternion is (x,y,z,w).
struct Transform {
    Vector3 position{0,0,0};
    Quaternion rotation{0,0,0,1};
    Vector3 scale{1,1,1};
    Matrix matrix() const;
};
// Apply X, then Y, then Z rotations, in degrees.
Quaternion rotationDegrees(Vector3 xyz);
struct AnimationState {
    std::string clip; // Empty means bind pose.
    double time = 0; // Seconds, independent for each object.
    float speed = 1;
    bool playing = true;
    bool loop = true;
};
enum class LightType { Directional, Point };
struct Light {
    LightType type = LightType::Directional;
    Color color = WHITE;
    float intensity = 1;
    float range = 10; // Radius in metres.
    Vector3 direction{0,-1,0}; // Local direction light travels.
    bool enabled = true;
};
struct Object {
    Transform transform;
    MeshHandle mesh;
    Color tint = WHITE;
    bool visible = true; // Also hides descendants and lights.
    AnimationState animation;
    std::unique_ptr<Light> light;
    ObjectId id() const { return id_; }
    ObjectId parent() const { return parent_; }
    const std::string& name() const { return name_; }
    const std::vector<ObjectId>& children() const { return children_; }
private:
    friend class Scene;
    ObjectId id_ = root;
    ObjectId parent_ = root;
    std::string name_;
    std::vector<ObjectId> children_;
};
class Scene {
public:
    Scene() = default;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    ObjectId createObject(std::string name, ObjectId parent = root);
    ObjectId addMesh(std::string name, MeshHandle mesh, ObjectId parent = root);
    ObjectId addLight(std::string name, Light light, ObjectId parent = root);
    void removeObject(ObjectId id); // Includes descendants.
    void setParent(ObjectId id, ObjectId parent); // Preserves LOCAL transform.
    Object& object(ObjectId id);
    const Object& object(ObjectId id) const;
    ObjectId find(const std::string& name) const; // root if not found.
    bool contains(ObjectId id) const;
    void setPosition(ObjectId id, Vector3 position);
    void translate(ObjectId id, Vector3 delta); // Parent coordinates.
    void setRotation(ObjectId id, Quaternion rotation);
    void setRotationDegrees(ObjectId id, Vector3 xyz);
    void rotate(ObjectId id, Quaternion delta); // Local rotation.
    void setScale(ObjectId id, Vector3 scale);
    Matrix worldMatrix(ObjectId id) const;
    const std::map<ObjectId,Object>& objects() const { return objects_; }
    const std::vector<ObjectId>& roots() const { return roots_; }
    Color ambient = WHITE;
    float ambientIntensity = 0.12f;
private:
    std::map<ObjectId,Object> objects_;
    std::map<std::string,ObjectId> names_;
    std::vector<ObjectId> roots_;
    ObjectId nextId_ = 1;
    std::vector<ObjectId>& siblings(ObjectId parent);
};
}

