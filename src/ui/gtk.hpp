/****************************************************************************
*    Copyright © 2014-2025 The Tumultuous Unicorn Of Darkness
*    Copyright © 2026 CPU-X fork contributors
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
****************************************************************************/

/*
* PROJECT CPU-X
* FILE ui/gtk.hpp
*
* GTK4 (gtkmm-4.0) graphical frontend.
*
* The widget tree is built in C++ rather than from a GtkBuilder file. Each
* displayed value is bound through a getter that resolves the *currently
* selected* core type, memory stick or graphics card at refresh time, so the
* shared Data model never stores toolkit objects. Another frontend can follow
* the same pattern without touching the backend.
*/

#ifndef _UI_GTK_HPP_
#define _UI_GTK_HPP_

#include <functional>
#include <memory>
#include <vector>
#include <gtkmm.h>
#include "data.hpp"

#define APPLICATION_ID "io.github." PRGUSERNAME "." PRGNAME_LOW // https://developer.gnome.org/documentation/tutorials/application-id.html#guidelines-for-choosing-an-application-id

enum GtkTheme
{
	AUTO,
	LIGHT,
	DARK
};

namespace cpux_gtk
{

using LabelGetter = std::function<const Label*()>;
using FrameGetter = std::function<const ::Frame*()>;

/* A titled group of name/value rows laid out on a grid.
 * Rows are either full width or paired (two name/value pairs per line). */
class InfoFrame : public Gtk::Frame
{
public:
	explicit InfoFrame(FrameGetter title);
	void add_row(LabelGetter getter, const char *tooltip = nullptr);
	void add_pair(LabelGetter left, LabelGetter right, const char *left_tooltip = nullptr, const char *right_tooltip = nullptr);
	/* Add an arbitrary value widget (switch, progress bar…) next to a name */
	Gtk::Label *add_widget_row(Gtk::Widget &widget, bool expand_value = true);
	void add_full_width(Gtk::Widget &widget);
	void refresh();

private:
	struct Binding
	{
		LabelGetter getter;
		const char *tooltip;
		Gtk::Label *name;
		Gtk::Label *value;
	};
	Binding make_binding(LabelGetter getter, const char *tooltip, int column, int row, int span);

	FrameGetter title;
	Gtk::Grid grid;
	int next_row = 0;
	std::vector<Binding> bindings;
};

/* A drop-down list of strings whose button ellipsizes long entries while the
 * popup shows them in full. */
class Selector : public Gtk::DropDown
{
public:
	Selector();
	void set_items(const std::vector<std::string> &items, unsigned selected);
	int get_selected_index() const;
	sigc::signal<void(int)> signal_index_changed;

private:
	Glib::RefPtr<Gtk::StringList> list;
	bool updating = false;
};

class SettingsWindow;

class MainWindow : public Gtk::ApplicationWindow
{
public:
	MainWindow(Data &data);
	~MainWindow() override;
	void refresh_current_page();
	void reload_preferences();
	void apply_theme();
	bool is_dark() const;

	Data &data;

private:
	Gtk::Widget &build_cpu_page();
	Gtk::Widget &build_caches_page();
	Gtk::Widget &build_motherboard_page();
	Gtk::Widget &build_memory_page();
	Gtk::Widget &build_system_page();
	Gtk::Widget &build_graphics_page();
	Gtk::Widget &build_bench_page();
	Gtk::Widget &build_about_page();
	Gtk::Widget &build_daemon_banner();
	Gtk::Widget &build_footer();
	void add_page(Gtk::Widget &content, const Tab &tab, TabNumber number);
	void refresh_page(TabNumber page);
	void refresh_all_pages();
	void set_cpu_logo();
	void draw_memory_bar(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height, int index);
	void on_bench_toggled(bool fast, bool state);
	void update_bench_controls();
	void copy_report();
	void start_refresh_timer();

	Gtk::Box root {Gtk::Orientation::VERTICAL};
	Gtk::Notebook notebook;
	std::vector<std::pair<TabNumber, Gtk::Widget*>> pages;
	std::vector<std::pair<TabNumber, InfoFrame*>> frames;
	std::vector<std::pair<TabNumber, std::function<void()>>> extra_refresh;
	sigc::connection refresh_handle;
	std::unique_ptr<SettingsWindow> settings_window;

	/* Daemon banner */
	Gtk::Revealer *daemon_revealer = nullptr;
	Gtk::Button *daemon_button = nullptr;

	/* CPU */
	Gtk::Image *cpu_logo = nullptr;
	Selector *cpu_type_selector = nullptr;
	Selector *cpu_core_selector = nullptr;

	/* Caches / Memory / Graphics selectors */
	Selector *cache_test_selector = nullptr;
	Selector *stick_selector = nullptr;
	Selector *card_selector = nullptr;

	/* System memory bars */
	std::vector<Gtk::DrawingArea*> memory_bars;

	/* Bench */
	Gtk::Switch *bench_slow_switch = nullptr;
	Gtk::Switch *bench_fast_switch = nullptr;
	Gtk::ProgressBar *bench_slow_bar = nullptr;
	Gtk::ProgressBar *bench_fast_bar = nullptr;
	Gtk::SpinButton *bench_duration = nullptr;
	Gtk::SpinButton *bench_threads = nullptr;
	bool bench_updating = false;
};

} // namespace cpux_gtk

#endif /* _UI_GTK_HPP_ */
