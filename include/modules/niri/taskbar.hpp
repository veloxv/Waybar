#pragma once

#include <ranges>
#include <gtkmm/button.h>
#include <json/value.h>

#include "AAppIconLabel.hpp"
#include "AModule.hpp"
#include "bar.hpp"
#include "modules/niri/backend.hpp"
#include "util/rewrite_string.hpp"
#include "util/sanitize_str.hpp"

namespace waybar::modules::niri {


class Task;

class Taskbar : public AModule, public EventHandler {
 public:
  Taskbar(const std::string &, const Bar &, const Json::Value &);
  ~Taskbar() override;
  void update() override;

 private:

  void onEvent(const Json::Value &ev) override;
  void doUpdate();
  Gtk::Button &addTask(const Json::Value &win);
  std::string getIcon(const Json::Value &win);
  bool image_load_icon(Gtk::Image &image, const Glib::RefPtr<Gtk::IconTheme> &icon_theme,
      Glib::RefPtr<Gio::DesktopAppInfo> app_info, int size);

  const Bar &bar_;
  Gtk::Box box_;

  // Map from niri window id to task
  std::unordered_map<uint64_t, std::shared_ptr<Task>> tasks_;
};

class Task : public AAppIconLabel {
  public:
    Task(const Json::Value &win, const Json::Value &config);
    ~Task() override;
    auto doUpdate(const Json::Value &win, std::unordered_map<uint64_t, const Json::Value> my_workspaces, const Bar &bar_) -> void;
    auto update() -> void override;
    Gtk::EventBox &getEventBox();

    uint64_t win_id_;

  protected:
    bool handleClick(GdkEventButton* const& e);
};

}  // namespace waybar::modules::niri
