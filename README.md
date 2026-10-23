# Starfield Screensaver

A compact Win32 screensaver that flies through procedurally generated nebulae and star
fields. Pure C++17 software rendering: no GPU, no external libraries, no runtime
dependencies.

[![Build](https://github.com/IljaGnezdilov/starfield-screensaver/actions/workflows/build.yml/badge.svg)](https://github.com/IljaGnezdilov/starfield-screensaver/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/release/IljaGnezdilov/starfield-screensaver)](https://github.com/IljaGnezdilov/starfield-screensaver/releases)

[Русская версия](README.ru.md)

| Spiral nebula | Warp |
| --- | --- |
| ![Spiral nebula](docs/screenshots/01-spiral.png) | ![Warp](docs/screenshots/02-warp.png) |

| Rosette nebula | Emission nebula and galactic band |
| --- | --- |
| ![Rosette nebula](docs/screenshots/03-rosette.png) | ![Emission nebula](docs/screenshots/04-emission.png) |

## Features

- Procedural nebulae of three types (emission, shell, burst) with lobes, arcs, jets,
  dust lanes and four color palettes.
- Up to 120,000 stars split into faint, main, bright and featured groups, with
  twinkling, diffraction spikes, bokeh, Doppler tint and warp streaks.
- Deterministic timeline: one seed drives the cycle schedule, star field and sky.
  Reproducible frames for benchmarks and dumps.
- Warp transitions with motion streaks, fade in/out and a softened background.
- Multi-monitor panorama rendered by one window per display using a shared virtual
  bounding box, so the view is seamless across screens.
- Software framebuffer with additive blending, capped at 16 MP; the background is
  rendered at a reduced resolution and stretched with bilinear filtering.
- Native Win32 settings dialog, per-user registry storage, reset/cancel/OK semantics,
  validation and command-line overrides.
- Preview, windowed, benchmark and frame-dump modes for development.

## Requirements

- Windows 10 or 11 (x64 build).
- Visual Studio 2022 with the **Desktop development with C++** workload, or CMake 3.20+
  with the same toolchain.
- Windows SDK.

## Build

```powershell
cmd /c build.bat
```

The script locates MSVC through `vswhere`, compiles the resources and links statically
(`/MT`). The result is `build\Starfield.scr`. It links `user32`, `gdi32`, `winmm`,
`shell32` and `advapi32`; `comctl32` is not used.

CMake alternative:

```powershell
cmake -S . -B build-cmake
cmake --build build-cmake --config Release
```

When adding a new `.cpp`, update both `build.bat` and `CMakeLists.txt`.

## Install and run

1. Open **Settings → Personalization → Lock screen → Screen saver** and choose
   `Starfield.scr`, or right-click the `.scr` file and select **Install** (this copies it
   to the system folder and may require administrator rights).
2. Use **Settings** in the screen saver dialog to configure the scene.
3. Use **Preview** to see it in the small control-panel window.

Windows launches `.scr` files through `ShellExecute`, which may drop command-line
arguments. For testing, copy `build\Starfield.scr` to `build\Starfield.exe` and run the
executable from a console.

### Command line

| Argument | Purpose |
| --- | --- |
| no arguments or `/s` | Fullscreen on all monitors |
| `/c` or `/c:12345` | Settings dialog; the number is an optional owner HWND |
| `/p 12345` | Preview inside the control-panel parent window |
| `/w` | Windowed test mode, 1280×800, exit with Esc |
| `/bench:1920x1080 5` | Benchmark for 5 seconds; size is optional |
| `/dump:dump 1280x720` | Save key frames as BMP |
| `/seed:12345` | Override the seed |
| `/round:5`, `/warp:1`, etc. | Override individual settings |

In fullscreen mode the screensaver exits on a click, a key press, noticeable mouse
movement, focus loss longer than about 1.2 s, or a display configuration change. The
first ~0.7 s of input is ignored to avoid false triggers.

`/dump` renders four cycles and saves up to 150 frames. File names such as
`frame_001234.bmp` correspond to milliseconds on the timeline. Async sky generation is
disabled for dumps and benchmarks so frames are deterministic.

## Settings

Settings are stored per user in `HKCU\Software\StarfieldScreensaver`; administrator
rights are never required. Missing values fall back to defaults. Floats and the seed are
stored as strings, integers as `REG_DWORD`.

The `/c` dialog is a single form with three groups. **Reset all settings** only changes
the form fields; nothing is written until OK. **Cancel** discards all changes. OK
validates the values, recomputes derived timings and saves them.

### Timeline

| Parameter | Registry key | Default | Range |
| --- | --- | ---: | --- |
| Seed | `seed` | `-1` (random) | `-1..4294967295` |
| Round duration, s | `round_seconds` | 5 | 1–60 |
| Rotation per round, ° | `angle_min` / `angle_max` | 16 / 40 | 10–360 |
| Rounds before warp | `rounds_min` / `rounds_max` | 1 / 5 | 1–100 |
| Warp, s | `warp_seconds` | 1 | 0.1–5 |

Derived values are not persisted:

```text
segmentEaseSeconds = min(0.5, round_seconds * 0.25)
fadeInSeconds      = min(0.7, round_seconds * 0.5)
fadeOutSeconds     = min(0.4, warp_seconds * 0.5)
```

### Nebulae

| Parameter | Key | Default | Range |
| --- | --- | ---: | --- |
| Intensity | `nebula_intensity` | 1 | 0.1–3 |
| Size | `nebula_size` | 1 | 0.25–3 |
| Dust | `nebula_dust` | 1 | 0–2 |
| Distortion | `nebula_warp` | 1 | 0–2 |
| Edge roughness | `nebula_edge` | 1 | 0–2 |
| Palette | `nebula_palette` | `-1` (random) | `-1..3` |
| Types | `nebula_types` | 7 | mask 1/2/4 |
| Lobes | `nebula_lobes_min/max` | 2/4 | 1–4 |
| Arcs | `nebula_arcs_min/max` | 1/3 | 1–3 |
| Jets | `nebula_jets_min/max` | 2/5 | 1–5 |

Types: `1` emission, `2` shell, `4` burst. At least one type is required. Counts are
picked randomly from `min..max` for every cycle.

### Stars

| Parameter | Key | Default | Range |
| --- | --- | ---: | --- |
| Faint | `stars_faint` | 26000 | 0–60000 |
| Main | `stars_main` | 4200 | 0–30000 |
| Bright | `stars_bright` | 140 | 0–3000 |
| Featured | `stars_featured` | 3 | 0–3 |
| Size | `star_size` | 1 | 0.25–5 |
| Brightness | `star_brightness` | 1 | 0.1–5 |
| Galactic band | `star_band` | 1 (100%) | 0–1 |
| Twinkle | `star_twinkle` | 0.06 (6%) | 0–0.5 |
| Main spikes | `star_spike_main` | 0.10 (10%) | 0–1 |
| Main bokeh | `star_bokeh_main` | 0.10 (10%) | 0–1 |
| Bright bokeh | `star_bokeh_bright` | 0.22 (22%) | 0–1 |
| Doppler | `star_doppler` | 1 (100%) | 0–3 |
| Warp | `star_warp` | 1 (100%) | 0.25–3 |
| Scale, px | `star_scale_min/max` | 0.6/2.5 | 0.1–8 |

The total number of stars is capped at 120,000. Featured stars are three fixed bright
stars; only their count is configurable.

CLI keys match registry keys; `/round`, `/warp` and `/seed` are short aliases. After the
registry is loaded and CLI overrides are applied, every value goes through
`clampSettings`, so a manual launch cannot push parameters outside their ranges.

## How it works

```text
main.cpp       modes, windows, per-monitor views, frame loop, bench/dump
settings.*     registry, defaults, clamping, strict validation, Win32 dialog
starfield.*    timeline, cycle schedule, async sky, stars, warp/fade
sky.*          equirectangular sky texture, nebulae, palettes, blur
renderer.*     software framebuffer and additive primitives
math.h         Vec3, Quat, deterministic RNG
```

Frame flow:

1. `Scene::prepare(t)` picks the cycle, swaps in a finished sky if available and starts
   building the next one.
2. `Scene::render()` computes the cycle parameters and draws the background and stars.
3. Each `Surface` owns its renderer and `View`; the timeline is shared by the whole app.
4. `Renderer::present()` hands a 32-bit DIB to the window through `SetDIBitsToDevice`
   or `StretchDIBits`.

Key decisions:

- One seed drives everything: the cycle schedule, stars and sky. Each next cycle's seed
  is derived deterministically from the base seed.
- The sky for the next cycle is built on a background thread and joined at the cycle
  boundary; if it is not ready, it is generated synchronously.
- Fullscreen windows are created per monitor and share the bounding box of all displays,
  so the panorama has no seams or repeats.
- The background is rendered into a reduced buffer (at most 1.2 MP, roughly a quarter of
  the frame) and stretched with bilinear filtering. Stars are drawn at full resolution.
- Rendering is additive and channels saturate at 255. Stars are sorted by brightness so
  bright stars consistently overlay faint ones.
- The full frame is capped at 16 MP; larger windows use a smaller internal buffer that is
  stretched on present.

## Performance

Reference numbers on the development machine: about 144–146 fps at 1080p and 42–45 fps
at 4K. Actual numbers depend on the scene, background and system load.

## Known limitations

- The settings dialog is a fixed 500×350 DLU form. On very high DPI it may look large;
  there is no separate DPI scaling.
- At most three featured stars.
- The sum of individual star maximums (93,003) is below the global 120,000 cap; the cap
  remains a safeguard and scales groups down proportionally.
- No installer, code signing or automatic registration. Windows SmartScreen may warn
  about the unsigned executable.
- Command-line arguments passed through the Windows shell to `.scr` files may be lost.
- Windows 10/11 only.

## Development notes

- Code comments are in English; the UI and Russian README are in Russian.
- Add new `.cpp` files to both `build.bat` and `CMakeLists.txt`.
- `/dump` and `/bench` disable async sky generation to keep output deterministic.
- Keep the registry format and the seed/RNG stream stable unless a change is explicitly
  intended.

## License

MIT. See [LICENSE](LICENSE).
