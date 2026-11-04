# Fork modernization status

Baseline: `bef2c44363f193ff844cfed41ac223a6ea5d57b7` (5.4.1).
Development is confined to zachjord/CPU-X. GTK4 replaces GTK3; there will be no
GTK3 compatibility frontend once the migration lands. Qt6 is deferred.

## Inspection findings

* `call_libcpuid_static()` copies libcpuid brand/codename and topology into `Data`.
  `fill_labels()` and `do_refresh()` drive static and selected-page dynamic data.
* `fallback_mode_static()` requests package lookup for absent or placeholder DMI
  values. The old matcher stopped at the first substring match. With Raphael as
  input, the 7945HX therefore received AM5; the Dragon Range rule could not match.
  This explains CPU-X's fallback, but does not establish why a particular
  libcpuid version supplied Raphael without the user's raw CPUID sample.
* Existing model rules can shadow longer models (Xeon E5-2690 v3/v4). Pure lookup
  and generator tests now cover specificity, conflicts and suffix boundaries.
* Shared `Tab`, `Frame` and `Label` objects contain opaque `Object *ext` pointers.
  GTK attaches widget wrappers; the shared headers do not include GTK types.
  A future frontend should own its binding lifetimes rather than expand this
  coupling. A backend rewrite or plugin framework is unnecessary.
* GTK uses GtkBuilder XML, GTKmm3, GSettings, notebook selection, timed refresh,
  selectors for CPU type/core, cache test, memory stick and GPU, benchmark
  switches/duration/thread controls, memory drawing areas and daemon reload.
  Fixed widget width requests and a large legacy XML layout require deliberate
  replacement, not only dependency renaming.
* NCurses is separately selectable with `WITH_GTK=OFF`. Preserve this option and
  verify it without GTK dependencies at every shared-state change.

## GTK4 frontend (implemented)

GTK3/GTKmm3, the 6,246-line GtkBuilder file, the Glade project and the fixed-colour
CSS files are removed. `src/ui/gtk.cpp` builds the window with gtkmm-4.0 (>= 4.10).

Decisions:

* **gtkmm-4.0, not the C API.** The rest of the program is C++17 and the old
  frontend already used gtkmm signal/slot idioms; gtkmm keeps lifetimes managed
  (`make_managed`) without a second language or manual GObject reference counting.
* **Code-built widgets, no GtkBuilder.** The old XML fixed most label widths and
  duplicated every label twice by ID. Pages are now composed from a small
  `InfoFrame` (grid of name/value rows) whose rows are *getters* evaluated on each
  refresh. Selected core type, stick or card changes therefore need no rebinding.
* **No toolkit pointers in `Data`.** `Object` and the `ext` members of `Tab`,
  `Frame` and `Label` are removed; the frontend owns all bindings. This is the
  only backend change and is the seam a Qt6 frontend would reuse. No abstraction
  layer or plugin system was added.
* **Layout.** Value labels expand, ellipsize, are selectable (context menu copy) and
  carry the full value as a tooltip. Every page scrolls in both directions, so the
  window can be shrunk freely (tiling compositors such as Sway). There are no fixed
  widget widths; only the CPU logo has a pixel size.
* **Themes.** CSS only derives borders/backgrounds from `currentColor`. The theme
  setting (Automatic/Light/Dark) maps to `gtk-application-prefer-dark-theme`.
  Memory bar text uses the widget's foreground colour.
* **Deprecated widgets avoided.** ComboBoxText → DropDown with an ellipsizing
  factory; InfoBar → Revealer banner; MessageDialog → AlertDialog.
* **Additions.** Copy the full report to the clipboard (button, Ctrl+Shift+C);
  Ctrl+Q quits. A missing GSettings schema falls back to defaults.
* **Packaging.** The polkit policy is now installed even with `WITH_GTK=OFF`,
  because the NCurses interface also starts the daemon.
* **Platforms.** Debian 12 (gtkmm 4.8) and Ubuntu 22.04 (gtkmm 4.6) cannot build
  the GUI; `scripts/build_cpu_x.sh` builds them with `WITH_GTK=OFF`.

Also fixed: `--tab` was always rejected (parsed before page visibility was
initialised) and saved GSettings overrode command-line options.

## Remaining roadmap

