#pragma once

class WayfireBackground;

class BackgroundDrawingArea : public Gtk::DrawingArea
{
    WayfireBackground *background;

    public:
    BackgroundDrawingArea(WayfireBackground *background);

    protected:
    bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
};

class WayfireBackground
{
    WayfireShellApp *app;
    WayfireOutput *output;
    zwf_wm_surface_v1 *wm_surface = NULL;

    BackgroundDrawingArea drawing_area;
    Gtk::Window window;
    Gtk::Image image;
    int scale;
    int cycle_timeout;
    bool inhibited = true;
    uint background[2] = {0};
    int output_width, output_height;
    std::vector<std::string> images;
    sigc::connection change_bg_conn;

    wf_option background_image, background_cycle_timeout;
    wf_option_callback image_updated, cycle_timeout_updated;

    void create_wm_surface();
    void handle_output_resize(uint32_t width, uint32_t height);
    bool background_transition_frame(int timer);
    bool change_background(int timer);
    void load_image(std::string path);
    bool load_images_from_dir(std::string path);
    bool load_next_background(std::string &path, uint current);
    void reset_background();
    void set_background();
    void reset_cycle_timeout();
    void setup_window();

    public:
    wf_duration fade_animation;
    Glib::RefPtr<Gdk::Pixbuf> pbuf, pbuf2;
    WayfireBackground(WayfireShellApp *app, WayfireOutput *output);
    ~WayfireBackground();
};
