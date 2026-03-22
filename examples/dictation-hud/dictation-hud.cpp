// Floating dictation HUD for whisper-toggle.sh (Gtk3 / gtkmm). Modes: listen | busy
// Exit codes: 0 = normal, 2 = user cancelled (ESC)
// Runs under XWayland (GDK_BACKEND=x11) so window positioning works on GNOME Wayland.
// Usage: whisper-dictation-hud <listen|busy> [audio-file-path]
#include <gdkmm/screen.h>
#include <glib-unix.h>
#include <gtkmm.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr const char * k_css = R"(
.whisper-hud {
    background-color: rgba(18, 18, 22, 0.45);
    border-radius: 10px;
    border: 1px solid rgba(255, 255, 255, 0.15);
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.3), inset 0 1px 0 rgba(255, 255, 255, 0.06);
}
.whisper-hud label { color: rgba(240, 240, 245, 0.95); }
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

void place_bottom_center(Gtk::Window & window) {
    Glib::RefPtr<Gdk::Screen> screen = Gdk::Screen::get_default();
    const int mon = screen->get_primary_monitor();
    Gdk::Rectangle geom;
    screen->get_monitor_geometry(mon, geom);
    int win_w = 0, win_h = 0;
    window.get_size(win_w, win_h);
    window.move(geom.get_x() + (geom.get_width() - win_w) / 2,
                geom.get_y() + geom.get_height() - win_h - 8);
}

// Reads recent audio samples from the wav file being written by arecord.
// Returns normalized amplitudes (0..1) for each of num_bins bins.
static std::vector<double> read_audio_levels(const std::string & path, int num_bins) {
    std::vector<double> levels(num_bins, 0.0);
    if (path.empty()) return levels;

    FILE * f = fopen(path.c_str(), "rb");
    if (!f) return levels;

    // WAV header is 44 bytes, samples are 16-bit signed LE mono @ 16kHz
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    const long header = 44;
    const long sample_bytes = 2;  // 16-bit

    // Read the most recent chunk of audio (~50ms worth = 800 samples at 16kHz)
    const long samples_wanted = 800;
    const long bytes_wanted = samples_wanted * sample_bytes;
    long data_size = file_size - header;
    if (data_size < sample_bytes * num_bins) {
        fclose(f);
        return levels;
    }

    long read_start = file_size - std::min(bytes_wanted, data_size);
    if (read_start < header) read_start = header;
    long actual_bytes = file_size - read_start;
    long actual_samples = actual_bytes / sample_bytes;

    fseek(f, read_start, SEEK_SET);
    std::vector<int16_t> buf(actual_samples);
    size_t got = fread(buf.data(), sample_bytes, actual_samples, f);
    fclose(f);

    if (got < static_cast<size_t>(num_bins)) return levels;

    // Split samples into bins and compute RMS for each
    long per_bin = got / num_bins;
    for (int i = 0; i < num_bins; ++i) {
        double sum_sq = 0.0;
        for (long j = 0; j < per_bin; ++j) {
            double s = buf[i * per_bin + j] / 32768.0;
            sum_sq += s * s;
        }
        double rms = std::sqrt(sum_sq / per_bin);
        // Scale up and clamp (speech typically has low RMS)
        levels[i] = std::min(1.0, rms * 5.0);
    }
    return levels;
}

// Animated waveform widget drawn with Cairo — reacts to live audio input
class Waveform : public Gtk::DrawingArea {
public:
    Waveform(const std::string & mode, const std::string & audio_path)
        : m_mode(mode), m_audio_path(audio_path) {
        set_size_request(140, 24);
        m_tick = 0.0;
        m_levels.assign(k_num_bars, 0.0);
        m_smooth.assign(k_num_bars, 0.0);
    }

    void advance() {
        m_tick += 0.08;
        // Read live levels from the audio file
        auto raw = read_audio_levels(m_audio_path, k_num_bars);
        // Smooth: rise fast, fall slow for natural feel
        for (int i = 0; i < k_num_bars; ++i) {
            if (raw[i] > m_smooth[i]) {
                m_smooth[i] += (raw[i] - m_smooth[i]) * 0.6;  // fast attack
            } else {
                m_smooth[i] += (raw[i] - m_smooth[i]) * 0.15; // slow decay
            }
            m_levels[i] = m_smooth[i];
        }
        queue_draw();
    }

private:
    static constexpr int k_num_bars = 24;

