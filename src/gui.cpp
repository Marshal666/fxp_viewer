#include "gui.hpp"
#define RAYGUI_IMPLEMENTATION
#include <raygui.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>
namespace fxp_viewer {
namespace {
constexpr Color panel{23,26,31,255}, line{48,54,63,255}, text{206,214,224,255};
Rectangle playbackPanel() {
    float width=std::min(368.0f,static_cast<float>(GetScreenWidth())-24);
    return {(GetScreenWidth()-width)/2,static_cast<float>(GetScreenHeight())-116,width,78};
}
std::string shorten(std::string s,int width,int size=16) {
    while (!s.empty() && MeasureText(s.c_str(),size)>width) s.pop_back();
    return s;
}
}
Gui::Gui() {
    GuiSetStyle(DEFAULT,TEXT_SIZE,16);
    GuiSetStyle(DEFAULT,BORDER_COLOR_NORMAL,ColorToInt(line));
    GuiSetStyle(DEFAULT,BASE_COLOR_NORMAL,ColorToInt(panel));
    GuiSetStyle(DEFAULT,TEXT_COLOR_NORMAL,ColorToInt(text));
    GuiSetStyle(DEFAULT,BORDER_COLOR_FOCUSED,0x7bb8bdff);
    GuiSetStyle(DEFAULT,BASE_COLOR_FOCUSED,0x263b43ff);
    GuiSetStyle(DEFAULT,TEXT_COLOR_FOCUSED,0xe0f4f5ff);
    GuiSetStyle(DEFAULT,BASE_COLOR_PRESSED,0x36505aff);
    GuiSetStyle(DEFAULT,TEXT_COLOR_PRESSED,0xffffffff);
    GuiSetStyle(DEFAULT,BORDER_COLOR_DISABLED,0x30363fff);
    GuiSetStyle(DEFAULT,BASE_COLOR_DISABLED,0x171a1fff);
    GuiSetStyle(DEFAULT,TEXT_COLOR_DISABLED,0x66717dff);
}
bool Gui::blocksCamera() const {
    return menu_!=0||picker_||!error_.empty()||GetMouseY()<36||GetMouseY()>GetScreenHeight()-26||CheckCollisionPointRec(GetMousePosition(),playbackPanel());
}
void Gui::browse(const std::filesystem::path& input) {
    std::error_code ec;
    auto full=std::filesystem::absolute(input,ec);
    if (ec||!std::filesystem::is_directory(full,ec)) { directoryError_="Directory does not exist"; return; }
    directory_=full.lexically_normal(); entries_.clear(); scroll_=0; directoryError_.clear();
    auto str=directory_.u8string();
    std::snprintf(path_.data(),path_.size(),"%s",str.c_str());
    for (std::filesystem::directory_iterator it(directory_,ec),end;!ec&&it!=end;it.increment(ec)) {
        auto ext=it->path().extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if (it->is_directory(ec)||ext==".yaml"||ext==".yml") entries_.push_back(*it);
    }
    if (ec) directoryError_=ec.message();
    std::sort(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){
        if (a.is_directory()!=b.is_directory()) return a.is_directory();
        return a.path().filename().u8string()<b.path().filename().u8string();
    });
}
void Gui::openPicker(const std::filesystem::path& directory) { picker_=true; menu_=0; browse(directory); }
void Gui::showError(std::string message) { error_=std::move(message); menu_=0; }
void Gui::draw(Viewer& viewer) {
    const float width=static_cast<float>(GetScreenWidth()),height=static_cast<float>(GetScreenHeight());
    DrawRectangle(0,0,GetScreenWidth(),36,panel);
    DrawLine(0,35,GetScreenWidth(),35,line);
    if (picker_ || !error_.empty()) GuiLock();
    if (GuiButton({10,5,52,26},"File")) menu_=menu_==1?0:1;
    if (GuiButton({68,5,60,26},"View")) menu_=menu_==2?0:2;
    GuiUnlock();
    DrawText("fxp_viewer",static_cast<int>(width)-112,11,16,{120,139,151,255});
    DrawRectangle(0,GetScreenHeight()-26,GetScreenWidth(),26,panel);
    DrawLine(0,GetScreenHeight()-26,GetScreenWidth(),GetScreenHeight()-26,line);
    DrawText(width>=1000 ? "RMB + WASD: fly   Q/E: down/up   Shift: faster   MMB: pan   Alt + LMB: orbit   Wheel: dolly" : "RMB: fly   MMB: pan   Alt+LMB: orbit   Wheel: dolly",
        12,GetScreenHeight()-18,10,{150,162,175,255});
    std::string info=std::to_string(viewer.scene().objects().size())+" objects   "+std::to_string(GetFPS())+" FPS";
    DrawText(info.c_str(),GetScreenWidth()-MeasureText(info.c_str(),10)-12,GetScreenHeight()-18,10,{150,162,175,255});
    // Playback stays available in a fixed bottom-centre panel.
    auto playback=playbackPanel();
    DrawRectangleRec(playback,panel); DrawRectangleLinesEx(playback,1,line);
    auto simulation=viewer.simulation();
    const bool blocked=modalActive() || IsCursorHidden();
    const float buttonWidth=(playback.width-40)/4;
    const char* labels[]={"Play","Pause","Step","Reset"};
    for (int i=0;i<4;++i) {
        bool enabled=simulation && !blocked;
        if (simulation) {
            if (i==0) enabled=enabled&&!simulation->playing()&&!simulation->faulted();
            if (i==1) enabled=enabled&&simulation->playing();
            if (i==2) enabled=enabled&&!simulation->faulted();
        }
        if (!enabled) GuiDisable();
        bool clicked=GuiButton({playback.x+8+i*(buttonWidth+8),playback.y+10,buttonWidth,30},labels[i]);
        GuiEnable();
        if (clicked && enabled) {
            try {
                if (i==0) simulation->play();
                else if (i==1) simulation->pause(viewer.scene());
                else if (i==2) simulation->step(viewer.scene());
                else simulation->reset(viewer.scene());
            } catch (const std::exception& e) { showError(e.what()); }
        }
    }
    char status[128];
    if (simulation)
        std::snprintf(status,sizeof(status),"%s   Tick %llu   %.2f s",
            simulation->faulted()?"Reset required":simulation->playing()?"Playing":"Paused",
            static_cast<unsigned long long>(simulation->tick()),simulation->time());
    else std::snprintf(status,sizeof(status),"No physics configured");
    DrawText(status,static_cast<int>(playback.x+(playback.width-MeasureText(status,10))/2),
        static_cast<int>(playback.y+54),10,text);
    if (menu_) {
        Rectangle box{menu_==1?10.0f:68.0f,38,220,76};
        DrawRectangleRec(box,panel); DrawRectangleLinesEx(box,1,line);
        if (menu_==1) {
            if (GuiButton({box.x+4,box.y+4,212,30},"Open...                 Ctrl+O")) openPicker(directory_.empty()?std::filesystem::current_path():directory_);
            if (GuiButton({box.x+4,box.y+40,212,30},"Exit")) viewer.requestClose();
        } else {
            if (GuiButton({box.x+4,box.y+4,212,30},IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)?"Leave Fullscreen      F11":"Go Fullscreen         F11")) { viewer.toggleFullscreen(); menu_=0; }
            if (GuiButton({box.x+4,box.y+40,212,30},"Reset Camera             Home")) { viewer.resetCamera(); menu_=0; }
        }
        if (IsKeyPressed(KEY_ESCAPE)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&GetMouseY()>36&&!CheckCollisionPointRec(GetMousePosition(),box))) menu_=0;
    }
    if (picker_) {
        DrawRectangle(0,36,GetScreenWidth(),GetScreenHeight()-36,{0,0,0,160});
        float w=std::min(width-32,720.0f),h=std::min(height-80,520.0f);
        Rectangle box{(width-w)/2,(height-h)/2,w,h};
        DrawRectangleRec(box,panel); DrawRectangleLinesEx(box,1,line);
        DrawText("Open scene",static_cast<int>(box.x+18),static_cast<int>(box.y+16),20,text);
        if (GuiButton({box.x+w-42,box.y+10,30,28},"x")||IsKeyPressed(KEY_ESCAPE)) picker_=false;
        if (GuiButton({box.x+16,box.y+52,48,30},"Up")) browse(directory_.parent_path());
        if (GuiTextBox({box.x+72,box.y+52,w-88,30},path_.data(),static_cast<int>(path_.size()),editing_)) editing_=!editing_;
        int rows=std::max(1,static_cast<int>((h-174)/30));
        Rectangle list{box.x+16,box.y+94,w-32,rows*30.0f};
        if (CheckCollisionPointRec(GetMousePosition(),list))
            scroll_=std::clamp(scroll_-static_cast<int>(GetMouseWheelMove()*3),0,std::max(0,static_cast<int>(entries_.size())-rows));
        for (int row=0;row<rows && row+scroll_<static_cast<int>(entries_.size());++row) {
            const auto entry=entries_[row+scroll_];
            auto name=(entry.is_directory()?std::string("[DIR]  "):std::string("       "))+entry.path().filename().u8string();
            if (GuiButton({list.x,list.y+row*30,list.width,28},shorten(name,static_cast<int>(w)-60).c_str())) {
                if (entry.is_directory()) { browse(entry.path()); break; }
                auto value=entry.path().u8string();
                std::snprintf(path_.data(),path_.size(),"%s",value.c_str());
                editing_=false;
            }
        }
        DrawText(shorten(directoryError_.empty()?"Select a YAML scene, or enter a full path.":directoryError_,static_cast<int>(w)-32,10).c_str(),
            static_cast<int>(box.x+16),static_cast<int>(box.y+h-60),10,text);
        if (GuiButton({box.x+w-128,box.y+h-42,112,28},"Open")||IsKeyPressed(KEY_ENTER)) {
            auto path=std::filesystem::u8path(path_.data());
            if (path.is_relative()) path=directory_/path;
            std::error_code ec;
            if (std::filesystem::is_directory(path,ec)) browse(path);
            else {
                picker_=false; editing_=false;
                if (!viewer.loadScene(path)) showError(viewer.lastError());
            }
        }
    }
    if (!error_.empty()) {
        DrawRectangle(0,36,GetScreenWidth(),GetScreenHeight()-36,{0,0,0,160});
        float w=std::min(width-32,800.0f);
        Rectangle box{(width-w)/2,100,w,std::min(height-140,340.0f)};
        DrawRectangleRec(box,panel); DrawRectangleLinesEx(box,1,{153,84,70,255});
        DrawText("Viewer error",static_cast<int>(box.x+18),118,20,{248,166,145,255});
        std::string remaining=error_; int y=156;
        while (!remaining.empty() && y<box.y+box.height-60) {
            auto newline=remaining.find('\n');
            std::string part=remaining.substr(0,newline);
            auto shown=shorten(part,static_cast<int>(w)-36,16);
            DrawText(shown.c_str(),static_cast<int>(box.x+18),y,16,text);
            std::size_t used=shown.size();
            if (used==part.size()&&newline!=std::string::npos) ++used;
            if (used==0) used=1;
            remaining.erase(0,used); y+=22;
        }
        if (GuiButton({box.x+w-112,box.y+box.height-44,94,28},"Close")||IsKeyPressed(KEY_ESCAPE)) error_.clear();
    }
}
}



