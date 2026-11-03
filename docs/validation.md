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

Both test executables passed: **14 core test cases + 3 UI test cases**, excluding Qt's initialization/cleanup cases.

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
