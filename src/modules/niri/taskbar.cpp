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

  /* std::vector<Json::Value> my_windows; */
  std::unordered_map<uint64_t, const Json::Value> my_workspaces;
  const auto &workspaces = gIPC->workspaces();
  const auto &windows = gIPC->windows();

  /* insert workspaces into map for visibility checking */
  for (const Json::Value &ws: workspaces)
    my_workspaces.insert(std::pair<uint64_t, const Json::Value>(ws["id"].asUInt64(), ws));


  /* get all workspaces for which windows should be shown */
  /* auto current_ws = std::views::filter(workspaces, */
  /*     [&](const auto &ws) { */
  /*     if (config_["all-outputs"] && config_["all-workspaces"]) */
  /*       return true; */
  /*  */
  /*     if (config_["all-outputs"]) */
  /*       return ws["is_active"].asBool(); */
  /*  */
  /*     return ws["is_active"].asBool() && ws["output"].asString() == bar_.output->name; */
  /*     }); */

  // get all relevant windows on the selected workspaces
  /* std::ranges::copy_if(windows, std::back_inserter(my_windows), */
  /*              [&](const auto &win) { */
  /*                return std::ranges::find_if(*current_ws, */
  /*                    [&](const auto &ws) { return win["workspace_id"].asUInt64() == ws["id"].asUInt64(); }) != */
  /*                current_ws->end(); */
  /*              }); */

  // Remove tasks for removed windows.
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    // find window id of this button in window list
    auto win = std::ranges::find_if(windows,
                           [it](const auto &win) { return win["id"].asUInt64() == it->first; });

    // remove if not found
    if (win == windows.end()) {
      it = tasks_.erase(it);
    } else {
      ++it;
    }
  }

  // Add tasks for new windows, update existing ones.
  for (const auto &win : windows) {

    auto ti = tasks_.find(win["id"].asUInt64());
    Task *task;
    if (ti == tasks_.end()) {
      tasks_.emplace(win["id"].asUInt64(), std::make_shared<Task>(win, config_));
      task = tasks_.find(win["id"].asUInt64())->second.get();
      box_.add(task->getEventBox());
    }
    else
      task = ti->second.get();

    task->doUpdate(win, my_workspaces, bar_);
  }

  // Refresh the button order.
  /* for (auto it = my_windows.cbegin(); it != my_windows.cend(); ++it) { */
  /*   const auto &ws = *it; */
  /*  */
  /*   auto pos = ws["id"].asUInt() - 1; */
  /*  */
  /*   auto &button = tasks_[ws["id"].asUInt64()].button; */
  /*   box_.reorder_child(button, pos); */
  /* } */

  spdlog::debug("[Taskbar]: updated.");
}

void Taskbar::update() {
  doUpdate();
  AModule::update();
}

/* Task implementations */
Task::Task(const Json::Value &win, const Json::Value &config):
  AAppIconLabel(config, "task", win["app_id"].asString(), config["format"].asString()) {
    event_box_.show_all();
    box_.set_name("task");

    event_box_.signal_button_press_event().connect(sigc::mem_fun(*this, &Task::handleClick));

    /* save id for click action */
    win_id_ = win["id"].asUInt64();
};


Task::~Task() = default;


auto Task::doUpdate(const Json::Value &win, const std::unordered_map<uint64_t, const Json::Value> my_workspaces, const Bar &bar_) -> void {

  /* visibility */
  const auto &ws = my_workspaces.find(win["workspace_id"].asUInt64())->second;

  if ((config_["all-outputs"].asBool() && config_["all-workspaces"].asBool()) ||
      (config_["all-outputs"].asBool() && ws["is_active"].asBool()) ||
      (config_["all-workspaces"].asBool() && ws["output"].asString() == bar_.output->name) ||
      (ws["is_active"].asBool() && ws["output"].asString() == bar_.output->name)
      )
    event_box_.show();
  else
    event_box_.hide();

  /* format text */
  std::string format_text = config_["format"].asString();

  if (win["is_floating"].asBool() && config_["format-floating"].isString())
    format_text = config_["format-floating"].asString();

  if (win["is_focused"].asBool() && config_["format-focused"].isString())
    format_text = config_["format-focused"].asString();

  std::string label_text;
  if (!format_text.empty()) {
    label_.show();
    label_text = waybar::util::rewriteString(
        fmt::format(fmt::runtime(format_text),
          fmt::arg("appid", win["app_id"].asString()),
          fmt::arg("title", win["title"].asString()),
          fmt::arg("windowid", win["id"].asString()),
          fmt::arg("workspaceid", win["workspace_id"].asString())),
        config_["rewrite"]);
    label_.set_markup(label_text);
  }
  else
    label_.hide();

  /* tooltip */
  if (tooltipEnabled()) {
    std::string tooltip_format;
    if (config_["tooltip-format"].isString()) {
      tooltip_format = config_["tooltip-format"].asString();
    }
    if (!tooltip_format.empty()) {
      event_box_.set_tooltip_text(
          fmt::format(fmt::runtime(tooltip_format),
                      fmt::arg("appid", win["app_id"].asString()),
                      fmt::arg("title", win["title"].asString()),
                      fmt::arg("windowid", win["id"].asString()),
                      fmt::arg("workspaceid", win["workspace_id"].asString())));
    } else if (!label_text.empty()) {
      event_box_.set_tooltip_text(label_text);
    }
  }
  else
    event_box_.set_tooltip_text(nullptr);

  /* handle icon */
  if (config_["icon"].asBool()) {
    updateAppIconName(win["app_id"].asString(), win["title"].asString());
    updateAppIcon();
    image_.show();
  }
  else
    image_.hide();

  /* add and remove style classes based on window type */
  auto style_context = box_.get_style_context();
  if (win["is_focused"].asBool())
    style_context->add_class("focused");
  else
    style_context->remove_class("focused");

  if (win["is_floating"].asBool())
    style_context->add_class("floating");
  else
    style_context->remove_class("floating");

  if (win["workspace_id"].asBool() && ws["is_focused"].asBool())
    style_context->add_class("focused_workspace");
  else
    style_context->remove_class("focused_workspace");

  if (win["workspace_id"].asBool() && ws["is_active"].asBool())
    style_context->add_class("active_workspace");
  else
    style_context->remove_class("active_workspace");

  spdlog::debug("[Task]: {}, visible: {}, label_text: {}, tooltip_format: {}, icon_name: {}", win["app_id"].asString(), event_box_.is_visible(), label_text, config_["tooltip-format"].asString(), app_icon_name_);
  AAppIconLabel::update();
}

Gtk::EventBox &Task::getEventBox() {
  return event_box_;
}

auto Task::update() -> void {
  AAppIconLabel::update();
}

bool Task::handleClick(GdkEventButton* const& e) {
  /* if (config_["focus-on-click"].isBool() && config_["focus-on-click"].asBool()){ */
    try {
      // {"Action":{"FocusWindow":{"id":1}}}
      Json::Value request(Json::objectValue);
      auto& action = (request["Action"] = Json::Value(Json::objectValue));
      auto& focusWindow = (action["FocusWindow"] = Json::Value(Json::objectValue));
      focusWindow["id"] = win_id_;

      IPC::send(request);

      spdlog::debug("[Task] Clicked task id {}", win_id_);
    } catch (const std::exception &e) {
      spdlog::error("[Task] Error switching window: {}", e.what());
    }
  /* } */

  return true;
}

}  // namespace waybar::modules::niri
