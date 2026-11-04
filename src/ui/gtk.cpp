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
* FILE ui/gtk.cpp
*/

/* gtkmm must come before project headers: util.hpp defines macros (e.g. DEFAULT)
 * that collide with glibmm enumerators. */
#include <gtkmm.h>
#include <giomm/settings.h>
#include <giomm/settingsschemasource.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cmath>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include "options.hpp"
#include "util.hpp"
#include "data.hpp"
#include "core/core.hpp"
#include "daemon/daemon.h"
#include "daemon/client.hpp"
#include "ui/gtk.hpp"

#if HAS_LIBCPUID
# include <libcpuid/libcpuid.h>
#endif

using namespace cpux_gtk;


/************************* Private helpers *************************/

/* GSettings is optional at runtime: an uninstalled schema (e.g. running from
 * the build tree) must not abort the program, defaults are used instead. */
static Glib::RefPtr<Gio::Settings> settings;

/* Theme-agnostic styling: only spacing and borders derived from the current
 * foreground colour, so every GTK theme (light, dark, high contrast) stays legible. */
static const char *APP_CSS = R"CSS(
.cpux-value {
	padding: 1px 6px;
	border-radius: 4px;
	border: 1px solid alpha(currentColor, 0.18);
	background-color: alpha(currentColor, 0.04);
}
.cpux-name {
	opacity: 0.85;
}
frame > label {
	font-weight: bold;
}
.cpux-banner {
	padding: 6px 12px;
	background-color: alpha(#e5a50a, 0.25);
}
.cpux-footer {
	padding: 4px 8px;
}
.cpux-about-title {
	font-size: 150%;
	font-weight: bold;
}
)CSS";

static inline bool check_data_path(const std::string &file, const std::string &full_path)
{
	const bool found = Glib::file_test(full_path, Glib::FileTest::EXISTS);
	MSG_DEBUG("check_data_path: file=%s ==> %s %s", file.c_str(), full_path.c_str(), found ? "found" : "does not exist");
	return found;
}

/* Search file location in standard paths */
static std::string get_data_path(const std::string &file)
{
	std::string full_path;

#if not defined (APPIMAGE) && not defined (FLATPAK)
	full_path = std::string(CPUX_DATA_DIRECTORY) + "/" + file;
	if(check_data_path(file, full_path))
		return full_path;
#endif

	for(const auto& dir : Glib::get_system_data_dirs())
	{
		full_path = dir + "/" + PRGNAME_LOW + "/" + file;
		if(check_data_path(file, full_path))
			return full_path;
	}

	MSG_WARNING(_("Cannot find path for '%s' file"), file.c_str());
	return std::string();
}

static std::string tr(const char *msg)
{
	return msg ? _(msg) : std::string();
}

static Gtk::Label *make_name_label()
{
	auto *label = Gtk::make_managed<Gtk::Label>();
	label->set_xalign(0.0);
	label->set_ellipsize(Pango::EllipsizeMode::END);
	label->add_css_class("cpux-name");
	return label;
}

static Gtk::Label *make_value_label()
{
	auto *label = Gtk::make_managed<Gtk::Label>();
	label->set_xalign(0.0);
	label->set_hexpand(true);
	label->set_selectable(true);
	label->set_ellipsize(Pango::EllipsizeMode::END);
	label->set_width_chars(4);
	label->add_css_class("cpux-value");
	return label;
}

static Gtk::ScrolledWindow *make_scrolled(Gtk::Widget &child)
{
	auto *scrolled = Gtk::make_managed<Gtk::ScrolledWindow>();
	scrolled->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
	scrolled->set_propagate_natural_height(true);
	scrolled->set_propagate_natural_width(true);
	scrolled->set_child(child);
	return scrolled;
}

static Gtk::Box *make_page_box()
{
	auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
	box->set_margin(8);
	return box;
}

static GtkTheme get_theme_setting()
{
	return settings ? static_cast<GtkTheme>(settings->get_enum("gui-theme")) : AUTO;
}


/************************* InfoFrame *************************/

InfoFrame::InfoFrame(FrameGetter title) : title(std::move(title))
{
	this->grid.set_row_spacing(4);
	this->grid.set_column_spacing(8);
	this->grid.set_margin(6);
	this->set_child(this->grid);
	this->set_hexpand(true);
}

InfoFrame::Binding InfoFrame::make_binding(LabelGetter getter, const char *tooltip, int column, int row, int span)
{
	Binding binding{std::move(getter), tooltip, make_name_label(), make_value_label()};
	this->grid.attach(*binding.name, column, row);
	this->grid.attach(*binding.value, column + 1, row, span);
	return binding;
}

void InfoFrame::add_row(LabelGetter getter, const char *tooltip)
{
	this->bindings.push_back(make_binding(std::move(getter), tooltip, 0, this->next_row++, 3));
}

void InfoFrame::add_pair(LabelGetter left, LabelGetter right, const char *left_tooltip, const char *right_tooltip)
{
	this->bindings.push_back(make_binding(std::move(left), left_tooltip, 0, this->next_row, 1));
	this->bindings.push_back(make_binding(std::move(right), right_tooltip, 2, this->next_row, 1));
	this->next_row++;
}

Gtk::Label *InfoFrame::add_widget_row(Gtk::Widget &widget, bool expand_value)
{
	auto *name = make_name_label();
	widget.set_hexpand(expand_value);
	if(!expand_value)
		widget.set_halign(Gtk::Align::START);
	widget.set_valign(Gtk::Align::CENTER);
	this->grid.attach(*name, 0, this->next_row);
	this->grid.attach(widget, 1, this->next_row, 3);
	this->next_row++;
	return name;
}

void InfoFrame::add_full_width(Gtk::Widget &widget)
{
	this->grid.attach(widget, 0, this->next_row++, 4);
}

void InfoFrame::refresh()
{
	const ::Frame *frame = this->title ? this->title() : nullptr;
	if(this->title && frame == nullptr)
	{
		this->set_visible(false);
		return;
	}
	this->set_visible(true);
	if(frame)
		this->set_label(frame->name);

	for(auto &binding : this->bindings)
	{
		const Label *label = binding.getter();
		binding.name->set_visible(label != nullptr);
		binding.value->set_visible(label != nullptr);
		if(label == nullptr)
			continue;
		binding.name->set_text(label->name);
		binding.name->set_tooltip_text(binding.tooltip ? tr(binding.tooltip) : label->name);
		/* Avoid resetting text (and the user's selection) when nothing changed */
		if(binding.value->get_text().raw() != label->value)
			binding.value->set_text(label->value);
		binding.value->set_tooltip_text(label->value);
		binding.value->set_has_tooltip(!label->value.empty());
	}
}


/************************* Selector *************************/

