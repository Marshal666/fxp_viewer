#include <fxp_viewer/physics_interactor.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <set>
#include <stdexcept>
namespace fxp_viewer {
namespace {
using namespace fxp_physics;
float renderScalar(Real value) {
    auto bits=fxp::to_ieee754_binary32_bits(value);
    float result;
    static_assert(sizeof(result)==sizeof(bits), "Requires IEEE binary32");
    std::memcpy(&result,&bits,sizeof(result));
    return result;
}
double seconds(Real value) {
    auto bits=fxp::to_ieee754_binary64_bits(value);
    double result;
    static_assert(sizeof(result)==sizeof(bits), "Requires IEEE binary64");
    std::memcpy(&result,&bits,sizeof(result));
    return result;
}
Real fraction(double value) {
    std::uint64_t bits;
    std::memcpy(&bits,&value,sizeof(bits));
    return fxp::from_ieee754_binary64_bits(bits);
}
}
struct PhysicsInteractor::Impl {
    struct Binding { ObjectId object; fxp_physics::BodyId body; Transform initial; };
    fxp_physics::World world;
    fxp_physics::Bytes initial;
    std::vector<Binding> bindings;
    double accumulator=0;
    bool playing=false, faulted=false, interpolate=false;
    explicit Impl(const PhysicsScene& setup):world(setup.config,setup.seed) {}
    void sync(Scene& scene,fxp_physics::Real alpha) {
        for (auto& binding:bindings) {
            if (!scene.contains(binding.object) || scene.object(binding.object).parent()!=root)
                throw std::logic_error("A physics-bound object was removed or reparented; reload the scene");
            auto pose=world.interpolated_pose(binding.body,alpha);
            auto q=pose.rotation.get_internal_vector();
            scene.setPosition(binding.object,{renderScalar(pose.position.x),renderScalar(pose.position.y),renderScalar(pose.position.z)});
            scene.setRotation(binding.object,{renderScalar(q.x),renderScalar(q.y),renderScalar(q.z),renderScalar(q.w)});
        }
    }
    void advance() {
        if (faulted) throw std::logic_error("Reset the simulation after a physics error");
        try { world.step(); }
        catch (...) { playing=false; faulted=true; accumulator=0; throw; }
    }
};
PhysicsInteractor::PhysicsInteractor(const Scene& scene,const PhysicsScene& setup):impl_(std::make_unique<Impl>(setup)) {
    auto& p=*impl_;
    std::set<ObjectId> bound;
    for (const auto& entry:setup.bodies) {
        const auto& object=scene.object(entry.object);
        try {
            if (object.parent()!=root) throw std::invalid_argument("Physics bodies must be root objects");
            if (!bound.insert(entry.object).second) throw std::invalid_argument("Object has multiple physics bodies");
            auto body=entry.body; body.user_data=entry.object;
            auto id=p.world.create_body(body);
            fxp_physics::ColliderDesc collider;
            collider.body=id; collider.shape=entry.shape; collider.material=entry.material;
            p.world.create_collider(collider);
            p.bindings.push_back({entry.object,id,object.transform});
        } catch (const std::exception& e) {
            throw std::runtime_error("Physics object '"+object.name()+"': "+e.what());
        }
    }
    p.initial=p.world.save();
}
PhysicsInteractor::~PhysicsInteractor()=default;
void PhysicsInteractor::play() {
    if (!impl_->faulted && !impl_->playing) {
        impl_->playing=true;
        impl_->interpolate=false; // Hold the paused pose until the next tick.
    }
}
void PhysicsInteractor::pause(Scene& scene) {
    impl_->playing=false; impl_->accumulator=0;
    if (!impl_->faulted) impl_->sync(scene,1);
}
void PhysicsInteractor::step(Scene& scene) {
    impl_->playing=false; impl_->accumulator=0;
    impl_->advance(); impl_->sync(scene,1);
}
void PhysicsInteractor::reset(Scene& scene) {
    auto& p=*impl_;
    p.world.load(p.initial);
    p.playing=false; p.faulted=false; p.accumulator=0;
    for (const auto& binding:p.bindings) scene.object(binding.object).transform=binding.initial;
}
void PhysicsInteractor::update(Scene& scene,double dt) {
    if (!std::isfinite(dt)||dt<0) throw std::invalid_argument("Frame delta must be finite and nonnegative");
    auto& p=*impl_;
    if (!p.playing) return;
    const double timestep=seconds(p.world.config().timestep);
    // Bound catch-up work after stalls. Drop excess wall time, never vary a tick.
    p.accumulator+=std::min(dt,0.25);
    int steps=0;
    const auto started=std::chrono::steady_clock::now();
    while (p.accumulator>=timestep && steps<8) {
        p.advance(); p.accumulator-=timestep; ++steps; p.interpolate=true;
        // Always finish an owed tick, then yield before catch-up stalls the UI.
        if (std::chrono::steady_clock::now()-started>=std::chrono::milliseconds(8)) break;
    }
    if (p.accumulator>=timestep) p.accumulator=std::fmod(p.accumulator,timestep);
    try { p.sync(scene,p.interpolate?fraction(p.accumulator/timestep):fxp_physics::Real(1)); }
    catch (...) { p.playing=false; p.faulted=true; p.accumulator=0; throw; }
}
bool PhysicsInteractor::playing() const { return impl_->playing; }
bool PhysicsInteractor::faulted() const { return impl_->faulted; }
std::uint64_t PhysicsInteractor::tick() const { return impl_->world.tick(); }
double PhysicsInteractor::time() const { return static_cast<double>(tick())*seconds(impl_->world.config().timestep); }
const fxp_physics::World& PhysicsInteractor::world() const { return impl_->world; }
fxp_physics::BodyId PhysicsInteractor::bodyId(ObjectId object) const {
    for (const auto& b:impl_->bindings) if (b.object==object) return b.body;
    return 0;
}
}