    std::string         m_mode;
    std::string         m_audio_path;
    double              m_tick;
    std::vector<double> m_levels;
    std::vector<double> m_smooth;

    bool on_draw(const Cairo::RefPtr<Cairo::Context> & cr) override {
        const int w = get_allocated_width();
        const int h = get_allocated_height();

        double r, g, b;
        if (m_mode == "busy") {
            r = 0.345; g = 0.651; b = 1.0;
        } else {
            r = 0.247; g = 0.725; b = 0.314;
        }

        const double bar_gap   = static_cast<double>(w) / k_num_bars;
        const double bar_width = bar_gap * 0.55;
        const double mid       = h / 2.0;
        const double max_amp   = (h / 2.0) - 1.0;
        const double min_bar   = 1.5;  // minimum visible bar height

        for (int i = 0; i < k_num_bars; ++i) {
            double level = m_levels[i];
            double bar_h;
            if (m_audio_path.empty()) {
                // No audio file: animated sine wave fallback (for busy mode)
                double phase = i * 0.7 + 0.3 * (i % 3);
                double amp = 0.5 * std::sin(m_tick * 2.3 + phase * 1.1)
                           + 0.3 * std::sin(m_tick * 3.7 + phase * 0.8)
                           + 0.2 * std::sin(m_tick * 5.1 + phase * 1.5);
                double norm = 0.15 + 0.85 * ((amp + 1.0) / 2.0);
                bar_h = norm * max_amp;
            } else {
                bar_h = min_bar + level * (max_amp - min_bar);
            }

            double x = i * bar_gap + (bar_gap - bar_width) / 2.0;

            cr->set_source_rgba(r, g, b, 0.4 + 0.5 * (bar_h / max_amp));
            double radius = bar_width / 2.0;
            double y_top  = mid - bar_h;
            double y_bot  = mid + bar_h;
            cr->begin_new_path();
            cr->arc(x + radius, y_top + radius, radius, M_PI, 0);
            cr->arc(x + radius, y_bot - radius, radius, 0, M_PI);
            cr->close_path();
            cr->fill();
        }
        return true;
    }
};

} // namespace

int main(int argc, char ** argv) {
    setenv("GDK_BACKEND", "x11", 1);

    // Extract mode and optional audio path before GTK sees them
    // argv[1] = mode (listen|busy), argv[2] = audio file path (optional)
    std::string mode = "listen";
    std::string audio_path;
    if (argc >= 2 && argv[1][0] != '-') {
        mode = argv[1];
    }
    if (argc >= 3 && argv[2][0] != '-') {
        audio_path = argv[2];
    }
    argc = 1;  // hide our args from GTK
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
    window.set_accept_focus(mode == "listen");
    window.set_focus_on_map(mode == "listen");
    window.set_type_hint(Gdk::WINDOW_TYPE_HINT_NOTIFICATION);
    window.set_default_size(160, -1);
    window.stick();

    window.signal_key_press_event().connect([&](GdkEventKey * event) -> bool {
        if (event->keyval == GDK_KEY_Escape) {
            exit_code = 2;
            window.close();
            return true;
        }
        return false;
    }, false);

    // Enable RGBA visual for true transparency
    auto screen = Gdk::Screen::get_default();
    auto visual = screen->get_rgba_visual();
    if (visual) {
        gtk_widget_set_visual(GTK_WIDGET(window.gobj()), visual->gobj());
    }
    window.set_app_paintable(true);

    apply_theme(window, mode);

    auto * outer = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
    outer->set_margin_top(8);
    outer->set_margin_bottom(8);
    outer->set_margin_start(10);
    outer->set_margin_end(10);

    // Reactive waveform — no text, just color: green = listening, blue = transcribing
    auto * wave = Gtk::manage(new Waveform(mode, mode == "listen" ? audio_path : ""));
    outer->pack_start(*wave, false, false, 0);

    window.add(*outer);
    // Position after window is mapped so we get the actual height
    window.signal_map().connect([&window]() {
        place_bottom_center(window);
    });

    sigc::connection anim = Glib::signal_timeout().connect([wave]() {
        wave->advance();
        return true;
    }, 50);

    window.signal_hide().connect([anim]() mutable { anim.disconnect(); });
    window.signal_unrealize().connect([&anim]() { anim.disconnect(); });

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