Selector::Selector()
{
	this->list = Gtk::StringList::create({});
	this->set_model(this->list);
	this->set_hexpand(true);

	/* Button: ellipsized so long names never force a wide window */
	auto factory = Gtk::SignalListItemFactory::create();
	factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem> &item)
	{
		auto *label = Gtk::make_managed<Gtk::Label>();
		label->set_xalign(0.0);
		label->set_ellipsize(Pango::EllipsizeMode::END);
		label->set_width_chars(6);
		item->set_child(*label);
	});
	factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem> &item)
	{
		auto *label = dynamic_cast<Gtk::Label*>(item->get_child());
		auto string = std::dynamic_pointer_cast<Gtk::StringObject>(item->get_item());
		if(label && string)
		{
			label->set_text(string->get_string());
			label->set_tooltip_text(string->get_string());
		}
	});
	this->set_factory(factory);

	/* Popup: full text */
	auto list_factory = Gtk::SignalListItemFactory::create();
	list_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem> &item)
	{
		auto *label = Gtk::make_managed<Gtk::Label>();
		label->set_xalign(0.0);
		item->set_child(*label);
	});
	list_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem> &item)
	{
		auto *label = dynamic_cast<Gtk::Label*>(item->get_child());
		auto string = std::dynamic_pointer_cast<Gtk::StringObject>(item->get_item());
		if(label && string)
			label->set_text(string->get_string());
	});
	this->set_list_factory(list_factory);

	this->property_selected().signal_changed().connect([this]
	{
		if(!this->updating)
			this->signal_index_changed.emit(this->get_selected_index());
	});
}

void Selector::set_items(const std::vector<std::string> &items, unsigned selected)
{
	this->updating = true;
	std::vector<Glib::ustring> strings(items.begin(), items.end());
	this->list->splice(0, this->list->get_n_items(), strings);
	if(!items.empty())
		this->set_selected(selected < items.size() ? selected : 0);
	this->set_sensitive(!items.empty());
	this->updating = false;
}

int Selector::get_selected_index() const
{
	const auto selected = this->get_selected();
	return (selected == GTK_INVALID_LIST_POSITION) ? -1 : int(selected);
}


/************************* Settings window *************************/

namespace cpux_gtk
{

class SettingsWindow : public Gtk::Window
{
public:
	SettingsWindow(MainWindow &parent);
	void load();

private:
	void save();
	void fill_cores();
	Gtk::Label *add_row(const char *name, Gtk::Widget &widget);

	MainWindow &parent;
	Gtk::Grid grid;
	int row = 0;
	Selector temperature_unit, theme, default_tab, default_type, default_core, default_test, default_stick, default_card;
	Gtk::SpinButton refresh_time;
	Gtk::CheckButton cpuid_decimal, start_daemon_check;
};

} // namespace cpux_gtk

Gtk::Label *SettingsWindow::add_row(const char *name, Gtk::Widget &widget)
{
	auto *label = Gtk::make_managed<Gtk::Label>(_(name));
	label->set_xalign(0.0);
	widget.set_hexpand(true);
	this->grid.attach(*label, 0, this->row);
	this->grid.attach(widget, 1, this->row);
	this->row++;
	return label;
}

SettingsWindow::SettingsWindow(MainWindow &parent) : parent(parent)
{
	Data &data = parent.data;

	this->set_title(_("Settings"));
	this->set_transient_for(parent);
	this->set_modal(true);
	this->set_hide_on_close(true);

	auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
	box->set_margin(12);
	this->grid.set_row_spacing(6);
	this->grid.set_column_spacing(12);
	box->append(this->grid);

	this->refresh_time.set_range(1, G_MAXUSHORT);
	this->refresh_time.set_increments(1, 60);
	this->refresh_time.set_numeric(true);

	this->temperature_unit.set_items({ "Celsius (°C)", "Fahrenheit (°F)", "Kelvin (K)", "Rankine (Ra)" }, 0);
	this->theme.set_items({ _("Automatic"), _("Light"), _("Dark") }, 0);
	this->default_tab.set_items({ data.cpu.name, data.caches.name, data.motherboard.name, data.memory.name,
	                              data.system.name, data.graphics.name, data.bench.name, data.about.name }, 0);

	std::vector<std::string> types, tests, sticks, cards;
	for(auto &type : data.cpu.cpu_types)
		types.push_back(type.name);
	tests = data.caches.test.names;
	for(unsigned i = 0; i < data.memory.sticks.size(); i++)
		sticks.push_back(data.memory.get_stick_formatted(i));
	for(unsigned i = 0; i < data.graphics.cards.size(); i++)
		cards.push_back(data.graphics.get_card_formatted(i));
	this->default_type.set_items(types, 0);
	this->default_test.set_items(tests, 0);
	this->default_stick.set_items(sticks, 0);
	this->default_card.set_items(cards, 0);
	this->default_type.signal_index_changed.connect([this](int) { this->fill_cores(); });

	auto *interface_label = Gtk::make_managed<Gtk::Label>();
	interface_label->set_markup("<b>" + Glib::Markup::escape_text(_("Interface")) + "</b>");
	interface_label->set_xalign(0.0);
	this->grid.attach(*interface_label, 0, this->row++, 2);
	this->add_row(N_("Temperature unit"), this->temperature_unit);
	this->add_row(N_("Refresh time"), this->refresh_time);
	this->add_row(N_("Theme"), this->theme);

	auto *startup_label = Gtk::make_managed<Gtk::Label>();
	startup_label->set_markup("<b>" + Glib::Markup::escape_text(_("Startup")) + "</b>");
	startup_label->set_xalign(0.0);
	startup_label->set_margin_top(6);
	this->grid.attach(*startup_label, 0, this->row++, 2);
	this->add_row(N_("Default tab"), this->default_tab);
	this->add_row(N_("Default core type"), this->default_type);
	this->add_row(N_("Default CPU core"), this->default_core);
	this->add_row(N_("Default cache test"), this->default_test);
	this->add_row(N_("Default memory stick"), this->default_stick);
	this->add_row(N_("Default graphic card"), this->default_card);
	this->cpuid_decimal.set_label(_("Print CPUID values in decimal"));
	this->start_daemon_check.set_label(_("Start and connect to daemon"));
	this->grid.attach(this->cpuid_decimal, 0, this->row++, 2);
	this->grid.attach(this->start_daemon_check, 0, this->row++, 2);

	auto *buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
	buttons->set_halign(Gtk::Align::END);
	auto *cancel = Gtk::make_managed<Gtk::Button>(_("Cancel"));
	auto *apply  = Gtk::make_managed<Gtk::Button>(_("Apply"));
	apply->add_css_class("suggested-action");
	buttons->append(*cancel);
	buttons->append(*apply);
	box->append(*buttons);
	this->set_child(*box);

	cancel->signal_clicked().connect([this] { this->set_visible(false); });
	apply->signal_clicked().connect([this]
	{
		this->save();
		this->set_visible(false);
	});
	this->set_default_widget(*apply);
}

