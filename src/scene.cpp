#include <fxp_viewer/scene.hpp>
#include <raymath.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace fxp_viewer {
namespace {
bool finite(Vector3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Quaternion normalized(Quaternion q) {
    float length = QuaternionLength(q);
    if (!std::isfinite(length) || length < 1e-8f) throw std::invalid_argument("Invalid rotation quaternion");
    return QuaternionNormalize(q);
}
void eraseId(std::vector<ObjectId>& ids, ObjectId id) { ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end()); }
}
Matrix Transform::matrix() const {
    return MatrixMultiply(MatrixMultiply(MatrixScale(scale.x,scale.y,scale.z),
        QuaternionToMatrix(rotation)),MatrixTranslate(position.x,position.y,position.z));
}
Quaternion rotationDegrees(Vector3 xyz) {
    if (!finite(xyz)) throw std::invalid_argument("Rotation angles must be finite");
    auto x = QuaternionFromAxisAngle({1,0,0},xyz.x*DEG2RAD);
    auto y = QuaternionFromAxisAngle({0,1,0},xyz.y*DEG2RAD);
    auto z = QuaternionFromAxisAngle({0,0,1},xyz.z*DEG2RAD);
    return QuaternionNormalize(QuaternionMultiply(z,QuaternionMultiply(y,x)));
}
std::vector<ObjectId>& Scene::siblings(ObjectId parent) { return parent == root ? roots_ : object(parent).children_; }
ObjectId Scene::createObject(std::string name, ObjectId parent) {
    if (name.empty() || names_.count(name)) throw std::invalid_argument("Object name must be nonempty and unique: " + name);
    if (parent != root) object(parent);
    Object node;
    node.id_ = nextId_++;
    node.parent_ = parent;
    node.name_ = std::move(name);
    auto id = node.id_;
    objects_.emplace(id,std::move(node));
    names_.emplace(objects_.at(id).name_,id);
    siblings(parent).push_back(id);
    return id;
}
ObjectId Scene::addMesh(std::string name, MeshHandle mesh, ObjectId parent) {
    auto id = createObject(std::move(name),parent);
    object(id).mesh = std::move(mesh);
    return id;
}
ObjectId Scene::addLight(std::string name, Light light, ObjectId parent) {
    auto id = createObject(std::move(name),parent);
    object(id).light = std::make_unique<Light>(light);
    return id;
}
void Scene::removeObject(ObjectId id) {
    auto children = object(id).children_;
    for (auto child : children) removeObject(child);
    auto& node = object(id);
    eraseId(siblings(node.parent_),id);
    names_.erase(node.name_);
    objects_.erase(id);
}
void Scene::setParent(ObjectId id, ObjectId parent) {
    auto& node = object(id);
    for (auto a = parent; a != root; a = object(a).parent_)
        if (a == id) throw std::invalid_argument("Reparenting would create a cycle");
    if (node.parent_ == parent) return;
    siblings(parent).push_back(id);
    eraseId(siblings(node.parent_),id);
    node.parent_ = parent;
}
Object& Scene::object(ObjectId id) { return objects_.at(id); }
const Object& Scene::object(ObjectId id) const { return objects_.at(id); }
ObjectId Scene::find(const std::string& name) const {
    auto it = names_.find(name); return it == names_.end() ? root : it->second;
}
bool Scene::contains(ObjectId id) const { return objects_.count(id) != 0; }
void Scene::setPosition(ObjectId id, Vector3 p) {
    if (!finite(p)) throw std::invalid_argument("Position must be finite");
    object(id).transform.position = p;
}
void Scene::translate(ObjectId id, Vector3 d) { setPosition(id,Vector3Add(object(id).transform.position,d)); }
void Scene::setRotation(ObjectId id, Quaternion q) { object(id).transform.rotation = normalized(q); }
void Scene::setRotationDegrees(ObjectId id, Vector3 v) { setRotation(id,rotationDegrees(v)); }
void Scene::rotate(ObjectId id, Quaternion q) { setRotation(id,QuaternionMultiply(object(id).transform.rotation,normalized(q))); }
void Scene::setScale(ObjectId id, Vector3 v) {
    if (!finite(v) || std::abs(v.x)<1e-6f || std::abs(v.y)<1e-6f || std::abs(v.z)<1e-6f)
        throw std::invalid_argument("Scale must be finite and nonzero");
    object(id).transform.scale = v;
}
Matrix Scene::worldMatrix(ObjectId id) const {
    auto& node = object(id); auto local = node.transform.matrix();
    return node.parent_ == root ? local : MatrixMultiply(local,worldMatrix(node.parent_));
}
}

