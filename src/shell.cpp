/*
 * Copyright (c) 2013 Tiago Vignatti
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <memory>

#include <gtkmm.h>
#include <wayland-client.h>

#include "gdkwayland.hpp"
#include "weston.hpp"
#include "wayland.hpp"
//#include "weston-desktop-shell-protocol.hpp"
//#include <wayland-client-protocol-extra.hpp>


#define LOGD(...) std::cout << "[DEBUG]: " << __VA_ARGS__ << '\n'
#define LOGI(...) std::cout << " [INFO]: " << __VA_ARGS__ << '\n'
#define LOGE(...) std::cerr << "  [ERR]: " << __VA_ARGS__ << '\n'

extern char** environ; /* defined by libc */

namespace tablecloth {
  std::string filename = "/home/topisani/.bin/wm/background.jpg";
  char* terminal_path = "/usr/bin/weston-terminal";

  struct Element {
    std::unique_ptr<Gtk::Window> window;
    Glib::RefPtr<Gdk::Pixbuf> pixbuf;
    wayland::Surface surface;
  };

  struct Desktop {
    Desktop() = default;

    wayland::Display display;
    wayland::Registry registry;
    weston::DesktopShell shell;
    wayland::Output output;

    Glib::RefPtr<Gdk::Display> gdk_display;
    Element background;
    Element curtain;
    Element panel;

    void create_panel();
    void create_grab_surface();
    void create_background();
  };

  static void launch_terminal()
  {
    char* argv[] = {nullptr, nullptr};
    pid_t pid;

    pid = fork();
    if (pid < 0) {
      fprintf(stderr, "fork failed: %m\n");
      return;
    }

    if (pid) return;

    argv[0] = terminal_path;
    if (execve(terminal_path, argv, environ) < 0) {
      fprintf(stderr, "execl '%s' failed: %m\n", terminal_path);
      exit(1);
    }
  }

  void Desktop::create_panel()
  {
    panel.window = std::make_unique<Gtk::Window>(Gtk::WindowType::WINDOW_TOPLEVEL);
    auto& window = *panel.window;

    window.set_title("tablecloth panel");
    window.set_decorated(false);

    auto& box1 = *Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
    window.add(box1);

    auto& button = *Gtk::manage(new Gtk::Button("TERM"));
    button.signal_clicked().connect(&launch_terminal);
    box1.pack_start(button, true, false, 0);
    button.show();
    box1.show();

    window.show_all();

    window.set_size_request(16, 16);

    Gdk::wayland::window::set_use_custom_surface(window);
    panel.surface = Gdk::wayland::window::get_wl_surface(window);
    shell.set_panel(output, panel.surface);
    shell.set_panel_position(weston::DesktopShell::PanelPosition::Top);
  }

  void Desktop::create_grab_surface()
  {
    curtain.window = std::make_unique<Gtk::Window>(Gtk::WINDOW_TOPLEVEL);
    auto& window = *curtain.window;
    window.set_title("Tablecloth grab surface");
    window.set_decorated(false);
    window.set_size_request(8192, 8192);
    window.show_all();

    LOGD("Curtain");
    Gdk::wayland::window::set_use_custom_surface(window);
    curtain.surface = Gdk::wayland::window::get_wl_surface(window);
    shell.set_grab_surface(curtain.surface);
  }

  void Desktop::create_background()
  {
    const char* xpm_data[] = {"1 1 1 1", "_ c SteelBlue", "_"};

    if (!filename.empty())
      background.pixbuf = Gdk::Pixbuf::create_from_file(filename);
    else
      background.pixbuf = Gdk::Pixbuf::create_from_xpm_data(xpm_data);

    background.window = std::make_unique<Gtk::Window>(Gtk::WINDOW_TOPLEVEL);
    auto& window = *background.window;

    window.signal_delete_event().connect([](GdkEventAny* const) {
      Gtk::Main::quit();
      return true;
    });
    window.signal_draw().connect([this](Cairo::RefPtr<Cairo::Context> ctx) {
      Gdk::Cairo::set_source_pixbuf(ctx, background.pixbuf, 0, 0);
      ctx->paint();

      return true;
    });

    window.set_title("background");
    window.set_decorated(false);
    window.set_size_request(1200, 600);
    window.show_all();

    LOGD("Background");
    Gdk::wayland::window::set_use_custom_surface(window);
    background.surface = Gdk::wayland::window::get_wl_surface(window);
    shell.set_background(output, background.surface);

    shell.set_background(output, background.surface);
  }

  int main(int argc, char* argv[])
  {
    Desktop desktop;

    auto main = Gtk::Main(argc, argv);

    LOGD("Other FD: " << desktop.display.get_fd());

    if (!desktop.display) LOGE("Failed to convert gdk wayland display to waylandpp");

    desktop.registry = desktop.display.get_registry();
    desktop.registry.on_global() = [&desktop](uint32_t name, std::string interface,
                                              uint32_t version) {
      LOGD("Got registry: " << interface);
      if (interface == wayland::weston_desktop_shell_t::interface_name) {
        desktop.registry.bind(name, desktop.shell, version);
        LOGD("Bound desktop shell");

        desktop.shell.on_grab_cursor() = [](uint32_t) { LOGD("cursor grabbed"); };
        desktop.shell.on_configure() = [&desktop](uint32_t edges, wayland::surface_t surf,
                                                  int32_t width, int32_t height) {
          LOGD("Configure shell: " << width << "x" << height);

          desktop.panel.window->set_size_request(width, 16);
          desktop.background.window->set_size_request(width, height);
          desktop.shell.desktop_ready();
        };

      } else if (interface == wayland::output_t::interface_name) {
        /* TODO: create multiple outputs */
        desktop.registry.bind(name, desktop.output, version);
      }
    };
    desktop.display.roundtrip();

    /* Wait until we have been notified about the compositor and shell
     * objects */
    if (!desktop.output || !desktop.shell) {
      LOGE("could not find output or shell modules");
     }
    desktop.create_background();
    desktop.create_grab_surface();
    desktop.create_panel();
    // launch_terminal();
    desktop.shell.desktop_ready();
    main.run();

    /* TODO cleanup */
    return EXIT_SUCCESS;
  }
} // namespace tablecloth

int main(int argc, char* argv[])
{
  return tablecloth::main(argc, argv);
}
