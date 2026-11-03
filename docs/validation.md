# Validation record

Date: **2026-11-03**. These are observed development-machine results, not guarantees for another system.

## Environment

- Ubuntu 24.04, x86-64, Linux 6.14.
- KDE desktop on X11.
- GCC 13.3, Qt 5.15.13, CMake Release build.
- No pre-existing live Codex native hosts were available during the initial inspection. All termination tests used deliberately created disposable fixture processes.

## Automated checks

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The initial implementation passed **14 core test cases + 3 UI test cases**, excluding Qt's initialization/cleanup cases. The color refresh below expands the UI coverage to six test functions, including both light/dark confirmation datasets.

Covered behavior:

- Exact native/npm host classification; rejection of arbitrary commands mentioning Codex; updated/deleted executable paths.
- Correct `/proc/stat` parsing with spaces and parentheses in the process name, and memory unit parsing.
- Discovery, selection, and graceful termination without affecting an unselected fixture.
- Multiple explicitly confirmed hosts terminated together.
- Grace-period escalation of a fixture that ignores `SIGTERM`.
- Confirmed child-process cleanup.
- Refusal of stale root identity, a non-Codex root, and an unrelated injected target.
- Protected-host exclusion.
- Open-rollout association, schema-tolerant metadata reads, and read-only database digest preservation.
- A recorded child edge remains labelled liveness unknown.
- Session history without runtime association is not presented as live.
- Demo mode disables kill actions; filtering works.
- Checks survive refresh but not simulated PID reuse.
- Confirmation can be cancelled without starting termination.

