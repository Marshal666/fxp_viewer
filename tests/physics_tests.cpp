#include <fxp_viewer/physics_interactor.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace fxp_viewer;
using namespace fxp_physics;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool threw=false; try { f(); } catch (const std::exception&) { threw=true; }
    require(threw,"Expected rejection");
}
int main() {
    try {
        Scene scene;
        auto ground=scene.createObject("ground"), ball=scene.createObject("ball");
        scene.setPosition(ground,{0,-0.5f,0}); scene.setScale(ground,{20,1,20});
        scene.setPosition(ball,{0,4,0});
        PhysicsScene setup;
        setup.config.timestep=1_r/60_r;
        setup.config.substeps=2;
        PhysicsBody floor; floor.object=ground; floor.body.type=BodyType::static_body;
        floor.body.pose.position={0,-0.5_r,0}; floor.shape=Shape::box({10,0.5_r,10});
        PhysicsBody sphere; sphere.object=ball; sphere.body.pose.position={0,4,0};
        sphere.shape=Shape::sphere(0.5_r);
        setup.bodies={floor,sphere};
        PhysicsInteractor sim(scene,setup);
        auto initial=sim.world().save();
        const double dt=static_cast<double>(setup.config.timestep.raw_value())/4294967296.0;
        require(!sim.playing() && sim.tick()==0,"Simulation must start paused");
        sim.update(scene,1); require(sim.world().save()==initial,"Paused simulation advanced");
        sim.play(); sim.update(scene,dt/2);
        require(sim.tick()==0,"Partial frame advanced a tick");
        sim.update(scene,dt/2); require(sim.tick()==1,"Accumulator lost fixed tick");
        sim.pause(scene);
        const float y=scene.object(ball).transform.position.y;
        require(y<4,"Physics pose did not reach the viewer");
        auto paused=sim.world().save();
        sim.update(scene,0.25);
        require(sim.world().save()==paused,"Pause did not freeze physics");
        sim.play(); sim.step(scene);
        require(!sim.playing() && sim.tick()==2,"Step must pause and advance exactly once");
        sim.reset(scene);
        require(sim.world().save()==initial && !sim.playing() && sim.tick()==0,"Reset did not restore exact state");
        require(scene.object(ball).transform.position.y==4,"Reset did not restore visual pose");
        rejects([&]{sim.update(scene,-1);});
        rejects([&]{sim.update(scene,std::numeric_limits<double>::quiet_NaN());});
        for (int i=0;i<180;++i) sim.step(scene);
        auto expected=sim.world().save();
        require(std::abs(scene.object(ball).transform.position.y-0.5f)<0.08f,"Ball did not settle on floor");
        sim.reset(scene);
        sim.play();
        for (int i=0;i<360;++i) sim.update(scene,dt/2);
        sim.pause(scene);
        require(sim.tick()==180 && sim.world().save()==expected,"Render cadence changed physics state");
        sim.reset(scene); sim.play(); sim.update(scene,100);
        require(sim.tick()==8,"Catch-up budget was not bounded");
        sim.reset(scene);
        require(sim.world().save()==initial,"Reset retained accumulator or solver state");
        auto parent=scene.createObject("parent");
        scene.setParent(ball,parent);
        rejects([&]{PhysicsInteractor invalid(scene,setup);});
        scene.setParent(ball,root);
        auto invalid=setup; invalid.bodies.push_back(sphere);
        rejects([&]{PhysicsInteractor duplicate(scene,invalid);});
        invalid=setup; invalid.config.substeps=0;
        rejects([&]{PhysicsInteractor invalidBudget(scene,invalid);});
        // A range failure must stop playback and require Reset, not keep ticking.
        invalid=setup; invalid.bodies[1].body.pose.position={8191_r,4,0};
        invalid.bodies[1].body.linear_velocity={128,0,0};
        PhysicsInteractor failing(scene,invalid);
        failing.play();
        rejects([&]{failing.update(scene,dt);});
        require(failing.faulted()&&!failing.playing(),"Physics exception did not halt playback");
        failing.play(); require(!failing.playing(),"Faulted simulation resumed without Reset");
        failing.reset(scene); require(!failing.faulted()&&failing.tick()==0,"Reset did not recover failed simulation");
        std::cout<<"Physics playback, collision, reset and cadence tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"TEST FAILURE: "<<e.what()<<'\n'; return 1; }
}
