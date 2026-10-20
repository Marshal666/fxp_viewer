#pragma once
#include "scene.hpp"
#include <fxp_physics/physics.hpp>
namespace fxp_viewer {
// One root scene object per body; collider dimensions are explicit metres.
// Object scale affects only rendering. Visual children follow their parent.
struct PhysicsBody {
    ObjectId object = root;
    fxp_physics::BodyDesc body;
    fxp_physics::ShapePtr shape;
    fxp_physics::Material material;
};
struct PhysicsScene {
    fxp_physics::Config config;
    std::uint64_t seed = 0;
    std::vector<PhysicsBody> bodies;
};
// Owns the simulation and its initial snapshot. Never reads rendered poses back.
class PhysicsInteractor {
public:
    PhysicsInteractor(const Scene& scene, const PhysicsScene& setup);
    ~PhysicsInteractor();
    PhysicsInteractor(const PhysicsInteractor&) = delete;
    PhysicsInteractor& operator=(const PhysicsInteractor&) = delete;
    void play();                        // Resume; does not restart.
    void pause(Scene& scene);           // Show latest completed tick.
    void step(Scene& scene);            // Pause, then advance exactly one tick.
    void reset(Scene& scene);           // Restore tick zero, then pause.
    void update(Scene& scene, double frameSeconds);
    bool playing() const;
    bool faulted() const;               // Reset is required after a step error.
    std::uint64_t tick() const;
    double time() const;
    const fxp_physics::World& world() const;
    fxp_physics::BodyId bodyId(ObjectId object) const; // 0 when unbound.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
