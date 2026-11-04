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

## GTK4 implementation direction

Prefer GTKmm4 to retain C++ idioms and minimize changes to signal/control logic.
Use resizable, scrollable grids; selectable labels with full-value tooltips;
frontend-owned bindings; GTK4 drawing callbacks and theme settings. Preserve
CPU, caches, motherboard, memory, system, graphics, benchmarks and about pages,
settings persistence and all selectors. Include Wayland/Sway and X11 startup,
large-font scaling, live refresh, benchmark cancellation and daemon reload in
acceptance checks. Do not remove GTK3 until its functional replacement is being
implemented; this first database milestone does not add GTK3 compatibility code.

## Remaining roadmap

1. Build/run baseline and changed NCurses/CLI with libcpuid enabled; capture
   7945HX raw CPUID for full identification regression.
2. Implement and verify the full GTK4 replacement and remove GTK3 assets/deps.
3. Evaluate machine-readable manufacturer/libcpuid sources and licenses; add
   imports, snapshots, conflict handling and proposed update automation.
4. Review daemon peer authentication, command validation and IPC error paths;
   audit bundled dmidecode/Bandwidth modifications before dependency updates.
5. Extend topology/hardware fixtures, daemon tests and GTK startup/control tests.

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