1. Capture 7945HX raw CPUID (`cpu-x --issue-fmt` prints the serialized libcpuid data;
   replay with `CPUX_CPUID_RAW=file`) for a full identification regression fixture.
2. Evaluate machine-readable manufacturer/libcpuid sources and licenses; add
   imports, snapshots, conflict handling and proposed update automation.
3. Review daemon peer authentication, command validation and IPC error paths;
   audit bundled dmidecode/Bandwidth modifications before dependency updates.
4. Extend topology/hardware fixtures and daemon protocol tests.
5. Verify the AppImage pipeline with GTK4 (script updated, not yet run).
6. Fork identity: application ID, URLs and the upstream "not maintained" banner
   still refer to the archived project.

The package-data foundation is not a claim that the migration, source importers,
daemon audit or full hardware regression suite is complete.

## Local verification (2026-11-04)

* Existing baseline AWK suite: passed (mawk/nawk available).
* Existing baseline grep suite: passed.
* Standalone C++ matcher: 19 checks passed with GCC C++17 and strict warnings.
* Database Python suite: 8 tests passed; generated output matches committed data.
* Address/undefined sanitizer matcher run: passed with leak detection disabled;
  LeakSanitizer itself cannot run in this traced environment.
* Baseline and changed CMake builds: passed with NCurses, bundled dmidecode
  and libcpuid 0.8.2 (`7ac123e3c13013ff6cd2d8c4ec1881177d71357f`).
  GTK, gettext, PCI/EGL/Vulkan/OpenCL, procps/statgrab and Bandwidth were disabled.
  Dependencies were installed after moving the apt cache to a writable directory.
  The private libcpuid install required `-I/tmp/cpux-deps/include` because its
  pkg-config include path points to the nested libcpuid directory.
* Baseline and changed `--version` and `--dump`: exit 0. Changed NCurses startup,
  timed refresh and quit: passed in a PTY. Polkit is unavailable in this container;
  privileged daemon behavior was not exercised.
* All four CTest suites passed (AWK, grep, matcher and Python database validation).
* All 125 inherited record values compared equal to the baseline source table.
* GUI, GPU collectors, Bandwidth and physical 7945HX verification remain untested.

Reproduce the reduced terminal build with `WITH_GTK`, `WITH_GETTEXT`,
`WITH_LIBPCI`, `WITH_LIBEGL`, `WITH_VULKAN`, `WITH_OPENCL`, `WITH_LIBPROCPS`,
`WITH_LIBSTATGRAB` and `WITH_BANDWIDTH` set to `OFF`; leave `WITH_LIBCPUID`,
`WITH_NCURSES` and `WITH_DMIDECODE` enabled. Build and run `ctest --test-dir build
--output-on-failure`, then `build/output/bin/cpu-x -D` and `-N`.

## GTK4 verification (2026-11-04)

Built with gtkmm 4.10.0 / GTK 4.14.5 (Ubuntu 24.04), GCC, Debug, no warnings, with
NCurses, libpci, EGL, Vulkan, OpenCL, libproc2, dmidecode and Bandwidth enabled.
libcpuid was **disabled** locally (only 0.6.4 is packaged; >= 0.8.0 is required),
so CPU identification values were empty in these runs; CI builds libcpuid.

* X11 (Xvfb): every tab started; light, dark (`Adwaita:dark`) and 16 pt font
  screenshots showed no clipping or overlap; window shrinks with scrolling.
* Wayland (headless Weston): GUI started and stayed running without diagnostics.
  Screenshots were not possible with the headless renderer; Sway itself untested.
* Driven with xdotool: prime (fast) benchmark ran to completion with live progress;
  slow benchmark started and was cancelled; threads control locked while running.
* Settings: theme and refresh time changed, applied live, persisted (keyfile
  backend) and were honoured on relaunch; no-schema fallback works.
* Copy report: clipboard holds the plain-text dump (no ANSI colours).
* `WITH_GTK=OFF`: binary links no GTK/GLib libraries; `-D` dump and `-N` TUI work.
* CTest: 5/5 passed, including the new `test_gui_smoke` (all tabs, no schema, dark).
* Not exercised: privileged daemon start (no polkit/pkexec in the container),
  real GPU/memory-stick/cache pages (no data in the container), HiDPI scale > 1.
