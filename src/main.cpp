#include <fxp_viewer/viewer.hpp>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    try {
        fxp_viewer::ViewerOptions options;
        std::filesystem::path scene,screenshot;
        int frames=-1;
        for (int i=1;i<argc;++i) {
            std::string arg=argv[i];
            auto value=[&](){ if (++i>=argc) throw std::invalid_argument("Missing value for "+arg); return std::string(argv[i]); };
            if (arg=="--help") {
                std::cout<<"fxp_viewer [scene.yaml] [--frames N] [--screenshot image.png] [--hidden]\n";
                return 0;
            } else if (arg=="--frames") { frames=std::stoi(value()); if (frames<1) throw std::invalid_argument("--frames must be positive"); }
            else if (arg=="--screenshot") screenshot=std::filesystem::u8path(value());
            else if (arg=="--hidden") options.hidden=true;
            else if (!arg.empty()&&arg[0]=='-') throw std::invalid_argument("Unknown option: "+arg);
            else if (!scene.empty()) throw std::invalid_argument("Specify only one scene");
            else scene=std::filesystem::u8path(arg);
        }
        fxp_viewer::Viewer viewer(options);
        if (!scene.empty()&&!viewer.loadScene(scene)) { std::cerr<<viewer.lastError()<<'\n'; return 1; }
        if (frames<0) viewer.run();
        else for (int i=0;i<frames&&!viewer.shouldClose();++i) viewer.frame(1.0/60.0);
        if (!screenshot.empty()) {
            auto image=LoadImageFromScreen();
            bool ok=ExportImage(image,screenshot.u8string().c_str()); UnloadImage(image);
            if (!ok) throw std::runtime_error("Could not save screenshot");
        }
        return 0;
    } catch (const std::exception& e) { std::cerr<<"fxp_viewer: "<<e.what()<<'\n'; return 1; }
}

