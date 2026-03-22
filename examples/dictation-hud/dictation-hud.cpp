// Floating dictation HUD for whisper-toggle.sh (Gtk3 / gtkmm). Modes: listen | busy
// Exit codes: 0 = normal, 2 = user cancelled (ESC)
#include <gdkmm/screen.h>
#include <glib-unix.h>
#include <gtkmm.h>

#include <cstdlib>
#include <string>

namespace {

constexpr const char * k_css = R"(
.whisper-hud {
    background-color: rgba(22, 22, 24, 0.96);
    border-radius: 12px;
    border: 1px solid rgba(255, 255, 255, 0.08);
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.45);
}
.whisper-hud label { color: #e6edf3; }
.whisper-hud progressbar trough {
    min-height: 5px;
    border-radius: 3px;
    background-color: rgba(255, 255, 255, 0.06);
}
.whisper-hud progressbar progress {
    border-radius: 3px;
    min-height: 5px;
}
.mode-listen progressbar progress { background-color: #3fb950; }
.mode-busy progressbar progress { background-color: #58a6ff; }
)";

void apply_theme(Gtk::Window & window, const std::string & mode) {
    auto provider = Gtk::CssProvider::create();
    try {
        provider->load_from_data(std::string(k_css));
    } catch (const Glib::Error &) {
        return;
    }
    Gtk::StyleContext::add_provider_for_screen(
        Gdk::Screen::get_default(), provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    window.get_style_context()->add_class("whisper-hud");
    window.get_style_context()->add_class(mode == "busy" ? "mode-busy" : "mode-listen");
}

void place_bottom_center(Gtk::Window & window, int width) {
    Glib::RefPtr<Gdk::Screen> screen = Gdk::Screen::get_default();
    const int mon                        = screen->get_primary_monitor();
    Gdk::Rectangle geom;
    screen->get_monitor_geometry(mon, geom);
    // Request size so we can read back the actual height
    int win_w = 0, win_h = 0;
    window.get_size(win_w, win_h);
    const int margin = 28;
    window.move(geom.get_x() + std::max(0, (geom.get_width() - width) / 2),
                geom.get_y() + geom.get_height() - win_h - margin);
}

} // namespace

int main(int argc, char ** argv) {
    // Extract our mode argument before GTK sees it (GTK would treat it as a filename)
    std::string mode = "listen";
    if (argc >= 2 && argv[1][0] != '-') {
        mode = argv[1];
        // Remove our argument so GTK only sees the program name
        for (int i = 1; i < argc - 1; ++i) {
            argv[i] = argv[i + 1];
        }
        --argc;
    }
    if (mode != "listen" && mode != "busy") {
        return 1;
    }

    auto app = Gtk::Application::create(argc, argv, "io.github.ggml-org.whispercpp.dictation_hud",
                                        Gio::APPLICATION_NON_UNIQUE | Gio::APPLICATION_HANDLES_OPEN);

    int exit_code = 0;

    Gtk::Window window;
    window.set_decorated(false);
    window.set_keep_above(true);
    window.set_skip_taskbar_hint(true);
    // In listen mode, accept focus so we can catch ESC to cancel
    window.set_accept_focus(mode == "listen");
    window.set_focus_on_map(mode == "listen");
    window.set_type_hint(Gdk::WINDOW_TYPE_HINT_NOTIFICATION);
    window.set_default_size(300, -1);
    window.stick();

    // ESC key cancels recording (exit code 2 signals cancellation to toggle script)
    window.signal_key_press_event().connect([&](GdkEventKey * event) -> bool {
        if (event->keyval == GDK_KEY_Escape) {
            exit_code = 2;
            window.close();
            return true;
        }
        return false;
    }, false);

    apply_theme(window, mode);

    auto * outer = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
    outer->set_margin_top(10);
    outer->set_margin_bottom(10);
    outer->set_margin_start(14);
    outer->set_margin_end(14);

    auto * brand = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    auto * wm    = Gtk::manage(new Gtk::Label());
    wm->set_markup("<span font_family='monospace' font_size='small' letter_spacing='1000'>whisper.cpp</span>");
    wm->set_halign(Gtk::ALIGN_START);
    auto * badge = Gtk::manage(new Gtk::Label());
    badge->set_markup("<span font_family='monospace' size='x-small' alpha='55%'>LOCAL</span>");
    badge->set_halign(Gtk::ALIGN_START);
    brand->pack_start(*wm, false, false, 0);
    brand->pack_start(*badge, false, false, 0);
    outer->pack_start(*brand, false, false, 0);

    const char * headline = mode == "busy" ? "Transcribing" : "Listening";
    const char * detail   = mode == "busy" ? "Running whisper-cli on your clip…"
                                           : "Speak — press shortcut to transcribe, ESC to cancel";

    auto * title = Gtk::manage(new Gtk::Label());
    title->set_markup(std::string("<span font_weight='bold' size='large'>") + headline + "</span>");
    title->set_halign(Gtk::ALIGN_START);
    outer->pack_start(*title, false, false, 0);

    auto * bar = Gtk::manage(new Gtk::ProgressBar());
    bar->set_show_text(false);
    bar->set_size_request(260, 4);
    bar->set_pulse_step(0.012);
    outer->pack_start(*bar, false, false, 0);

    auto * sub = Gtk::manage(new Gtk::Label());
    sub->set_markup(std::string("<span alpha='70%' size='small'>") + detail + "</span>");
    sub->set_halign(Gtk::ALIGN_START);
    sub->set_line_wrap(true);
    sub->set_max_width_chars(42);
    outer->pack_start(*sub, false, false, 0);

    window.add(*outer);

    place_bottom_center(window, 300);

    sigc::connection pulse = Glib::signal_timeout().connect([bar]() {
        bar->pulse();
        return true;
    },
                                                              60);

    // Disconnect the timer before widgets are destroyed to prevent use-after-free on bar
    window.signal_hide().connect([pulse]() mutable { pulse.disconnect(); });
    window.signal_unrealize().connect([&pulse]() { pulse.disconnect(); });

    auto on_unix_term = +[](gpointer user_data) -> gboolean {
        auto * w = static_cast<Gtk::Window *>(user_data);
        w->close();
        return G_SOURCE_REMOVE;
    };
    g_unix_signal_add(SIGTERM, on_unix_term, &window);
    g_unix_signal_add(SIGINT, on_unix_term, &window);

    window.show_all();
    app->run(window);
    return exit_code;
}
