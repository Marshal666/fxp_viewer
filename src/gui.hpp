#pragma once
#include <fxp_viewer/viewer.hpp>
#include <array>
namespace fxp_viewer {
class Gui {
public:
    Gui();
    bool blocksCamera() const;
    bool modalActive() const { return menu_!=0 || picker_ || !error_.empty(); }
    void openPicker(const std::filesystem::path& directory);
    void draw(Viewer& viewer);
    void showError(std::string message);
private:
    int menu_=0;
    bool picker_=false,editing_=false;
    std::array<char,4096> path_{};
    std::filesystem::path directory_;
    std::vector<std::filesystem::directory_entry> entries_;
    std::string directoryError_,error_;
    int scroll_=0;
    void browse(const std::filesystem::path& path);
};
}


