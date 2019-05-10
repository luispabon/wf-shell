#include <sys/stat.h>
#include <wordexp.h>
#include <dirent.h>
#include <gtkmm.h>
#include <gtkmm/window.h>
#include <gtkmm/image.h>
#include <gdkmm/pixbuf.h>
#include <gdk/gdkwayland.h>
#include <config.hpp>

#include <iostream>
#include <map>

#include <gtk-utils.hpp>
#include <wf-shell-app.hpp>

#include "background.hpp"

/* TODO: Get output refresh rate and compute this value
 * 1000 msecs / 60Hz */
#define BACKGROUND_FADE_TIMEOUT 16
/* TODO: Compute from user defined target fade time and output refresh rate
 * 1.0 / (USER_DEFINED_TARGET_FADE_TIME / BACKGROUND_FADE_TIMEOUT) */
#define ALPHA_STEP 0.015625


Glib::RefPtr<Gdk::Pixbuf>
create_from_file_safe(std::string path)
{
    Glib::RefPtr<Gdk::Pixbuf> pbuf;
    try
    {
        pbuf = Gdk::Pixbuf::create_from_file(path);
        return pbuf;
    }
    catch (...)
    {
        return pbuf;
    }
}

bool
BackgroundDrawingArea::on_draw(const Cairo::RefPtr<Cairo::Context>& cr)
{
    auto pbuf = background->pbuf;
    auto pbuf2 = background->pbuf2;
    auto alpha = background->alpha;

    Gdk::Cairo::set_source_pixbuf(cr, pbuf, 0, 0);
    cr->rectangle(0, 0, pbuf->get_width(), pbuf->get_height());
    cr->paint_with_alpha(alpha);

    if (!pbuf2)
        return true;

    Gdk::Cairo::set_source_pixbuf(cr, pbuf2, 0, 0);
    cr->rectangle(0, 0, pbuf2->get_width(), pbuf2->get_height());
    cr->paint_with_alpha(1.0 - alpha);

    return true;
}

BackgroundDrawingArea::BackgroundDrawingArea(WayfireBackground *background)
{
    this->background = background;
}

void
WayfireBackground::create_wm_surface()
{
    auto gdk_window = window.get_window()->gobj();
    auto surface = gdk_wayland_window_get_wl_surface(gdk_window);

    if (!surface)
    {
        std::cerr << "Error: created window was not a wayland surface" << std::endl;
        std::exit(-1);
    }

    wm_surface = zwf_shell_manager_v1_get_wm_surface(
        output->display->zwf_shell_manager, surface,
        ZWF_WM_SURFACE_V1_ROLE_BACKGROUND, output->handle);
    zwf_wm_surface_v1_configure(wm_surface, 0, 0);
}

void
WayfireBackground::handle_output_resize(uint32_t width, uint32_t height)
{
    output_width = width;
    output_height = height;

    window.set_size_request(width, height);
    window.show_all();

    if (!wm_surface)
        create_wm_surface();

    set_background();
}

bool
WayfireBackground::background_transition_frame(int timer)
{
    alpha += ALPHA_STEP;

    if (alpha > 1.0)
    {
        alpha = 0.0;
        return false;
    }

    auto gdk_window = window.get_window();
    Gdk::Rectangle r(0, 0, window.get_allocation().get_width(),
        window.get_allocation().get_height());
    gdk_window->invalidate_rect(r, false);

    return true;
}

bool
WayfireBackground::change_background(int timer)
{
    if (alpha != 0.0)
        return true;

    background[1] = background[0];

    if (++background[0] > images.size() - 1)
        background[0] = 0;

    auto path = images[background[0]];
    pbuf = create_from_file_safe(path);

    if (!load_next_background(path, background[1]))
        return false;

    std::cout << "Loaded " << path << std::endl;

    pbuf2 = create_from_file_safe(images[background[1]]);

    pbuf = pbuf->scale_simple(output_width * scale,
                              output_height * scale,
                              Gdk::INTERP_BILINEAR);

    pbuf2 = pbuf2->scale_simple(output_width * scale,
                              output_height * scale,
                              Gdk::INTERP_BILINEAR);

    frame_handler_conn = Glib::signal_timeout().connect(sigc::bind(sigc::mem_fun(this,
        &WayfireBackground::background_transition_frame), 1), BACKGROUND_FADE_TIMEOUT,
        Glib::PRIORITY_HIGH_IDLE + 20);

    return true;
}

void
WayfireBackground::load_image(std::string path)
{
    images.push_back(path);
}

bool
WayfireBackground::load_images_from_dir(std::string path)
{
    wordexp_t exp;

    /* Expand path */
    wordexp(path.c_str(), &exp, 0);
    auto dir = opendir(exp.we_wordv[0]);
    if (!dir)
        return false;

    /* Iterate over all files in the directory */
    dirent *file;
    while ((file = readdir(dir)) != 0)
    {
        /* Skip hidden files and folders */
        if (file->d_name[0] == '.')
            continue;

        auto fullpath = std::string(exp.we_wordv[0]) + "/" + file->d_name;

        struct stat next;
        if (stat(fullpath.c_str(), &next) == 0)
        {
            if (S_ISDIR(next.st_mode))
                /* Recursive search */
                load_images_from_dir(fullpath);
            else
                load_image(fullpath);
        }
    }
    return true;
}

