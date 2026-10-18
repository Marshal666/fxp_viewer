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

void readObjects(const YAML::Node& list,Scene& scene,const std::map<std::string,MeshHandle>& meshes,ObjectId parent,int depth,int& lights) {
    if (!list.IsSequence()) throw std::runtime_error("objects/children must be a sequence");
    if (depth>64) throw std::runtime_error("Scene hierarchy exceeds 64 levels");
    for (auto node:list) {
        std::string id="<unnamed>";
        try {
            fields(node,{"id","transform","mesh","color","visible","animation","light","children"});
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
            if (node["children"]) readObjects(node["children"],scene,meshes,handle,depth+1,lights);
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
        fields(doc,{"version","camera","ambient","background","meshes","objects"});
        if (!doc["version"]||doc["version"].as<int>()!=1) throw std::runtime_error("Scene version must be 1");
        SceneDocument result;
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
        if (doc["objects"]) { int lights=0; readObjects(doc["objects"],result.scene,meshes,root,0,lights); }
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.u8string()+"\n"+error.what());
    }
}
}

