#include "modules/niri/taskbar.hpp"

#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <spdlog/spdlog.h>

namespace waybar::modules::niri {

Taskbar::Taskbar(const std::string &id, const Bar &bar, const Json::Value &config)
    : AModule(config, "taskbar", id, false, false), bar_(bar), box_(bar.orientation, 0) {
  box_.set_name("taskbar");
  if (!id.empty()) {
    box_.get_style_context()->add_class(id);
  }
  box_.get_style_context()->add_class(MODULE_CLASS);
  event_box_.add(box_);

  if (!gIPC) gIPC = std::make_unique<IPC>();

  /* gIPC->registerForIPC("WorkspaceChanged", this); */
  /* gIPC->registerForIPC("WorkspaceActivated", this); */
  /* gIPC->registerForIPC("WorkspaceActiveWindowChanged", this); */

  gIPC->registerForIPC("WindowsChanged", this);
  gIPC->registerForIPC("WindowOpenedOrChanged", this);
  gIPC->registerForIPC("WindowClosed", this);
  gIPC->registerForIPC("WindowFocusChanged", this);

  dp.emit();
}

Taskbar::~Taskbar() { gIPC->unregisterForIPC(this); }

void Taskbar::onEvent(const Json::Value &ev) { dp.emit(); }

void Taskbar::doUpdate() {
  auto ipcLock = gIPC->lockData();

  const auto alloutputs = config_["all-outputs"].asBool();
  const auto allworkspaces = config_["all-workspaces"].asBool();
  std::vector<Json::Value> my_windows;
  std::vector<Json::Value> my_workspaces;
  const auto &workspaces = gIPC->workspaces();
  const auto &windows = gIPC->windows();


  // get active workspace on current output
  auto current_ws = std::find_if(workspaces.cbegin(), workspaces.cend(),
      [&](const auto &ws) {
        return ws["output"].asString() == bar_.output->name && ws["is_active"].asBool();
      });

  // get all relevant windows on the current workspace
  std::copy_if(windows.cbegin(), windows.cend(), std::back_inserter(my_windows),
               [&](const auto &win) {
                 if (allworkspaces) return true;

                 return win["workspace_id"].asUInt64() == (*current_ws)["id"].asUInt64();
               });

  // Remove buttons for removed windows.
  for (auto it = buttons_.begin(); it != buttons_.end();) {
    // find window id of this button in window list
    auto win = std::find_if(my_windows.begin(), my_windows.end(),
                           [it](const auto &win) { return win["id"].asUInt64() == it->first; });
    // remove if not found
    if (win == my_windows.end()) {
      it = buttons_.erase(it);
    } else {
      ++it;
    }
  }

  // Add buttons for new windows, update existing ones.
  // TODO
  for (const auto &win : my_windows) {
    auto bit = buttons_.find(win["id"].asUInt64());
    auto &button = bit == buttons_.end() ? addButton(win) : bit->second;
    auto style_context = button.get_style_context();

    // add and remove style classes based on window type
    if (win["is_focused"].asBool())
      style_context->add_class("focused");
    else
      style_context->remove_class("focused");

    if (win["is_floating"].asBool())
      style_context->add_class("floating");
    else
      style_context->remove_class("floating");

    if (win["workspace_id"]) {
      if (win["workspace_id"].asUInt64() == (*current_ws)["id"].asUInt64())
        style_context->add_class("current_workspace");
      else
        style_context->remove_class("current_workspace");
    } else {
      style_context->remove_class("current_workspace");
    }

    // handle named workspaces
    // TODO: handle named windows!
    std::string title = win["title"].asString();
    button.set_name("niri-window-" + win["id"].asString());

    if (config_["format"].isString()) {
      auto format = config_["format"].asString();
      title = fmt::format(fmt::runtime(format), fmt::arg("icon", getIcon(win)),
                         fmt::arg("appid", win["app_id"].asString()),
                         fmt::arg("value", title), fmt::arg("title", win["title"].asString()),
                         fmt::arg("windowid", win["id"].asUInt()),
                         fmt::arg("workspace", win["workspace_id"].asString()));
    }
    if (!config_["disable-markup"].asBool()) {
      static_cast<Gtk::Label *>(button.get_children()[0])->set_markup(title);
    } else {
      button.set_label(title);
    }

    // hide windows
    // TODO hide windows based on list?
    if (config_["current-only"].asBool()) {
      const auto *property = alloutputs ? "is_focused" : "is_active";
      if (win[property].asBool())
        button.show();
      else
        button.hide();
    } else {
      button.show();
    }
  }

  // Refresh the button order.
  for (auto it = my_windows.cbegin(); it != my_windows.cend(); ++it) {
    const auto &ws = *it;

    // TODO sort by position?
    auto pos = ws["id"].asUInt() - 1;
    if (alloutputs) pos = it - my_windows.cbegin();

    auto &button = buttons_[ws["id"].asUInt64()];
    box_.reorder_child(button, pos);
  }
}

void Taskbar::update() {
  doUpdate();
  AModule::update();
}

Gtk::Button &Taskbar::addButton(const Json::Value &win) {
  std::string name = win["id"].asString();

  auto pair = buttons_.emplace(win["id"].asUInt64(), name);
  auto &&button = pair.first->second;
  box_.pack_start(button, false, false, 0);
  button.set_relief(Gtk::RELIEF_NONE);
  if (!config_["disable-click"].asBool()) {
    const auto id = win["id"].asUInt64();
    button.signal_pressed().connect([=] {
      try {
        // {"Action":{"FocusWindow":{"id":1}}}
        Json::Value request(Json::objectValue);
        auto &action = (request["Action"] = Json::Value(Json::objectValue));
        auto &focusWindow = (action["FocusWindow"] = Json::Value(Json::objectValue));
        focusWindow["id"] = id;

        IPC::send(request);
      } catch (const std::exception &e) {
        spdlog::error("Error switching window: {}", e.what());
      }
    });
  }
  return button;
}

std::string Taskbar::getIcon(const Json::Value &win) {
  // TODO get icon from desktop file
  return win["title"].asString();
}

// for icons
// handle app_id is called to get name and icon
// - image_load_icon -> uses icon theme and app info to load icon image

}  // namespace waybar::modules::niri