bool
WayfireBackground::load_next_background(std::string &path, uint current)
{
    while (!pbuf)
    {
        if (++background[0] > images.size() - 1)
            background[0] = 0;

        if (background[0] == current)
        {
            std::cerr << "Failed to load background images from " << background_image->as_string() << std::endl;
            window.remove();
            return false;
        }

        path = images[background[0]];
        pbuf = create_from_file_safe(path);

        if (!pbuf)
            std::cerr << "Failed to load " << path << std::endl;
    }

    return true;
}

void
WayfireBackground::reset_background()
{
    alpha = 0.0;
    images.clear();
    change_bg_conn.disconnect();
    frame_handler_conn.disconnect();
    background[0] = background[1] = 0;
    pbuf.clear();
    pbuf2.clear();
}

void
WayfireBackground::set_background()
{
    reset_background();
    auto path = background_image->as_string();
    cycle_timeout = background_cycle_timeout->as_int() * 1000;
    try {
        if (load_images_from_dir(path) && images.size())
        {
            if (!load_next_background(path, 0))
                throw std::exception();

            std::cout << "Loaded " << path << std::endl;
        }
        else
        {
            pbuf = create_from_file_safe(path);
            if (!pbuf)
                throw std::exception();
        }

        frame_handler_conn = Glib::signal_timeout().connect(sigc::bind(sigc::mem_fun(this,
            &WayfireBackground::background_transition_frame), 1), BACKGROUND_FADE_TIMEOUT,
            Glib::PRIORITY_HIGH_IDLE + 20);

        scale = window.get_scale_factor();
        pbuf = pbuf->scale_simple(output_width * scale,
                                  output_height * scale,
                                  Gdk::INTERP_BILINEAR);

        if (!drawing_area.get_parent())
        {
            window.add(drawing_area);
            window.show_all();
        }
        if (images.size())
        {
            change_bg_conn = Glib::signal_timeout().connect(sigc::bind(sigc::mem_fun(
                this, &WayfireBackground::change_background), 0), cycle_timeout);
        }
    } catch (...)
    {
        window.remove();
        if (images.size())
            std::cerr << "Failed to load background images from " << path << std::endl;
        else if (path != "none")
            std::cerr << "Failed to load background image " << path << std::endl;
    }

    if (inhibited)
    {
        zwf_output_v1_inhibit_output_done(output->zwf);
        inhibited = false;
    }
}

void
WayfireBackground::reset_cycle_timeout()
{
    cycle_timeout = background_cycle_timeout->as_int() * 1000;
    change_bg_conn.disconnect();
    if (images.size())
    {
        change_bg_conn = Glib::signal_timeout().connect(sigc::bind(sigc::mem_fun(
            this, &WayfireBackground::change_background), 0), cycle_timeout);
    }
}

void
WayfireBackground::setup_window()
{
    window.set_resizable(false);
    window.set_decorated(false);

    background_image = app->config->get_section("background")
        ->get_option("image", "none");
    background_cycle_timeout = app->config->get_section("background")
        ->get_option("cycle_timeout", "150");
    image_updated = [=] () { set_background(); };
    cycle_timeout_updated = [=] () { reset_cycle_timeout(); };
    background_image->add_updated_handler(&image_updated);
    background_cycle_timeout->add_updated_handler(&cycle_timeout_updated);

    window.property_scale_factor().signal_changed().connect(
        sigc::mem_fun(this, &WayfireBackground::set_background));
}

WayfireBackground::WayfireBackground(WayfireShellApp *app, WayfireOutput *output)
    : drawing_area(this)
{
    this->app = app;
    this->output = output;

    zwf_output_v1_inhibit_output(output->zwf);
    setup_window();
    output->resized_callback = [=] (WayfireOutput*, uint32_t w, uint32_t h)
    {
        std::cout << "handle resize" << std::endl;
        handle_output_resize(w, h);
    };
}

WayfireBackground::~WayfireBackground()
{
    background_image->rem_updated_handler(&image_updated);
}

class WayfireBackgroundApp : public WayfireShellApp
{
    std::map<WayfireOutput*, std::unique_ptr<WayfireBackground> > backgrounds;

    public:
    WayfireBackgroundApp(int argc, char **argv)
        : WayfireShellApp(argc, argv)
    {
    }

    void on_new_output(WayfireOutput *output)
    {
        backgrounds[output] = std::unique_ptr<WayfireBackground> (
            new WayfireBackground(this, output));
    }

    void on_output_removed(WayfireOutput *output)
    {
        backgrounds.erase(output);
    }
};

int main(int argc, char **argv)
{
    WayfireBackgroundApp background_app(argc, argv);
    background_app.run();
    return 0;
}
