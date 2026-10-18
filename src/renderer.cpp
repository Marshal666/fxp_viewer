#include <fxp_viewer/renderer.hpp>
#include "shaders.hpp"
#include <raymath.h>
#include <rlgl.h>
#include <cgltf.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace fxp_viewer {
namespace {
float linear(unsigned char value) {
    float v = value/255.0f;
    return v <= 0.04045f ? v/12.92f : std::pow((v+0.055f)/1.055f,2.4f);
}
std::string extension(const std::filesystem::path& path) {
    auto s = path.extension().string();
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}
void requireFile(const std::filesystem::path& p) {
    if (!std::filesystem::is_regular_file(p)) throw std::runtime_error("File does not exist: " + p.u8string());
}
bool finite(Vector3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
void generateNormals(Mesh& mesh) {
    if (mesh.normals) {
        bool valid=true;
        for (int i=0;i<mesh.vertexCount;++i) {
            Vector3 n{mesh.normals[i*3],mesh.normals[i*3+1],mesh.normals[i*3+2]};
            if (!finite(n) || Vector3LengthSqr(n)<1e-12f) { valid=false; break; }
        }
        if (valid) return;
        MemFree(mesh.normals);
        mesh.normals=nullptr;
    }
    std::vector<Vector3> normals(mesh.vertexCount);
    for (int t=0;t<mesh.triangleCount;++t) {
        int i[3];
        for (int k=0;k<3;++k) i[k] = mesh.indices ? mesh.indices[t*3+k] : t*3+k;
        Vector3 p[3];
        for (int k=0;k<3;++k) p[k] = {mesh.vertices[i[k]*3],mesh.vertices[i[k]*3+1],mesh.vertices[i[k]*3+2]};
        auto n = Vector3CrossProduct(Vector3Subtract(p[1],p[0]),Vector3Subtract(p[2],p[0]));
        for (int k=0;k<3;++k) normals[i[k]] = Vector3Add(normals[i[k]],n);
    }
    mesh.normals = static_cast<float*>(MemAlloc(mesh.vertexCount*3*sizeof(float)));
    if (!mesh.normals) throw std::bad_alloc();
    for (int i=0;i<mesh.vertexCount;++i) {
        auto n = Vector3Normalize(normals[i]);
        mesh.normals[3*i]=n.x; mesh.normals[3*i+1]=n.y; mesh.normals[3*i+2]=n.z;
    }
    // Re-upload once at load time to supply the previously absent normal VBO.
    if (mesh.vaoId) rlUnloadVertexArray(mesh.vaoId);
    if (mesh.vboId) {
        for (int i=0;i<9;++i) if (mesh.vboId[i]) rlUnloadVertexBuffer(mesh.vboId[i]);
        MemFree(mesh.vboId);
    }
    mesh.vaoId=0; mesh.vboId=nullptr;
    UploadMesh(&mesh,false);
}
}
struct MeshAsset::Impl {
    Model model{};
    ModelAnimation* animations=nullptr;
    int animationCount=0;
    std::vector<AnimationClip> clips;
    std::map<unsigned int,Texture2D> textures;
    BoundingBox bounds{};
    void release() {
        if (animations) UnloadModelAnimations(animations,animationCount);
        animations=nullptr; animationCount=0;
        if (model.meshes) UnloadModel(model);
        model={};
        for (const auto& entry:textures) UnloadTexture(entry.second);
        textures.clear();
    }
    ~Impl() { release(); }
    void collectTextures() {
        for (int i=0;i<model.materialCount;++i)
            for (int j=0;j<12;++j) {
                auto texture=model.materials[i].maps[j].texture;
                if (texture.id && texture.id!=rlGetTextureIdDefault()) textures.emplace(texture.id,texture);
            }
    }
};
MeshAsset::MeshAsset():impl_(std::make_unique<Impl>()) {}
MeshAsset::~MeshAsset()=default;
const std::vector<AnimationClip>& MeshAsset::clips() const { return impl_->clips; }
BoundingBox MeshAsset::bounds() const { return impl_->bounds; }

struct Renderer::Impl {
    Shader shader{};
    int ambient=-1,lightCount=-1,positions=-1,directions=-1,colors=-1,skinned=-1,bones=-1,mirrored=-1;
    std::vector<std::weak_ptr<MeshAsset>> assets;
    struct Item { const Object* object; Matrix world; };
    std::vector<Item> items;
    std::vector<std::pair<ObjectId,Matrix>> stack;
};
Renderer::Renderer():impl_(std::make_unique<Impl>()) {
    if (!IsWindowReady()) throw std::runtime_error("Renderer requires an open graphics context");
    auto& r=*impl_;
    r.shader=LoadShaderFromMemory(lightingVertex,lightingFragment);
    if (!IsShaderValid(r.shader) || r.shader.id==rlGetShaderIdDefault()) {
        UnloadShader(r.shader); r.shader={};
        throw std::runtime_error("Could not compile OpenGL 3.3 lighting shader");
    }
    r.ambient=GetShaderLocation(r.shader,"ambient");
    r.lightCount=GetShaderLocation(r.shader,"lightCount");
    r.positions=GetShaderLocation(r.shader,"lightPosition[0]");
    r.directions=GetShaderLocation(r.shader,"lightDirectionRange[0]");
    r.colors=GetShaderLocation(r.shader,"lightColor[0]");
    r.skinned=GetShaderLocation(r.shader,"skinned");
    r.bones=GetShaderLocation(r.shader,"boneMatrices[0]");
    r.mirrored=GetShaderLocation(r.shader,"mirrored");
}
Renderer::~Renderer() {
    // Release GPU resources while context is still alive, even when callers
    // retained handles past the viewer's lifetime.
    for (auto& weak:impl_->assets) if (auto mesh=weak.lock()) mesh->impl_->release();
    UnloadShader(impl_->shader);
}
MeshHandle Renderer::adoptModel(Model model) {
    MeshHandle asset(new MeshAsset);
    auto& a=*asset->impl_;
    a.model=model;
    a.collectTextures();
    if (!IsModelValid(model)) throw std::runtime_error("Model contains no drawable geometry");
    if (model.skeleton.boneCount>128) throw std::runtime_error("At most 128 skeleton bones are supported");
    for (int i=0;i<model.meshCount;++i) generateNormals(a.model.meshes[i]);
    for (int i=0;i<model.materialCount;++i) a.model.materials[i].shader=impl_->shader;
    a.bounds=GetModelBoundingBox(model);
    for (auto& texture:a.textures) {
        GenTextureMipmaps(&texture.second);
        SetTextureFilter(texture.second,TEXTURE_FILTER_TRILINEAR);
    }
    impl_->assets.erase(std::remove_if(impl_->assets.begin(),impl_->assets.end(),
        [](const auto& w){return w.expired();}),impl_->assets.end());
    impl_->assets.push_back(asset);
    return asset;
}
MeshHandle Renderer::loadModel(const std::filesystem::path& input) {
    auto path=std::filesystem::absolute(input);
    requireFile(path);
    auto ext=extension(path);
    if (ext!=".obj" && ext!=".gltf" && ext!=".glb") throw std::runtime_error("Supported models: OBJ, glTF, GLB");
    if (ext!=".obj") {
        cgltf_options options{};
        cgltf_data* data=nullptr;
        if (cgltf_parse_file(&options,path.u8string().c_str(),&data)!=cgltf_result_success)
            throw std::runtime_error("Invalid glTF: "+path.u8string());
        std::unique_ptr<cgltf_data,decltype(&cgltf_free)> guard(data,cgltf_free);
        if (cgltf_load_buffers(&options,data,path.u8string().c_str())!=cgltf_result_success ||
            cgltf_validate(data)!=cgltf_result_success || data->meshes_count==0)
            throw std::runtime_error("Invalid glTF geometry or missing buffer: "+path.u8string());
        if (data->extensions_required_count>0) throw std::runtime_error("Required glTF extensions are not supported; export core glTF 2.0");
        if (data->skins_count>1) throw std::runtime_error("Only one skeleton per glTF file is supported");
        for (cgltf_size i=0;i<data->meshes_count;++i)
            for (cgltf_size j=0;j<data->meshes[i].primitives_count;++j) {
                const auto& p=data->meshes[i].primitives[j];
                if (p.has_draco_mesh_compression || p.targets_count)
                    throw std::runtime_error("Draco compression and morph targets are not supported");
                if (p.type!=cgltf_primitive_type_triangles) throw std::runtime_error("glTF meshes must use triangles");
            }
    } else {
        std::ifstream file(path);
        bool vertex=false,face=false;
        for (std::string line;std::getline(file,line);) {
            vertex |= line.rfind("v ",0)==0;
            face |= line.rfind("f ",0)==0;
        }
        if (!vertex || !face) throw std::runtime_error("OBJ must contain vertices and faces");
    }
    auto asset=adoptModel(LoadModel(path.u8string().c_str()));
    auto& a=*asset->impl_;
    if (ext!=".obj" && a.model.skeleton.boneCount>0) {
        a.animations=LoadModelAnimations(path.u8string().c_str(),&a.animationCount);
        for (int i=0;i<a.animationCount;++i) {
            auto& anim=a.animations[i];
            if (anim.keyframeCount<=0 || anim.boneCount!=a.model.skeleton.boneCount)
                throw std::runtime_error("Invalid animation skeleton");
            std::string name=anim.name;
            if (name.empty()) name="clip_"+std::to_string(i);
            for (const auto& clip:a.clips) if (clip.name==name) throw std::runtime_error("Duplicate/truncated animation name: "+name);
            a.clips.push_back({name,std::max(0,anim.keyframeCount-1)/60.0});
        }
    }
    return asset;
}
MeshHandle Renderer::cube() { return adoptModel(LoadModelFromMesh(GenMeshCube(1,1,1))); }
MeshHandle Renderer::sphere() { return adoptModel(LoadModelFromMesh(GenMeshSphere(0.5f,24,32))); }
MeshHandle Renderer::plane() { return adoptModel(LoadModelFromMesh(GenMeshPlane(1,1,1,1))); }
MeshHandle Renderer::createMesh(const MeshData& data) {
    const auto count=data.indices.empty()?data.vertices.size():data.indices.size();
    if (count==0 || count%3 || count>10000000) throw std::invalid_argument("Mesh requires a nonempty triangle list (maximum 10M vertices)");
    auto vertices=data.vertices;
    bool generate=true;
    for (const auto& v:vertices) {
        if (!finite(v.position)||!finite(v.normal)||!std::isfinite(v.uv.x)||!std::isfinite(v.uv.y))
            throw std::invalid_argument("Mesh vertices must be finite");
        if (Vector3LengthSqr(v.normal)>1e-12f) generate=false;
    }
    auto index=[&](std::size_t i)->std::size_t {
        auto n=data.indices.empty()?i:data.indices[i];
        if (n>=vertices.size()) throw std::invalid_argument("Mesh index is out of bounds");
        return n;
    };
    for (std::size_t i=0;i<count;i+=3) {
        auto a=index(i), b=index(i+1), c=index(i+2);
        if (generate) {
            auto n=Vector3CrossProduct(Vector3Subtract(vertices[b].position,vertices[a].position),
                Vector3Subtract(vertices[c].position,vertices[a].position));
            for (auto k:{a,b,c}) vertices[k].normal=Vector3Add(vertices[k].normal,n);
        }
    }
    Mesh mesh{};
    mesh.vertexCount=static_cast<int>(count); mesh.triangleCount=mesh.vertexCount/3;
    mesh.vertices=static_cast<float*>(MemAlloc(static_cast<unsigned int>(count*3*sizeof(float))));
    mesh.normals=static_cast<float*>(MemAlloc(static_cast<unsigned int>(count*3*sizeof(float))));
    mesh.texcoords=static_cast<float*>(MemAlloc(static_cast<unsigned int>(count*2*sizeof(float))));
    if (!mesh.vertices||!mesh.normals||!mesh.texcoords) { UnloadMesh(mesh); throw std::bad_alloc(); }
    for (std::size_t i=0;i<count;++i) {
        auto& v=vertices[index(i)]; auto n=Vector3Normalize(v.normal);
        mesh.vertices[3*i]=v.position.x; mesh.vertices[3*i+1]=v.position.y; mesh.vertices[3*i+2]=v.position.z;
        mesh.normals[3*i]=n.x; mesh.normals[3*i+1]=n.y; mesh.normals[3*i+2]=n.z;
        mesh.texcoords[2*i]=v.uv.x; mesh.texcoords[2*i+1]=v.uv.y;
    }
    UploadMesh(&mesh,false);
    return adoptModel(LoadModelFromMesh(mesh));
}
void Renderer::setColorTexture(const MeshHandle& mesh,const std::filesystem::path& path) {
    requireFile(path);
    if (!mesh || !mesh->impl_->model.meshes) throw std::invalid_argument("Invalid mesh handle");
    auto texture=LoadTexture(path.u8string().c_str());
    if (!IsTextureValid(texture)) throw std::runtime_error("Cannot load texture: "+path.u8string());
    GenTextureMipmaps(&texture); SetTextureFilter(texture,TEXTURE_FILTER_TRILINEAR);
    auto& a=*mesh->impl_;
    a.textures.emplace(texture.id,texture);
    for (int i=0;i<a.model.materialCount;++i) a.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture=texture;
    // Free textures no longer referenced by any material map.
    std::set<unsigned int> used;
    for (int i=0;i<a.model.materialCount;++i) for (int j=0;j<12;++j) used.insert(a.model.materials[i].maps[j].texture.id);
    for (auto it=a.textures.begin();it!=a.textures.end();) {
        if (!used.count(it->first)) { UnloadTexture(it->second); it=a.textures.erase(it); } else ++it;
    }
}
void Renderer::updateAnimations(Scene& scene,double dt) {
    if (!std::isfinite(dt)||dt<0) throw std::invalid_argument("Frame delta must be finite and nonnegative");
    for (auto& entry:scene.objects()) {
        auto& a=scene.object(entry.first).animation;
        if (a.playing && !a.clip.empty()) a.time+=dt*a.speed;
    }
}
void Renderer::draw(const Scene& scene,const Camera3D& camera) {
    auto& r=*impl_;
    r.items.clear(); r.stack.clear();
    for (auto id:scene.roots()) r.stack.push_back({id,MatrixIdentity()});
    while (!r.stack.empty()) {
        auto entry=r.stack.back(); r.stack.pop_back();
        auto& obj=scene.object(entry.first);
        if (!obj.visible) continue;
        auto world=MatrixMultiply(obj.transform.matrix(),entry.second);
        r.items.push_back({&obj,world});
        for (auto id:obj.children()) r.stack.push_back({id,world});
    }
    std::array<Vector4,maxLights> positions{},directions{},colors{};
    int count=0;
    for (auto& item:r.items) {
        auto& ptr=item.object->light;
        if (!ptr || !ptr->enabled || ptr->intensity<=0 || count==maxLights) continue;
        const auto& l=*ptr;
        if (l.type==LightType::Point && l.range<=0) continue;
        auto p=Vector3Transform({0,0,0},item.world);
        Matrix rotation=item.world; rotation.m12=rotation.m13=rotation.m14=0;
        auto direction=Vector3Normalize(Vector3Transform(l.direction,rotation));
        positions[count]={p.x,p.y,p.z,l.type==LightType::Point?1.0f:0.0f};
        directions[count]={direction.x,direction.y,direction.z,std::max(l.range,0.001f)};
        colors[count]={linear(l.color.r)*l.intensity,linear(l.color.g)*l.intensity,linear(l.color.b)*l.intensity,1};
        ++count;
    }
    Vector3 ambient{linear(scene.ambient.r)*scene.ambientIntensity,linear(scene.ambient.g)*scene.ambientIntensity,linear(scene.ambient.b)*scene.ambientIntensity};
    SetShaderValue(r.shader,r.ambient,&ambient,SHADER_UNIFORM_VEC3);
    SetShaderValue(r.shader,r.lightCount,&count,SHADER_UNIFORM_INT);
    if (count) {
        SetShaderValueV(r.shader,r.positions,positions.data(),SHADER_UNIFORM_VEC4,count);
        SetShaderValueV(r.shader,r.directions,directions.data(),SHADER_UNIFORM_VEC4,count);
        SetShaderValueV(r.shader,r.colors,colors.data(),SHADER_UNIFORM_VEC4,count);
    }
    BeginMode3D(camera);
    // Two-sided geometry also handles mirrored transforms without winding bugs.
    rlDisableBackfaceCulling();
    for (auto& item:r.items) {
        if (!item.object->mesh) continue;
        auto& asset=*item.object->mesh->impl_;
        auto& model=asset.model;
        if (!model.meshes) continue;
        const auto& state=item.object->animation;
        bool animated=false;
        for (int i=0;i<asset.animationCount;++i) if (asset.clips[i].name==state.clip) {
            double duration=asset.clips[i].duration, time=state.time;
            if (state.loop && duration>0) { time=std::fmod(time,duration); if (time<0) time+=duration; }
            else time=std::clamp(time,0.0,duration);
            UpdateModelAnimation(model,asset.animations[i],static_cast<float>(time*60));
            animated=true; break;
        }
        if (!animated && model.boneMatrices)
            for (unsigned int i=0;i<model.skeleton.boneCount;++i) model.boneMatrices[i]=MatrixIdentity();
        if (model.boneMatrices) {
            rlEnableShader(r.shader.id);
            rlSetUniformMatrices(r.bones,model.boneMatrices,model.skeleton.boneCount);
        }
        auto world=MatrixMultiply(model.transform,item.world);
        int mirrored=MatrixDeterminant(world)<0 ? 1:0;
        SetShaderValue(r.shader,r.mirrored,&mirrored,SHADER_UNIFORM_INT);
        for (int i=0;i<model.meshCount;++i) {
            int skinned=model.boneMatrices && model.meshes[i].boneIndices ? 1:0;
            SetShaderValue(r.shader,r.skinned,&skinned,SHADER_UNIFORM_INT);
            auto material=model.materials[model.meshMaterial[i]];
            auto& color=material.maps[MATERIAL_MAP_DIFFUSE].color;
            auto saved=color; auto tint=item.object->tint;
            color={static_cast<unsigned char>(color.r*tint.r/255),static_cast<unsigned char>(color.g*tint.g/255),
                static_cast<unsigned char>(color.b*tint.b/255),static_cast<unsigned char>(color.a*tint.a/255)};
            DrawMesh(model.meshes[i],material,world);
            color=saved;
        }
    }
    rlEnableBackfaceCulling();
    EndMode3D();
}
}