void SettingsWindow::fill_cores()
{
	auto &cpu_types = this->parent.data.cpu.cpu_types;
	const int type = this->default_type.get_selected_index();
	std::vector<std::string> cores;
	if(type >= 0 && unsigned(type) < cpu_types.size())
		for(uint16_t i = 0; i < cpu_types[type].footer.num_threads; i++)
			cores.push_back(cpu_types[type].get_core_formatted(i));
	this->default_core.set_items(cores, settings ? settings->get_uint("default-cpu-core") : 0);
}

void SettingsWindow::load()
{
	if(!settings)
		return;
	settings->revert();
	this->temperature_unit.set_selected(settings->get_enum("temperature-unit"));
	this->refresh_time.set_value(settings->get_uint("refresh-time"));
	this->theme.set_selected(settings->get_enum("gui-theme"));
	this->default_tab.set_selected(settings->get_enum("default-tab"));
	if(this->default_type.get_model()->get_n_items() > settings->get_uint("default-core-type"))
		this->default_type.set_selected(settings->get_uint("default-core-type"));
	this->fill_cores();
	if(this->default_test.get_model()->get_n_items() > settings->get_uint("default-cache-test"))
		this->default_test.set_selected(settings->get_uint("default-cache-test"));
	if(this->default_stick.get_model()->get_n_items() > settings->get_uint("default-memory-stick"))
		this->default_stick.set_selected(settings->get_uint("default-memory-stick"));
	if(this->default_card.get_model()->get_n_items() > settings->get_uint("default-active-card"))
		this->default_card.set_selected(settings->get_uint("default-active-card"));
	this->cpuid_decimal.set_active(settings->get_boolean("print-cpuid-decimal"));
	this->start_daemon_check.set_active(settings->get_boolean("always-start-daemon"));
}

void SettingsWindow::save()
{
	if(!settings)
		return;
	auto set_uint_if_valid = [](const char *key, int value) { if(value >= 0) settings->set_uint(key, value); };

	settings->set_enum("temperature-unit", this->temperature_unit.get_selected_index());
	settings->set_uint("refresh-time", this->refresh_time.get_value_as_int());
	settings->set_enum("gui-theme", this->theme.get_selected_index());
	settings->set_enum("default-tab", this->default_tab.get_selected_index());
	set_uint_if_valid("default-core-type", this->default_type.get_selected_index());
	set_uint_if_valid("default-cpu-core", this->default_core.get_selected_index());
	set_uint_if_valid("default-cache-test", this->default_test.get_selected_index());
	set_uint_if_valid("default-memory-stick", this->default_stick.get_selected_index());
	set_uint_if_valid("default-active-card", this->default_card.get_selected_index());
	settings->set_boolean("print-cpuid-decimal", this->cpuid_decimal.get_active());
	settings->set_boolean("always-start-daemon", this->start_daemon_check.get_active());
	settings->apply();
	this->parent.reload_preferences();
}


/************************* Main window *************************/

