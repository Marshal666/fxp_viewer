#include <fxp_viewer/viewer.hpp>
#include <yaml-cpp/yaml.h>
#include <raymath.h>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
namespace fxp_viewer {
namespace {
void fields(const YAML::Node& node,std::initializer_list<const char*> allowed) {
    if (!node.IsMap()) throw std::runtime_error("Expected a mapping");
    std::set<std::string> names;
    for (auto entry:node) {
        auto name=entry.first.as<std::string>();
        if (!names.insert(name).second) throw std::runtime_error("Duplicate field: "+name);
        bool ok=false; for (auto key:allowed) if (name==key) ok=true;
        if (!ok) throw std::runtime_error("Unknown field: "+name);
    }
}
float number(const YAML::Node& node) {
    float v=node.as<float>();
    if (!std::isfinite(v)) throw std::runtime_error("Numbers must be finite");
    return v;
}
Vector3 vec3(const YAML::Node& node) {
    if (!node.IsSequence()||node.size()!=3) throw std::runtime_error("Expected [x, y, z]");
    return {number(node[0]),number(node[1]),number(node[2])};
}
Color color(const YAML::Node& node) {
    if (!node.IsSequence()||(node.size()!=3&&node.size()!=4)) throw std::runtime_error("Expected [r, g, b] or [r, g, b, a], using 0..255");
    Color c{255,255,255,255}; unsigned char* values=&c.r;
    for (std::size_t i=0;i<node.size();++i) {
        int n=node[i].as<int>();
        if (n<0||n>255) throw std::runtime_error("Color channels must be in 0..255");
        values[i]=static_cast<unsigned char>(n);
    }
    return c;
}
void positive(float n,const char* name) { if (n<=0) throw std::runtime_error(std::string(name)+" must be positive"); }
void nonnegative(float n,const char* name) { if (n<0) throw std::runtime_error(std::string(name)+" cannot be negative"); }

fxp_physics::Real fixedNumber(const YAML::Node& node) {
    if (!node.IsScalar()) throw std::runtime_error("Expected a fixed-point decimal");
    auto value=fxp_physics::Real::from_string(node.Scalar());
    if (value==fxp_physics::Real::min() || value==fxp_physics::Real::max())
        throw std::runtime_error("Physics number exceeds fixed-point range");
    return value;
}
fxp_physics::Vec3 fixedVector(const YAML::Node& node) {
    if (!node.IsSequence() || node.size()!=3) throw std::runtime_error("Expected [x, y, z]");
    return {fixedNumber(node[0]),fixedNumber(node[1]),fixedNumber(node[2])};
}
PhysicsBody readPhysicsBody(const YAML::Node& node,ObjectId id) {
    using namespace fxp_physics;
    auto p=node["physics"];
    fields(p,{"type","collider","mass","linear_velocity","angular_velocity","linear_damping",
              "angular_damping","gravity_scale","allow_sleep","material"});
    PhysicsBody result; result.object=id;
    auto& body=result.body;
    auto type=p["type"]?p["type"].as<std::string>():std::string("dynamic");
    if (type=="static") body.type=BodyType::static_body;
    else if (type=="kinematic") body.type=BodyType::kinematic;
    else if (type!="dynamic") throw std::runtime_error("Physics type must be static, kinematic or dynamic");
    if (auto t=node["transform"]) {
        // Import the original decimal text, not the viewer's float transforms.
        if (t["position"]) body.pose.position=fixedVector(t["position"]);
        if (t["rotation"]) {
            auto xyz=fixedVector(t["rotation"]);
            if (fxp::abs(xyz.x)>360000 || fxp::abs(xyz.y)>360000 || fxp::abs(xyz.z)>360000)
                throw std::runtime_error("Physics Euler angles must be within +/-360000 degrees");
            xyz=xyz*(fxp::pi_v<Real>/180);
            body.pose.rotation=Quat(xyz.z,{0,0,1})*(Quat(xyz.y,{0,1,0})*Quat(xyz.x,{1,0,0}));
        }
    }
    if (p["mass"]) body.mass=fixedNumber(p["mass"]);
    if (p["linear_velocity"]) body.linear_velocity=fixedVector(p["linear_velocity"]);
    if (p["angular_velocity"]) body.angular_velocity=fixedVector(p["angular_velocity"]);
    if (p["linear_damping"]) body.linear_damping=fixedNumber(p["linear_damping"]);
    if (p["angular_damping"]) body.angular_damping=fixedNumber(p["angular_damping"]);
    if (p["gravity_scale"]) body.gravity_scale=fixedNumber(p["gravity_scale"]);
    if (p["allow_sleep"]) body.allow_sleep=p["allow_sleep"].as<bool>();
    if (auto m=p["material"]) {
        fields(m,{"static_friction","dynamic_friction","restitution"});
        if (m["static_friction"]) result.material.static_friction=fixedNumber(m["static_friction"]);
        if (m["dynamic_friction"]) result.material.dynamic_friction=fixedNumber(m["dynamic_friction"]);
        if (m["restitution"]) result.material.restitution=fixedNumber(m["restitution"]);
    }
    auto collider=p["collider"];
    if (!collider || !collider.IsMap()) throw std::runtime_error("Physics body needs a collider mapping");
    auto shape=collider["shape"].as<std::string>();
    if (shape=="box") {
        fields(collider,{"shape","half_extents"});
        result.shape=Shape::box(fixedVector(collider["half_extents"]));
    } else if (shape=="sphere") {
        fields(collider,{"shape","radius"});
        result.shape=Shape::sphere(fixedNumber(collider["radius"]));
    } else if (shape=="convex") {
        fields(collider,{"shape","vertices","faces"});
        const auto vertices=collider["vertices"],faces=collider["faces"];
        if (!vertices.IsSequence() || vertices.size()<4 || vertices.size()>128)
            throw std::runtime_error("Convex vertices must be a sequence of 4..128 positions");
        if (!faces.IsSequence() || faces.size()<4 || faces.size()>256)
            throw std::runtime_error("Convex faces must be a sequence of 4..256 index lists");
        std::vector<Vec3> points;
        points.reserve(vertices.size());
        for (auto vertex:vertices) points.push_back(fixedVector(vertex));
        std::vector<std::vector<std::uint32_t>> polygons;
        polygons.reserve(faces.size());
        for (auto face:faces) {
            if (!face.IsSequence() || face.size()<3 || face.size()>128)
                throw std::runtime_error("Convex face must contain 3..128 vertex indices");
            std::vector<std::uint32_t> indices;
            indices.reserve(face.size());
            for (auto value:face) {
                if (!value.IsScalar())
                    throw std::runtime_error("Convex face index must be an integer");
                const auto index=value.as<std::int64_t>();
                if (index<0 || static_cast<std::uint64_t>(index)>=points.size())
                    throw std::runtime_error("Convex face index is outside the vertex list");
                indices.push_back(static_cast<std::uint32_t>(index));
            }
            polygons.push_back(std::move(indices));
        }
        result.shape=Shape::convex(std::move(points),std::move(polygons));
    } else throw std::runtime_error("Collider shape must be box, sphere or convex");
    return result;
}

void readObjects(const YAML::Node& list,Scene& scene,const std::map<std::string,MeshHandle>& meshes,ObjectId parent,int depth,int& lights,PhysicsScene* physics) {
    if (!list.IsSequence()) throw std::runtime_error("objects/children must be a sequence");
    if (depth>64) throw std::runtime_error("Scene hierarchy exceeds 64 levels");
    for (auto node:list) {
        std::string id="<unnamed>";
        try {
            fields(node,{"id","transform","mesh","color","visible","animation","light","children","physics"});
            id=node["id"].as<std::string>();
            if (scene.objects().size()>=100000) throw std::runtime_error("Too many scene objects");
            auto handle=scene.createObject(id,parent);
            auto& object=scene.object(handle);
            if (auto t=node["transform"]) {
                fields(t,{"position","rotation","scale"});
                if (t["position"]) scene.setPosition(handle,vec3(t["position"]));
                if (t["rotation"]) scene.setRotationDegrees(handle,vec3(t["rotation"]));
                if (t["scale"]) scene.setScale(handle,vec3(t["scale"]));
            }
            if (node["mesh"]) {
                auto key=node["mesh"].as<std::string>();
                auto it=meshes.find(key);
                if (it==meshes.end()) throw std::runtime_error("Unknown mesh: "+key);
                object.mesh=it->second;
            }
            if (node["color"]) object.tint=color(node["color"]);
            if (node["visible"]) object.visible=node["visible"].as<bool>();
            if (auto a=node["animation"]) {
                fields(a,{"clip","time","speed","playing","loop"});
                if (!object.mesh||object.mesh->clips().empty()) throw std::runtime_error("Animation needs a skeletal mesh with clips");
                object.animation.clip=a["clip"]?a["clip"].as<std::string>():object.mesh->clips().front().name;
                bool found=false;
                for (auto& clip:object.mesh->clips()) if (clip.name==object.animation.clip) found=true;
                if (!found) throw std::runtime_error("Unknown animation clip: "+object.animation.clip);
                if (a["time"]) object.animation.time=number(a["time"]);
                if (a["speed"]) object.animation.speed=number(a["speed"]);
                if (a["playing"]) object.animation.playing=a["playing"].as<bool>();
                if (a["loop"]) object.animation.loop=a["loop"].as<bool>();
            }
            if (auto l=node["light"]) {
                fields(l,{"type","color","intensity","range","direction","enabled"});
                if (++lights>Renderer::maxLights) throw std::runtime_error("A scene supports at most 8 lights");
                object.light=std::make_unique<Light>();
                auto& light=*object.light;
                auto type=l["type"].as<std::string>();
                if (type=="point") light.type=LightType::Point;
                else if (type!="directional") throw std::runtime_error("Light type must be point or directional");
                if (l["color"]) light.color=color(l["color"]);
                if (l["intensity"]) { light.intensity=number(l["intensity"]); nonnegative(light.intensity,"Light intensity"); }
                if (l["range"]) { light.range=number(l["range"]); positive(light.range,"Light range"); }
                if (l["direction"]) {
                    light.direction=vec3(l["direction"]);
                    if (Vector3LengthSqr(light.direction)<1e-10f) throw std::runtime_error("Light direction cannot be zero");
                }
                if (l["enabled"]) light.enabled=l["enabled"].as<bool>();
            }
            if (node["physics"]) {
                if (!physics) throw std::runtime_error("Object physics needs a scene-level physics mapping");
                if (parent!=root) throw std::runtime_error("Physics bodies must be root objects; visual children are allowed");
                physics->bodies.push_back(readPhysicsBody(node,handle));
            }
            if (node["children"]) readObjects(node["children"],scene,meshes,handle,depth+1,lights,physics);
        } catch (const std::exception& error) {
            throw std::runtime_error("Object '"+id+"' (line "+std::to_string(node.Mark().line+1)+"): "+error.what());
        }
    }
}
}
SceneDocument loadSceneFile(const std::filesystem::path& input,Renderer& renderer) {
    auto path=std::filesystem::absolute(input);
    try {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("Cannot open scene");
        auto doc=YAML::Load(file);
        fields(doc,{"version","camera","ambient","background","meshes","objects","physics"});
        if (!doc["version"]||doc["version"].as<int>()!=1) throw std::runtime_error("Scene version must be 1");
        SceneDocument result;
        if (auto p=doc["physics"]) {
            fields(p,{"tick_hz","substeps","velocity_iterations","gravity","seed"});
            result.physics.emplace();
            auto& setup=*result.physics;
            if (p["tick_hz"]) {
                auto hz=p["tick_hz"].as<int>();
                if (hz<10 || hz>1000) throw std::runtime_error("Physics tick_hz must be 10..1000");
                setup.config.timestep=fxp_physics::Real(1)/fxp_physics::Real(hz);
            }
            if (p["substeps"]) setup.config.substeps=p["substeps"].as<std::uint32_t>();
            if (p["velocity_iterations"]) setup.config.velocity_iterations=p["velocity_iterations"].as<std::uint32_t>();
            if (p["gravity"]) setup.config.gravity=fixedVector(p["gravity"]);
            if (p["seed"]) setup.seed=p["seed"].as<std::uint64_t>();
        }
        if (auto b=doc["background"]) {
            if (!b.IsMap()) throw std::runtime_error("background must be a mapping");
            auto type=b["type"]?b["type"].as<std::string>():std::string("color");
            auto rgb=[](const YAML::Node& value) {
                if (!value.IsSequence() || value.size()!=3)
                    throw std::runtime_error("Background colors must be [r, g, b], using 0..255");
                return color(value);
            };
            auto& background=result.scene.background;
            if (type=="color") {
                fields(b,{"type","color"});
                if (b["color"]) background.color=rgb(b["color"]);
            } else if (type=="skybox") {
                fields(b,{"type","sky_color","horizon_color","ground_color"});
                background.type=BackgroundType::Skybox;
                if (b["sky_color"]) background.skyColor=rgb(b["sky_color"]);
                if (b["horizon_color"]) background.horizonColor=rgb(b["horizon_color"]);
                if (b["ground_color"]) background.groundColor=rgb(b["ground_color"]);
            } else throw std::runtime_error("Background type must be color or skybox");
        }
        if (auto c=doc["camera"]) {
            fields(c,{"position","target","fov","speed"});
            if (c["position"]) result.camera.position=vec3(c["position"]);
            if (c["target"]) result.camera.target=vec3(c["target"]);
            if (c["fov"]) result.camera.fov=number(c["fov"]);
            if (c["speed"]) result.camera.speed=number(c["speed"]);
            if (result.camera.fov<5||result.camera.fov>150) throw std::runtime_error("Camera fov must be 5..150 degrees");
            positive(result.camera.speed,"Camera speed");
            auto delta=Vector3Subtract(result.camera.target,result.camera.position);
            if (Vector3LengthSqr(delta)<1e-8f || (delta.x*delta.x+delta.z*delta.z)<1e-8f)
                throw std::runtime_error("Camera must have a nonvertical viewing direction");
        }
        if (auto a=doc["ambient"]) {
            fields(a,{"color","intensity"});
            if (a["color"]) result.scene.ambient=color(a["color"]);
            if (a["intensity"]) { result.scene.ambientIntensity=number(a["intensity"]); nonnegative(result.scene.ambientIntensity,"Ambient intensity"); }
        }
        std::map<std::string,MeshHandle> meshes;
        if (auto m=doc["meshes"]) {
            if (!m.IsMap()) throw std::runtime_error("meshes must be a mapping");
            for (auto entry:m) {
                auto name=entry.first.as<std::string>(); auto value=entry.second;
                if (meshes.count(name)) throw std::runtime_error("Duplicate mesh: "+name);
                fields(value,{"file","primitive","texture"});
                if (static_cast<bool>(value["file"])==static_cast<bool>(value["primitive"]))
                    throw std::runtime_error("Mesh '"+name+"' needs exactly one of file or primitive");
                MeshHandle mesh;
                if (value["file"]) mesh=renderer.loadModel(path.parent_path()/std::filesystem::u8path(value["file"].as<std::string>()));
                else {
                    auto primitive=value["primitive"].as<std::string>();
                    if (primitive=="cube") mesh=renderer.cube();
                    else if (primitive=="sphere") mesh=renderer.sphere();
                    else if (primitive=="plane") mesh=renderer.plane();
                    else throw std::runtime_error("Unknown primitive: "+primitive);
                }
                if (value["texture"]) renderer.setColorTexture(mesh,path.parent_path()/std::filesystem::u8path(value["texture"].as<std::string>()));
                meshes.emplace(name,std::move(mesh));
            }
        }
        if (doc["objects"]) { int lights=0; readObjects(doc["objects"],result.scene,meshes,root,0,lights,result.physics?&*result.physics:nullptr); }
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.u8string()+"\n"+error.what());
    }
}
}