The same suites passed under AddressSanitizer + UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan -j4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```

Leak detection is disabled for the Qt/offscreen sanitizer run; this is not a leak-free certification.

Other local checks:

- Desktop file validation passed.
- A separate Release configuration with `-DBUILD_TESTING=OFF` built successfully. Installing into a temporary prefix inside the project's ignored build directory installed the executable, desktop entry, and icon; the installed binary's snapshot and desktop-file validation passed. No system-wide or user-local installation was performed.
- Headless `--snapshot` runs without a graphical application object.
- The synthetic demo renders and saves a screenshot without real session data.
- A normal X11/KDE window opened and closed during the desktop benchmark.
- A temporary Codex-shaped fixture opened an existing local Codex rollout, then the monitor associated one observed session and recovered model metadata from the installed Codex storage. Database size and modification time were unchanged. This proves compatibility with that sampled storage, **not** end-to-end integration with a real running Codex app-server.

## Resource measurements

Measured with `tools/benchmark.py`, a two-second warmup, four synthetic fixture hosts with small synthetic session files, no inspector selection, and no kill operation. Existing system processes were scanned read-only; only the script's own child processes were cleaned up. CPU is measured as a percentage of **one core**.

| Backend / sample | Measurement | Median RSS | Median PSS | Median USS | CPU |
| --- | --- | --- | --- | --- | --- |
| Qt offscreen, zero fixture hosts | 12.04 s | 54.92 MiB | 17.60 MiB | 14.90 MiB | 0.58% |
| Qt offscreen, four fixture hosts | 8.02 s | 56.28 MiB | 17.88 MiB | 15.04 MiB | 0.75% |
| Normal X11/KDE window, four fixture hosts | 12.06 s | 133.95 MiB | 33.26 MiB | 24.42 MiB | 1.41% |

The zero-host sample preceded the final minor icon change; the four-host rows are post-change measurements. The larger desktop RSS includes additional mapped/shared desktop, font, icon, and display-backend pages. PSS apportions shared memory; it is not interchangeable with RSS. Short CPU samples have substantial scheduling noise.

The Release executable was approximately **344 KiB** before stripping, excluding system Qt libraries and plugins. Rebuilding with a different compiler or flags changes that figure.

Reproduce from the repository root:

```sh
python3 tools/benchmark.py --hosts 0 --seconds 12
python3 tools/benchmark.py --hosts 4 --seconds 12
# Opens a temporary visible window; requires a working X11 display.
python3 tools/benchmark.py --platform xcb --hosts 4 --seconds 12
```

## Color refresh follow-up — 2026-11-03

The visual refresh adds a blue-green brand header, semantic state badges, a blue memory area trace, and red destructive actions. It introduces no package dependency, animation timer, blur, or shadow effect. The process/termination implementation is unchanged.

Current validation:

- Release tests: **14 core + 6 UI test functions passed**. Confirmation cancellation is exercised separately in light and dark appearances.
- ASan/UBSan tests passed with the same leak-detection limitation documented above.
- Automated contrast checks cover primary/secondary text, semantic colors and badge fills, the header, the summary memory value, selection text, and filled destructive buttons; each tested text pair is at least 4.5:1.
- Simulated live Qt application-palette changes update both appearances while preserving the selected row and keeping demo kill actions disabled.
- The 780×600 window test checks the actual window size and containment of search/kill controls. This is not an exhaustive large-font or screen-reader audit.
- Both [light](demo.png) and [dark](demo-dark.png) synthetic screenshots were rendered and visually inspected. No real session information is present in either image.
- Normal monitoring follows the Qt system palette. The forced appearance flag is restricted to synthetic demo mode.

Follow-up resource samples use the updated aggregate-workload reporting in `tools/benchmark.py`. Four synthetic hosts were created; two other current-user Codex hosts were also present and were only observed read-only. No existing host was terminated. Thus this is **not** a controlled before/after benchmark against the earlier four-host-only table.

| Backend | Observed hosts / processes at start | Measurement | Median RSS | Median PSS | Median USS | CPU, one core |
| --- | --- | --- | --- | --- | --- | --- |
| Qt offscreen | 6 / 20 | 8.02 s | 58.66 MiB | 18.67 MiB | 15.71 MiB | 1.12% |
| Normal X11/KDE window | 6 / 22 | 8.04 s | 136.44 MiB | 33.25 MiB | 24.38 MiB | 1.12% |

Both samples use a two-second warmup and no inspector selection. Native desktop memory remains dominated by Qt/desktop integration. The measurements support keeping the visual changes lightweight, but do not prove zero overhead or establish an upper resource bound.

See [appearance notes](appearance.md) for exact color values and measured contrast ratios.

## Logo and desktop-entry follow-up — 2026-11-03

- Added an original SVG mark (mint terminal prompt + coral stop square on a blue-green tile), committed 16–512 px PNGs, and embedded 16–256 px icons in Qt resources. The running monitor needs no SVG renderer or icon-theme lookup for its own mark.
- Release and ASan/UBSan runs passed all three suites: **14 core, 7 UI, and 5 installer test functions**; the confirmation test also has separate light/dark datasets. The same sanitizer leak-detection limitation still applies.
- Every embedded icon size was checked for resource presence, dimensions, outer transparency, stop-square color, and a nonempty window icon. Synthetic light/dark screenshots were regenerated with the new logo.
- A production build without tests was installed into a temporary prefix containing spaces. Its generated desktop entry passed `desktop-file-validate`; its installed binary ran in demo mode from a different working directory.
- The authorized current-user installation placed the binary in `~/.local/bin`, logo assets under `~/.local/share/icons/hicolor`, an application-menu entry under `~/.local/share/applications`, and **Codex Killer.desktop** in the configured desktop directory. No system-wide files, autostart entries, services, or Codex settings were changed.
- The shortcut is executable (0755), and GIO reports both execution permission and trusted metadata. `Exec`, `TryExec`, and `Icon` point to absolute installed paths. The user's existing `codex-desktop.desktop` entry was preserved.
- Launching the actual desktop shortcut through GIO started the installed executable and produced a visible X11 window with `WM_CLASS=codex-killer` and a `_NET_WM_ICON` property. The window was left open for the user. No real Codex process was terminated and no real-session screenshot was saved or published.

The installer adds no runtime component to the monitor. Rebuild and rerun it to update the installed copy. See [logo and desktop integration](branding.md).

## Publication checks

- The repository was created as **public** under the default GitHub account: [M1racleShih/codex-killer](https://github.com/M1racleShih/codex-killer).
- Repository-local Git author settings use the GitHub account name and account email; global Git settings were not changed.
- Generated build artifacts and real Codex data were excluded. Staged source files were checked for private absolute paths and common credential patterns; the published screenshot uses synthetic data only.
- The initial implementation commit passed [GitHub Actions on Ubuntu 24.04](https://github.com/M1racleShih/codex-killer/actions/runs/41609086573): configure, build, both test suites, and the headless snapshot smoke test.
- Local Markdown links resolved successfully.

## Remaining limitations

- No destructive test was performed against the user's real Codex sessions.
- A true live app-server with multiple concurrently loaded sessions was not available for end-to-end verification. Open-FD/session association and shared-host presentation were tested with controlled data.
- Wayland, other distributions, older supported Qt versions, restricted `/proc`, denied pidfds, and large fleets remain unverified.
- No exhaustive keyboard/screen-reader audit or long-duration soak test has been performed. Native widgets and explicit labels are used, but that alone is not a full accessibility certification.
- Runtime memory depends heavily on desktop integration, themes, process count, file size, and permissions. The table is not a hard upper bound.
- Detached/reparented/new child processes can remain after a confirmed operation; the process set deliberately does not expand after confirmation.