MainWindow::MainWindow(Data &data) : data(data)
{
	this->set_title(PRGNAME);
	this->set_icon_name(APPLICATION_ID);

	auto provider = Gtk::CssProvider::create();
	provider->load_from_data(APP_CSS);
	Gtk::StyleContext::add_provider_for_display(Gdk::Display::get_default(), provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	this->apply_theme();

	this->notebook.set_scrollable(true);
	this->notebook.set_vexpand(true);
	this->add_page(this->build_cpu_page(),         data.cpu,         TAB_CPU);
	this->add_page(this->build_caches_page(),      data.caches,      TAB_CACHES);
	this->add_page(this->build_motherboard_page(), data.motherboard, TAB_MOTHERBOARD);
	this->add_page(this->build_memory_page(),      data.memory,      TAB_MEMORY);
	this->add_page(this->build_system_page(),      data.system,      TAB_SYSTEM);
	this->add_page(this->build_graphics_page(),    data.graphics,    TAB_GRAPHICS);
	this->add_page(this->build_bench_page(),       data.bench,       TAB_BENCH);
	this->add_page(this->build_about_page(),       data.about,       TAB_ABOUT);

	this->root.append(this->build_daemon_banner());
	this->root.append(this->notebook);
	this->root.append(this->build_footer());
	this->set_child(this->root);

	this->settings_window = std::make_unique<SettingsWindow>(*this);
	this->refresh_all_pages();
	this->set_cpu_logo();

	this->notebook.set_current_page(Options::get_selected_page());
	this->notebook.signal_switch_page().connect([this](Gtk::Widget*, guint page_number)
	{
		Options::set_selected_page(static_cast<TabNumber>(page_number));
		this->refresh_page(static_cast<TabNumber>(page_number));
	});
	this->start_refresh_timer();

	/* Do not start with the first selectable value label focused (and its text selected) */
	this->signal_show().connect([this] { this->notebook.grab_focus(); });

	/* Keyboard shortcuts: Ctrl+Q quits, Ctrl+Shift+C copies the full report */
	auto shortcuts = Gtk::ShortcutController::create();
	shortcuts->set_scope(Gtk::ShortcutScope::GLOBAL);
	shortcuts->add_shortcut(Gtk::Shortcut::create(Gtk::ShortcutTrigger::parse_string("<Control>q"),
		Gtk::CallbackAction::create([this](Gtk::Widget&, const Glib::VariantBase&) { this->close(); return true; })));
	shortcuts->add_shortcut(Gtk::Shortcut::create(Gtk::ShortcutTrigger::parse_string("<Control><Shift>c"),
		Gtk::CallbackAction::create([this](Gtk::Widget&, const Glib::VariantBase&) { this->copy_report(); return true; })));
	this->add_controller(shortcuts);
}

MainWindow::~MainWindow()
{
	this->refresh_handle.disconnect();
	this->data.bench.is_running = false;
}

void MainWindow::add_page(Gtk::Widget &content, const Tab &tab, TabNumber number)
{
	auto *tab_label = Gtk::make_managed<Gtk::Label>(tab.name);
	this->notebook.append_page(content, *tab_label);
	content.set_visible(Options::get_page_visibility(number));
	this->pages.emplace_back(number, &content);
}

bool MainWindow::is_dark() const
{
	switch(get_theme_setting())
	{
		case LIGHT: return false;
		case DARK:  return true;
		default:    break;
	}
	/* Light text means a dark theme */
	const auto fg = this->get_color();
	return (0.2126 * fg.get_red() + 0.7152 * fg.get_green() + 0.0722 * fg.get_blue()) > 0.5;
}

void MainWindow::apply_theme()
{
	static std::optional<bool> system_prefers_dark;
	auto gtk_settings = Gtk::Settings::get_default();
	if(!gtk_settings)
		return;
	if(!system_prefers_dark)
		system_prefers_dark = gtk_settings->property_gtk_application_prefer_dark_theme().get_value();

	switch(get_theme_setting())
	{
		case LIGHT: gtk_settings->property_gtk_application_prefer_dark_theme() = false; break;
		case DARK:  gtk_settings->property_gtk_application_prefer_dark_theme() = true;  break;
		default:    gtk_settings->property_gtk_application_prefer_dark_theme() = *system_prefers_dark; break;
	}
	for(auto *bar : this->memory_bars)
		bar->queue_draw();
}

void MainWindow::reload_preferences()
{
	if(!settings)
		return;
	Options::set_temp_unit(static_cast<OptTempUnit>(settings->get_enum("temperature-unit")));
	Options::set_refr_time(settings->get_uint("refresh-time"));
	Options::set_cpuid_decimal(settings->get_boolean("print-cpuid-decimal"));
	this->start_refresh_timer();
	this->apply_theme();
}

void MainWindow::start_refresh_timer()
{
	this->refresh_handle.disconnect();
	this->refresh_handle = Glib::signal_timeout().connect_seconds([this]
	{
		this->refresh_current_page();
		return true;
	}, Options::get_refr_time());
}

void MainWindow::refresh_current_page()
{
	const auto page = Options::get_selected_page();
	switch(page)
	{
		case TAB_CPU:
		case TAB_CACHES:
		case TAB_SYSTEM:
		case TAB_GRAPHICS:
		case TAB_BENCH:
			do_refresh(this->data, page);
			break;
		default:
			break;
	}
	this->refresh_page(page);
}

void MainWindow::refresh_page(TabNumber page)
{
	for(auto &[number, frame] : this->frames)
		if(number == page)
			frame->refresh();
	for(auto &[number, callback] : this->extra_refresh)
		if(number == page)
			callback();
}

void MainWindow::refresh_all_pages()
{
	for(auto &[number, frame] : this->frames)
		frame->refresh();
	for(auto &[number, callback] : this->extra_refresh)
		callback();
}

void MainWindow::copy_report()
{
	std::ostringstream report;
	const bool color = Options::get_color();
	Options::set_color(false);
	report << this->data;
	Options::set_color(color);
	this->get_clipboard()->set_text(report.str());
}


/* Common widgets */

Gtk::Widget &MainWindow::build_daemon_banner()
{
	auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
	box->add_css_class("cpux-banner");
	auto *icon = Gtk::make_managed<Gtk::Image>();
	icon->set_from_icon_name("dialog-warning-symbolic");
	auto *label = Gtk::make_managed<Gtk::Label>(_("Privileges are required to gather some information"));
	label->set_wrap(true);
	label->set_xalign(0.0);
	label->set_hexpand(true);
	this->daemon_button = Gtk::make_managed<Gtk::Button>(_("Start daemon"));
	auto *close = Gtk::make_managed<Gtk::Button>();
	close->set_icon_name("window-close-symbolic");
	close->set_has_frame(false);
	close->set_tooltip_text(_("Close"));
	box->append(*icon);
	box->append(*label);
	box->append(*this->daemon_button);
	box->append(*close);

	this->daemon_revealer = Gtk::make_managed<Gtk::Revealer>();
	this->daemon_revealer->set_child(*box);
	this->daemon_revealer->set_reveal_child(!DAEMON_UP);

#ifndef FLATPAK
	const int pkcheck = run_command("pkcheck --action-id org.freedesktop.policykit.exec --process %u > /dev/null 2>&1", getpid());
	if(WEXITSTATUS(pkcheck) > 2)
	{
		this->daemon_button->set_sensitive(false);
		this->daemon_button->set_tooltip_text(_("No polkit authentication agent found"));
	}
	else
#endif /* !FLATPAK */
		this->daemon_button->set_tooltip_text(_("Ask password to start daemon in background"));

	close->signal_clicked().connect([this] { this->daemon_revealer->set_reveal_child(false); });
	this->daemon_button->signal_clicked().connect([this]
	{
		this->daemon_button->set_sensitive(false);
		const char *msg = start_daemon(true);
		if(msg == nullptr)
		{
			this->data.reload = true;
			this->get_application()->quit();
		}
		else
		{
			auto dialog = Gtk::AlertDialog::create(_(msg));
			dialog->show(*this);
			this->daemon_button->set_sensitive(true);
		}
	});

	return *this->daemon_revealer;
}

Gtk::Widget &MainWindow::build_footer()
{
	auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
	box->add_css_class("cpux-footer");
	auto *version = Gtk::make_managed<Gtk::Label>(this->data.about.about.version);
	version->set_xalign(0.0);
	version->set_hexpand(true);
	version->set_ellipsize(Pango::EllipsizeMode::END);
	version->add_css_class("dim-label");

	auto *copy = Gtk::make_managed<Gtk::Button>();
	copy->set_icon_name("edit-copy-symbolic");
	copy->set_tooltip_text(_("Copy all information to the clipboard (Ctrl+Shift+C)"));
	auto *settings_button = Gtk::make_managed<Gtk::Button>();
	settings_button->set_icon_name("preferences-system-symbolic");
	settings_button->set_tooltip_text(_("Settings"));
	settings_button->set_sensitive(bool(settings));
	auto *close = Gtk::make_managed<Gtk::Button>(_("Close"));

	box->append(*version);
	box->append(*copy);
	box->append(*settings_button);
	box->append(*close);

	copy->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::copy_report));
	settings_button->signal_clicked().connect([this]
	{
		this->settings_window->load();
		this->settings_window->present();
	});
	close->signal_clicked().connect([this] { this->close(); });
	return *box;
}


/* CPU tab */

void MainWindow::set_cpu_logo()
{
#if HAS_LIBCPUID
	static const std::unordered_map<cpu_vendor_t, std::string> logos =
	{
		{ VENDOR_INTEL,     "Intel.png"     }, { VENDOR_AMD,       "AMD.png"       },
		{ VENDOR_CYRIX,     "Cyrix.png"     }, { VENDOR_NEXGEN,    "NexGen.png"    },
		{ VENDOR_TRANSMETA, "Transmeta.png" }, { VENDOR_UMC,       "UMC.png"       },
		{ VENDOR_CENTAUR,   "Centaur.png"   }, { VENDOR_RISE,      "Rise.png"      },
		{ VENDOR_SIS,       "SiS.png"       }, { VENDOR_NSC,       "NSC.png"       },
		{ VENDOR_HYGON,     "Hygon.png"     }, { VENDOR_ARM,       "ARM.png"       },
		{ VENDOR_BROADCOM,  "Broadcom.png"  }, { VENDOR_CAVIUM,    "Cavium.png"    },
		{ VENDOR_DEC,       "DEC.png"       }, { VENDOR_FUJITSU,   "Fujitsu.png"   },
		{ VENDOR_HISILICON, "HiSilicon.png" }, { VENDOR_INFINEON,  "Infineon.png"  },
		{ VENDOR_FREESCALE, "Freescale.png" }, { VENDOR_NVIDIA,    "NVIDIA.png"    },
		{ VENDOR_QUALCOMM,  "Qualcomm.png"  }, { VENDOR_SAMSUNG,   "Samsung.png"   },
		{ VENDOR_MARVELL,   "Marvell.png"   }, { VENDOR_APPLE,     "Apple.png"     },
		{ VENDOR_FARADAY,   "Faraday.png"   }, { VENDOR_MICROSOFT, "Microsoft.png" },
		{ VENDOR_PHYTIUM,   "Phytium.png"   }, { VENDOR_AMPERE,    "Ampere.png"    },
		{ VENDOR_UNKNOWN,   "Unknown.png"   }
	};
	const auto it = logos.find(this->data.cpu.vendor);
	const auto path = get_data_path(it != logos.end() ? it->second : logos.at(VENDOR_UNKNOWN));
#else
	const auto path = get_data_path("Unknown.png");
#endif /* HAS_LIBCPUID */
	if(!path.empty())
		this->cpu_logo->set(path);
	this->cpu_logo->set_visible(!path.empty());
}

Gtk::Widget &MainWindow::build_cpu_page()
{
	auto &cpu = this->data.cpu;
	auto *page = make_page_box();
	auto type = [&cpu]() -> Data::Cpu::CpuType& { return cpu.get_selected_cpu_type(); };

	/* Processor */
	auto *processor = Gtk::make_managed<InfoFrame>([type] { return &type().processor; });
	processor->add_row([type] { return &type().processor.vendor; });
	processor->add_row([type] { return &type().processor.codename; });
	processor->add_row([type] { return &type().processor.package; });
	processor->add_pair([type] { return &type().processor.technology; },
	                    [type] { return &type().processor.voltage; });
	processor->add_row([type] { return &type().processor.specification; });
#if HAS_LIBCPUID
	auto is_arm = [type] { return type().processor.architecture == ARCHITECTURE_ARM; };
	processor->add_pair([type, is_arm] { return is_arm() ? &type().processor.implementer : &type().processor.family; },
	                    [type, is_arm] { return is_arm() ? &type().processor.variant     : &type().processor.model; },
	                    N_("BaseFamily"), N_("BaseModel"));
	processor->add_pair([type, is_arm] { return is_arm() ? &type().processor.partnum  : &type().processor.dispfamily; },
	                    [type, is_arm] { return is_arm() ? &type().processor.revision : &type().processor.dispmodel; },
	                    N_("CPU display (\"true\") family (computed as BaseFamily+ExtendedFamily)"),
	                    N_("CPU display (\"true\") model (computed as (ExtendedModel<<4)+BaseModel)"));
	processor->add_pair([type, is_arm]() -> const Label* { return is_arm() ? nullptr : &type().processor.stepping; },
	                    [type] { return &type().processor.temperature; });
#else
	processor->add_row([type] { return &type().processor.temperature; });
#endif /* HAS_LIBCPUID */
	processor->add_row([type] { return &type().processor.instructions; });

	/* Gtk::Image scales the logo to a square of pixel_size, preserving its aspect ratio */
	this->cpu_logo = Gtk::make_managed<Gtk::Image>();
	this->cpu_logo->set_pixel_size(112);
	this->cpu_logo->set_valign(Gtk::Align::CENTER);
	auto *processor_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
	processor_row->append(*processor);
	processor_row->append(*this->cpu_logo);
	page->append(*processor_row);

	/* Clocks and caches, side by side */
	auto *clocks = Gtk::make_managed<InfoFrame>([&cpu] { return &cpu.clocks; });
	clocks->add_row([&cpu] { return &cpu.clocks.core_speed; });
	clocks->add_row([&cpu] { return &cpu.clocks.multiplier; });
	clocks->add_row([&cpu] { return &cpu.clocks.bus_speed; });
	clocks->add_row([&cpu] { return &cpu.clocks.usage; });
	auto *caches = Gtk::make_managed<InfoFrame>([type] { return &type().caches; });
	caches->add_row([type] { return &type().caches.level1d; });
	caches->add_row([type] { return &type().caches.level1i; });
	caches->add_row([type] { return &type().caches.level2; });
	caches->add_row([type] { return &type().caches.level3; });
	auto *middle = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
	middle->set_homogeneous(true);
	middle->append(*clocks);
	middle->append(*caches);
	page->append(*middle);

	/* Footer: core type, core, cores and threads */
	auto *footer = Gtk::make_managed<InfoFrame>(nullptr);
	this->cpu_type_selector = Gtk::make_managed<Selector>();
	this->cpu_core_selector = Gtk::make_managed<Selector>();
	auto *selectors = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
	selectors->append(*this->cpu_type_selector);
	selectors->append(*this->cpu_core_selector);
	footer->add_full_width(*selectors);
	footer->add_pair([type] { return &type().footer.cores; },
	                 [type] { return &type().footer.threads; });
	page->append(*footer);

	auto fill_cores = [this, type]
	{
		std::vector<std::string> cores;
		for(uint16_t i = 0; i < type().footer.num_threads; i++)
			cores.push_back(type().get_core_formatted(i));
		this->cpu_core_selector->set_items(cores, Options::get_selected_core());
	};
	std::vector<std::string> types;
	for(auto &cpu_type : cpu.cpu_types)
		types.push_back(cpu_type.name);
	this->cpu_type_selector->set_items(types, Options::get_selected_type());
	fill_cores();

	this->cpu_type_selector->signal_index_changed.connect([this, fill_cores](int index)
	{
		if(index < 0)
			return;
		Options::set_selected_type(index);
		fill_cores();
		this->refresh_current_page();
	});
	this->cpu_core_selector->signal_index_changed.connect([](int index)
	{
		if(index >= 0)
			Options::set_selected_core(index);
	});

	for(auto *frame : { processor, clocks, caches, footer })
		this->frames.emplace_back(TAB_CPU, frame);
	return *make_scrolled(*page);
}


/* Caches tab */

Gtk::Widget &MainWindow::build_caches_page()
{
	auto &caches = this->data.caches;
	auto *page = make_page_box();
	auto level = [&caches](unsigned index) -> Data::Caches::CpuType::Cache*
	{
		auto &type = caches.get_selected_cpu_type();
		return index < type.caches.size() ? &type.caches[index] : nullptr;
	};

	for(unsigned index = Data::Caches::CpuType::L1; index <= Data::Caches::CpuType::L4; index++)
	{
		auto *frame = Gtk::make_managed<InfoFrame>([level, index]() -> const ::Frame* { return level(index); });
		frame->add_row([level, index]() -> const Label* { auto *cache = level(index); return cache ? &cache->size : nullptr; });
		frame->add_row([level, index]() -> const Label* { auto *cache = level(index); return cache ? &cache->speed : nullptr; });
		page->append(*frame);
		this->frames.emplace_back(TAB_CACHES, frame);
	}

	auto *test = Gtk::make_managed<InfoFrame>([&caches] { return &caches.test; });
	this->cache_test_selector = Gtk::make_managed<Selector>();
	test->add_full_width(*this->cache_test_selector);
	this->cache_test_selector->set_items(caches.test.names, Options::get_selected_test());
	this->cache_test_selector->signal_index_changed.connect([](int index)
	{
		if(index >= 0)
			Options::set_selected_test(index);
	});
	page->append(*test);
	this->frames.emplace_back(TAB_CACHES, test);

	return *make_scrolled(*page);
}


/* Motherboard tab */

Gtk::Widget &MainWindow::build_motherboard_page()
{
	auto &mb = this->data.motherboard;
	auto *page = make_page_box();

	auto *board = Gtk::make_managed<InfoFrame>([&mb] { return &mb.board; });
	board->add_row([&mb] { return &mb.board.manufacturer; });
	board->add_row([&mb] { return &mb.board.model; });
	board->add_row([&mb] { return &mb.board.revision; });

	auto *bios = Gtk::make_managed<InfoFrame>([&mb] { return &mb.bios; });
	bios->add_row([&mb] { return &mb.bios.brand; });
	bios->add_row([&mb] { return &mb.bios.version; });
	bios->add_row([&mb] { return &mb.bios.date; });
	bios->add_row([&mb] { return &mb.bios.romsize; });
	bios->add_row([&mb] { return &mb.bios.efi_pk; }, N_("EFI Platform Key certificate information"));

	auto *chipset = Gtk::make_managed<InfoFrame>([&mb] { return &mb.chipset; });
	chipset->add_row([&mb] { return &mb.chipset.vendor; });
	chipset->add_row([&mb] { return &mb.chipset.model; });

	for(auto *frame : { board, bios, chipset })
	{
		page->append(*frame);
		this->frames.emplace_back(TAB_MOTHERBOARD, frame);
	}
	return *make_scrolled(*page);
}


/* Memory tab */

Gtk::Widget &MainWindow::build_memory_page()
{
	auto &memory = this->data.memory;
	auto *page = make_page_box();
	auto stick = [&memory]() -> Data::Memory::Stick* { return memory.sticks.empty() ? nullptr : &memory.get_selected_stick(); };

	auto *frame = Gtk::make_managed<InfoFrame>([stick]() -> const ::Frame* { return stick(); });
	using Stick = Data::Memory::Stick;
	auto field = [stick](Label Stick::*member) { return [stick, member]() -> const Label* { auto *s = stick(); return s ? &(s->*member) : nullptr; }; };
	frame->add_row(field(&Stick::manufacturer));
	frame->add_row(field(&Stick::part_number));
	frame->add_pair(field(&Stick::type), field(&Stick::type_detail));
	frame->add_pair(field(&Stick::device_locator), field(&Stick::bank_locator),
	                N_("Identify the physically-labeled socket or board position where the memory device is located"),
	                N_("Identify the physically labeled bank where the memory device is located"));
	frame->add_pair(field(&Stick::size), field(&Stick::rank));
	frame->add_pair(field(&Stick::speed), field(&Stick::voltage));
	page->append(*frame);

	auto *footer = Gtk::make_managed<InfoFrame>([&memory] { return &memory.footer; });
	this->stick_selector = Gtk::make_managed<Selector>();
	footer->add_full_width(*this->stick_selector);
	std::vector<std::string> sticks;
	for(unsigned i = 0; i < memory.sticks.size(); i++)
		sticks.push_back(memory.get_stick_formatted(i));
	this->stick_selector->set_items(sticks, Options::get_selected_stick());
	this->stick_selector->signal_index_changed.connect([this](int index)
	{
		if(index < 0)
			return;
		Options::set_selected_stick(index);
		this->refresh_page(TAB_MEMORY);
	});
	page->append(*footer);

	this->frames.emplace_back(TAB_MEMORY, frame);
	this->frames.emplace_back(TAB_MEMORY, footer);
	return *make_scrolled(*page);
}


/* System tab */

void MainWindow::draw_memory_bar(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height, int index)
{
	const auto &mem = this->data.system.memory;
	const bool swap = (index == 4);
	const long double total = swap ? mem.swap_total : mem.mem_total;
	if(total <= 0)
		return;

	/* Each bar shows its own segment, offset by the segments before it */
	const long double parts[] = { mem.mem_used, mem.mem_buffers, mem.mem_cached, mem.mem_free };
	double before = 0.0, percent;
	if(swap)
		percent = mem.swap_used / total * 100;
	else
	{
		for(int i = 0; i < index; i++)
			before += parts[i] / total * 100;
		percent = parts[index] / total * 100;
	}
	if(std::isnan(percent))
		return;

	static const double light[5][3] = { {1.00, 0.76, 0.03}, {0.13, 0.59, 0.95}, {0.61, 0.15, 0.69}, {0.30, 0.69, 0.31}, {0.96, 0.26, 0.21} };
	static const double dark [5][3] = { {0.80, 0.60, 0.00}, {0.21, 0.52, 0.89}, {0.57, 0.38, 0.84}, {0.15, 0.64, 0.41}, {0.88, 0.11, 0.14} };
	const auto &color = this->is_dark() ? dark[index] : light[index];
	const auto fg = this->get_color();

	/* Track */
	cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.08);
	cr->rectangle(0, 0, width, height);
	cr->fill();
	/* Segment */
	cr->set_source_rgb(color[0], color[1], color[2]);
	cr->rectangle(before / 100 * width, 0, percent / 100 * width, height);
	cr->fill();
	/* Percentage, in the theme foreground colour */
	auto layout = this->create_pango_layout(string_format("%.2f%%", percent));
	int text_width, text_height;
	layout->get_pixel_size(text_width, text_height);
	cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), fg.get_alpha());
	cr->move_to(std::max(2, width - text_width - 4), (height - text_height) / 2.0);
	layout->show_in_cairo_context(cr);
}

Gtk::Widget &MainWindow::build_system_page()
{
	auto &system = this->data.system;
	auto *page = make_page_box();

	auto *os = Gtk::make_managed<InfoFrame>([&system] { return &system.os; });
	os->add_row([&system] { return &system.os.name; });
	os->add_row([&system] { return &system.os.kernel; });
	os->add_row([&system] { return &system.os.hostname; });
	os->add_row([&system] { return &system.os.uptime; });
	page->append(*os);
	this->frames.emplace_back(TAB_SYSTEM, os);

	/* Memory: name, value and a usage bar per line */
	auto *memory = Gtk::make_managed<Gtk::Frame>();
	auto *grid = Gtk::make_managed<Gtk::Grid>();
	grid->set_row_spacing(4);
	grid->set_column_spacing(8);
	grid->set_margin(6);
	grid->set_column_homogeneous(false);
	memory->set_child(*grid);
	const Label *labels[] = { &system.memory.used, &system.memory.buffers, &system.memory.cached, &system.memory.free, &system.memory.swap };
	std::vector<std::pair<Gtk::Label*, Gtk::Label*>> rows;
	for(int i = 0; i < 5; i++)
	{
		auto *name  = make_name_label();
		auto *value = make_value_label();
		auto *bar   = Gtk::make_managed<Gtk::DrawingArea>();
		bar->set_hexpand(true);
		bar->set_content_height(1);
		bar->set_draw_func([this, i](const Cairo::RefPtr<Cairo::Context> &cr, int width, int height) { this->draw_memory_bar(cr, width, height, i); });
		grid->attach(*name, 0, i);
		grid->attach(*value, 1, i);
		grid->attach(*bar, 2, i);
		this->memory_bars.push_back(bar);
		rows.emplace_back(name, value);
	}
	page->append(*memory);
	this->extra_refresh.emplace_back(TAB_SYSTEM, [this, memory, rows, labels = std::vector<const Label*>(std::begin(labels), std::end(labels))]
	{
		memory->set_label(this->data.system.memory.name);
		for(std::size_t i = 0; i < rows.size(); i++)
		{
			rows[i].first->set_text(labels[i]->name);
			rows[i].first->set_tooltip_text(labels[i]->name);
			rows[i].second->set_text(labels[i]->value);
			rows[i].second->set_tooltip_text(labels[i]->value);
			this->memory_bars[i]->queue_draw();
		}
	});

	return *make_scrolled(*page);
}


/* Graphics tab */

Gtk::Widget &MainWindow::build_graphics_page()
{
	auto &graphics = this->data.graphics;
	auto *page = make_page_box();
	auto card = [&graphics]() -> Data::Graphics::Card* { return graphics.cards.empty() ? nullptr : &graphics.get_selected_card(); };
	using Card = Data::Graphics::Card;
	auto field = [card](Label Card::*member) { return [card, member]() -> const Label* { auto *c = card(); return c ? &(c->*member) : nullptr; }; };

	auto *frame = Gtk::make_managed<InfoFrame>([card]() -> const ::Frame* { return card(); });
	frame->add_pair(field(&Card::vendor), field(&Card::kernel_driver));
	frame->add_row(field(&Card::user_mode_driver), N_("User Mode Driver Version"));
	frame->add_row(field(&Card::model));
	frame->add_pair(field(&Card::comp_unit), field(&Card::device_id));
	frame->add_pair(field(&Card::vbios_version), field(&Card::interface));
	frame->add_pair(field(&Card::temperature), field(&Card::usage));
	frame->add_pair(field(&Card::core_voltage), field(&Card::power_avg));
	frame->add_pair(field(&Card::core_clock), field(&Card::mem_clock));
	frame->add_row(field(&Card::mem_used));
	frame->add_pair(field(&Card::resizable_bar), field(&Card::vulkan_rt), nullptr, N_("Vulkan Ray Tracing"));
	frame->add_row(field(&Card::opengl_version));
	frame->add_row(field(&Card::vulkan_version));
	frame->add_row(field(&Card::opencl_version));
	page->append(*frame);

	auto *footer = Gtk::make_managed<InfoFrame>([&graphics] { return &graphics.footer; });
	this->card_selector = Gtk::make_managed<Selector>();
	footer->add_full_width(*this->card_selector);
	std::vector<std::string> cards;
	for(unsigned i = 0; i < graphics.cards.size(); i++)
		cards.push_back(graphics.get_card_formatted(i));
	this->card_selector->set_items(cards, Options::get_selected_gpu());
	this->card_selector->signal_index_changed.connect([this](int index)
	{
		if(index < 0)
			return;
		Options::set_selected_gpu(index);
		this->refresh_current_page();
	});
	page->append(*footer);

	this->frames.emplace_back(TAB_GRAPHICS, frame);
	this->frames.emplace_back(TAB_GRAPHICS, footer);
	return *make_scrolled(*page);
}


/* Bench tab */

void MainWindow::on_bench_toggled(bool fast, bool state)
{
	if(this->bench_updating)
		return;
	if(state)
	{
		this->data.bench.fast_mode = fast;
		start_benchmarks(this->data);
	}
	else
		this->data.bench.is_running = false;
	this->update_bench_controls();
}

void MainWindow::update_bench_controls()
{
	auto &bench = this->data.bench;
	this->bench_updating = true;
	auto *active_switch  = bench.fast_mode ? this->bench_fast_switch : this->bench_slow_switch;
	auto *standby_switch = bench.fast_mode ? this->bench_slow_switch : this->bench_fast_switch;
	auto *active_bar     = bench.fast_mode ? this->bench_fast_bar    : this->bench_slow_bar;

	this->bench_slow_bar->set_text(bench.prime_slow.score.value);
	this->bench_fast_bar->set_text(bench.prime_fast.score.value);
	if(bench.is_running)
	{
		active_switch->set_active(true);
		standby_switch->set_sensitive(false);
		this->bench_threads->set_sensitive(false);
		const double total = double(bench.parameters.duration_i) * 60;
		active_bar->set_fraction(total > 0 ? std::min(1.0, bench.parameters.elapsed_i / total) : 0.0);
	}
	else
	{
		active_switch->set_active(false);
		standby_switch->set_sensitive(true);
		this->bench_threads->set_sensitive(true);
		if(bench.is_completed)
			active_bar->set_fraction(1.0);
	}
	this->bench_updating = false;
}

Gtk::Widget &MainWindow::build_bench_page()
{
	auto &bench = this->data.bench;
	auto *page = make_page_box();

	auto make_prime_frame = [this, page](Data::Bench::PrimeNumbers &prime, Gtk::ProgressBar *&bar, Gtk::Switch *&toggle, bool fast)
	{
		auto *frame = Gtk::make_managed<InfoFrame>([&prime] { return &prime; });
		bar = Gtk::make_managed<Gtk::ProgressBar>();
		bar->set_show_text(true);
		bar->set_ellipsize(Pango::EllipsizeMode::END);
		toggle = Gtk::make_managed<Gtk::Switch>();
		auto *score_name = frame->add_widget_row(*bar);
		auto *run_name   = frame->add_widget_row(*toggle, false);
		toggle->signal_state_set().connect([this, fast](bool state)
		{
			this->on_bench_toggled(fast, state);
			return false;
		}, false);
		this->extra_refresh.emplace_back(TAB_BENCH, [&prime, score_name, run_name]
		{
			score_name->set_text(prime.score.name);
			run_name->set_text(prime.state.name);
		});
		page->append(*frame);
		this->frames.emplace_back(TAB_BENCH, frame);
	};
	make_prime_frame(bench.prime_slow, this->bench_slow_bar, this->bench_slow_switch, false);
	make_prime_frame(bench.prime_fast, this->bench_fast_bar, this->bench_fast_switch, true);

	auto *parameters = Gtk::make_managed<InfoFrame>([&bench] { return &bench.parameters; });
	this->bench_duration = Gtk::make_managed<Gtk::SpinButton>();
	this->bench_duration->set_range(1, 60 * 24);
	this->bench_duration->set_increments(1, 60);
	this->bench_duration->set_numeric(true);
	this->bench_duration->set_value(bench.parameters.duration_i);
	this->bench_threads = Gtk::make_managed<Gtk::SpinButton>();
	this->bench_threads->set_range(1, std::max(1u, std::thread::hardware_concurrency()));
	this->bench_threads->set_increments(1, 1);
	this->bench_threads->set_numeric(true);
	this->bench_threads->set_value(bench.parameters.threads_i);
	auto *duration_name = parameters->add_widget_row(*this->bench_duration, false);
	auto *threads_name  = parameters->add_widget_row(*this->bench_threads, false);
	this->bench_duration->signal_value_changed().connect([this]
	{
		const int duration = this->bench_duration->get_value_as_int();
		if(duration > 0)
			this->data.bench.parameters.set_duration(duration);
	});
	this->bench_threads->signal_value_changed().connect([this]
	{
		const int threads = this->bench_threads->get_value_as_int();
		if(threads > 0)
			this->data.bench.parameters.set_threads(threads);
	});
	page->append(*parameters);
	this->frames.emplace_back(TAB_BENCH, parameters);
	this->extra_refresh.emplace_back(TAB_BENCH, [this, &bench, duration_name, threads_name]
	{
		duration_name->set_text(bench.parameters.duration.name);
		threads_name->set_text(bench.parameters.threads.name);
		this->update_bench_controls();
	});

	return *make_scrolled(*page);
}


/* About tab */

Gtk::Widget &MainWindow::build_about_page()
{
	auto &about = this->data.about;
	auto *page = make_page_box();

	auto make_text = [](const std::string &text)
	{
		auto *label = Gtk::make_managed<Gtk::Label>(text);
		label->set_wrap(true);
		label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
		label->set_justify(Gtk::Justification::CENTER);
		label->set_selectable(true);
		label->set_max_width_chars(70);
		return label;
	};

	auto *header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
	auto *logo = Gtk::make_managed<Gtk::Image>();
	logo->set_from_icon_name(APPLICATION_ID);
	logo->set_pixel_size(96);
	const auto logo_path = get_data_path(std::string(PRGNAME) + ".png");
	if(!logo_path.empty())
		logo->set(logo_path);
	auto *title = Gtk::make_managed<Gtk::Label>(PRGNAME);
	title->add_css_class("cpux-about-title");
	header->append(*logo);
	header->append(*title);
	header->append(*make_text(about.description.text));
	page->append(*header);

	auto *about_frame = Gtk::make_managed<Gtk::Frame>(about.about.Frame::name);
	auto *about_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
	about_box->set_margin(8);
	about_box->append(*make_text(about.about.version));
	about_box->append(*make_text(about.about.author));
	auto *website = Gtk::make_managed<Gtk::LinkButton>(PRGURL, about.about.website);
	about_box->append(*website);
	about_frame->set_child(*about_box);
	page->append(*about_frame);

	auto *license_frame = Gtk::make_managed<Gtk::Frame>(about.license.Frame::name);
	auto *license_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
	license_box->set_margin(8);
	license_box->append(*make_text(about.license.copyright));
	license_box->append(*make_text(about.license.name));
	license_box->append(*make_text(about.license.warranty));
	license_frame->set_child(*license_box);
	page->append(*license_frame);

	return *make_scrolled(*page);
}


/************************* Public functions *************************/

/* Load and apply settings from GSettings */
void load_settings()
{
	Glib::init();
	Gio::init();
	auto source = Gio::SettingsSchemaSource::get_default();
	if(!source || !source->lookup(APPLICATION_ID, true))
	{
		MSG_WARNING(_("GSettings schema '%s' is not installed, using default settings"), APPLICATION_ID);
		return;
	}

	settings = Gio::Settings::create(APPLICATION_ID);
	settings->delay();
	Options::set_temp_unit     (static_cast<OptTempUnit>(settings->get_enum("temperature-unit")));
	Options::set_refr_time     (settings->get_uint("refresh-time"));
	Options::set_selected_page (static_cast<TabNumber>(settings->get_enum("default-tab")));
	Options::set_selected_type (settings->get_uint("default-core-type"));
	Options::set_selected_core (settings->get_uint("default-cpu-core"));
	Options::set_selected_test (settings->get_uint("default-cache-test"));
	Options::set_selected_stick(settings->get_uint("default-memory-stick"));
	Options::set_selected_gpu  (settings->get_uint("default-active-card"));
	Options::set_cpuid_decimal (settings->get_boolean("print-cpuid-decimal"));
	Options::set_with_daemon   (settings->get_boolean("always-start-daemon"));
}

/* Start CPU-X in GTK mode */
int start_gui_gtk(Data &data)
{
	MSG_VERBOSE("%s", _("Starting GTK GUI…"));
	try
	{
		Glib::set_prgname(APPLICATION_ID);
		auto app = Gtk::Application::create(APPLICATION_ID, Gio::Application::Flags::NON_UNIQUE);
		std::unique_ptr<MainWindow> window;
		app->signal_activate().connect([&app, &window, &data]
		{
			window = std::make_unique<MainWindow>(data);
			app->add_window(*window);
			window->present();
		});
		const int status = app->run(0, nullptr);
		window.reset();
		return status;
	}
	catch(const Glib::Error &error)
	{
		MSG_ERROR("%s: %s", _("failed to start GTK GUI"), error.what());
	}
	catch(const std::exception &error)
	{
		MSG_ERROR("%s: %s", _("failed to start GTK GUI"), error.what());
	}
	return EXIT_FAILURE;
}
