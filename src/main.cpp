#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <timeapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "renderer.h"
#include "settings.h"
#include "starfield.h"

#include "resource.h"

namespace {

constexpr wchar_t kWindowClass[] = L"StarfieldScreensaverWindow";
constexpr wchar_t kWindowTitle[] = L"Starfield Screensaver";

// Command-line modes. Windows normally passes /s, /c or /p; the other modes are
// development helpers and are easiest to use with a copy renamed to Starfield.exe.
enum class Mode {
    Fullscreen,
    Preview,
    Windowed,
    Bench,
    Dump,
    Config,
};

struct App;

// One window plus its own render target. In fullscreen mode there is one Surface per
// monitor, but all Surfaces share the same Scene and timeline so the sky lines up.
struct Surface {
    App* app = nullptr;
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    sf::Renderer renderer;
    sf::View view;
    RECT rect = {};
    int width = 0;
    int height = 0;
};

struct App {
    HINSTANCE instance = nullptr;
    Mode mode = Mode::Fullscreen;
    std::vector<Surface> surfaces;
    sf::Scene scene;
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    double lastRender = 0.0;
    POINT initialCursor{};
    DWORD startTick = 0;
    bool running = true;
    bool cursorHidden = false;
    bool rebuildDisplays = false;
    bool destroyingSurfaces = false;
    bool everForeground = false;
    DWORD foregroundLostTick = 0;
};

double elapsedSeconds(const App& app, const LARGE_INTEGER& now) {
    return static_cast<double>(now.QuadPart - app.start.QuadPart) /
           static_cast<double>(app.frequency.QuadPart);
}

void hideCursor(App& app) {
    if (app.cursorHidden) {
        return;
    }
    while (ShowCursor(FALSE) >= 0) {
    }
    app.cursorHidden = true;
}

void showCursor(App& app) {
    if (!app.cursorHidden) {
        return;
    }
    while (ShowCursor(TRUE) < 0) {
    }
    app.cursorHidden = false;
}

Surface* findSurface(App& app, HWND hwnd) {
    for (Surface& surface : app.surfaces) {
        if (surface.hwnd == hwnd) {
            return &surface;
        }
    }
    return nullptr;
}

bool isOurWindow(App& app, HWND hwnd) {
    if (hwnd == nullptr) {
        return false;
    }
    return findSurface(app, hwnd) != nullptr;
}

uint32_t g_seed = 0;

uint32_t resolveSeed(int64_t configured) {
    if (configured >= 0) {
        return static_cast<uint32_t>(static_cast<uint64_t>(configured) & 0xFFFFFFFFull);
    }

    // -1 means "random": mix UTC milliseconds with splitmix64 so nearby launches do
    // not produce visually similar schedules. Zero is remapped because Rng() rejects it.
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    uint64_t z = static_cast<uint64_t>(now) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    const uint32_t mixed = static_cast<uint32_t>(z ^ (z >> 32));
    return mixed == 0u ? 0x5EA57A12u : mixed;
}

void updatePanoramaViews(App& app) {
    if (app.mode != Mode::Fullscreen || app.surfaces.empty()) {
        return;
    }

    // Treat all monitors as one virtual screen. The union bounds define a single
    // projection; each Surface only gets the sub-rectangle it actually covers.
    RECT bounds = app.surfaces.front().rect;
    for (const Surface& surface : app.surfaces) {
        bounds.left = std::min(bounds.left, surface.rect.left);
        bounds.top = std::min(bounds.top, surface.rect.top);
        bounds.right = std::max(bounds.right, surface.rect.right);
        bounds.bottom = std::max(bounds.bottom, surface.rect.bottom);
    }

    const float width = static_cast<float>(std::max(1L, bounds.right - bounds.left));
    const float height = static_cast<float>(std::max(1L, bounds.bottom - bounds.top));
    const float centerX = 0.5f * (static_cast<float>(bounds.left) + static_cast<float>(bounds.right));
    const float centerY = 0.5f * (static_cast<float>(bounds.top) + static_cast<float>(bounds.bottom));
    const float focal =
        0.5f * height / std::tan(cfg::kFovYDeg * 0.5f * 3.14159265358979323846f / 180.0f);

    for (Surface& surface : app.surfaces) {
        surface.view.centerX = centerX - static_cast<float>(surface.rect.left);
        surface.view.centerY = centerY - static_cast<float>(surface.rect.top);
        surface.view.focal = focal;
        surface.view.referenceWidth = width;
        surface.view.referenceHeight = height;
    }
}

void updateClientSize(Surface& surface) {
    if (surface.hwnd == nullptr) {
        return;
    }

    RECT rect = {};
    GetClientRect(surface.hwnd, &rect);

    const int width = std::max(1, static_cast<int>(rect.right - rect.left));
    const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));

    if (width != surface.width || height != surface.height) {
        surface.width = width;
        surface.height = height;
        surface.renderer.resize(width, height);
        if (surface.app != nullptr) {
            surface.app->scene.resize(surface.renderer.width(), surface.renderer.height());
            if (!surface.app->destroyingSurfaces) {
                updatePanoramaViews(*surface.app);
            }
        }
    }
}

void quit(App& app) {
    app.running = false;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Surface* surface = reinterpret_cast<Surface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    App* app = surface != nullptr ? surface->app : nullptr;

    switch (message) {
        case WM_NCCREATE: {
            const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }

        case WM_CREATE: {
            Surface* createSurface = reinterpret_cast<Surface*>(
                reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            if (createSurface != nullptr) {
                createSurface->hwnd = hwnd;
            }
            return 0;
        }

        case WM_SIZE:
            if (surface != nullptr) {
                updateClientSize(*surface);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETCURSOR:
            if (app != nullptr && app->mode != Mode::Windowed) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
            if (app != nullptr && app->mode == Mode::Fullscreen) {
                if (GetTickCount() - app->startTick > 700) {
                    quit(*app);
                }
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (app != nullptr) {
                if (app->mode == Mode::Fullscreen) {
                    if (GetTickCount() - app->startTick > 300) {
                        quit(*app);
                    }
                } else if (app->mode == Mode::Windowed && wParam == VK_ESCAPE) {
                    quit(*app);
                }
            }
            return 0;

        case WM_DISPLAYCHANGE:
            if (app != nullptr && app->mode == Mode::Fullscreen) {
                app->rebuildDisplays = true;
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (app == nullptr || !app->destroyingSurfaces) {
                PostQuitMessage(0);
            }
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool registerWindowClass(HINSTANCE instance) {
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    if (icon == nullptr) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;

    return RegisterClassExW(&wc) != 0;
}

HWND createSurfaceWindow(App& app, DWORD exStyle, DWORD style, int x, int y, int width,
                         int height, HWND parent, const wchar_t* title) {
    app.surfaces.emplace_back();
    Surface* surface = &app.surfaces.back();
    surface->app = &app;

    HWND hwnd = CreateWindowExW(exStyle, kWindowClass, title, style, x, y, width, height, parent,
                                nullptr, app.instance, surface);
    if (hwnd == nullptr) {
        app.surfaces.pop_back();
        return nullptr;
    }

    surface->hwnd = hwnd;
    surface->rect = {x, y, x + width, y + height};
    surface->dc = GetDC(hwnd);
    updateClientSize(*surface);
    return hwnd;
}

BOOL CALLBACK monitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* rects = reinterpret_cast<std::vector<RECT>*>(data);

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }

    const RECT& rect = info.rcMonitor;
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return TRUE;
    }

    for (const RECT& existing : *rects) {
        if (existing.left == rect.left && existing.top == rect.top &&
            existing.right == rect.right && existing.bottom == rect.bottom) {
            return TRUE;
        }
    }

    rects->push_back(rect);
    return TRUE;
}

std::vector<RECT> collectMonitors() {
    std::vector<RECT> rects;
    EnumDisplayMonitors(nullptr, nullptr, monitorProc, reinterpret_cast<LPARAM>(&rects));

    if (rects.empty()) {
        RECT fallback = {};
        fallback.right = GetSystemMetrics(SM_CXSCREEN);
        fallback.bottom = GetSystemMetrics(SM_CYSCREEN);
        if (fallback.right <= fallback.left || fallback.bottom <= fallback.top) {
            fallback.right = 1920;
            fallback.bottom = 1080;
        }
        rects.push_back(fallback);
    }

    return rects;
}

bool createFullscreenSurfaces(App& app) {
    // One topmost popup per monitor, positioned in physical pixels. The union of these
    // rects becomes the panorama used by updatePanoramaViews().
    const std::vector<RECT> rects = collectMonitors();
    if (rects.empty()) {
        return false;
    }

    for (const RECT& rect : rects) {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;

        HWND hwnd = createSurfaceWindow(app, WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                        WS_POPUP | WS_VISIBLE, rect.left, rect.top, width, height,
                                        nullptr, kWindowTitle);
        if (hwnd == nullptr) {
            return false;
        }

        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_TOPMOST, rect.left, rect.top, width, height, SWP_SHOWWINDOW);
    }

    updatePanoramaViews(app);

    if (!app.surfaces.empty()) {
        SetForegroundWindow(app.surfaces.front().hwnd);
        SetFocus(app.surfaces.front().hwnd);
    }

    return true;
}

HWND createPreviewWindow(App& app, HWND parent) {
    RECT rect = {};
    GetClientRect(parent, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        width = 320;
        height = 240;
    }

    HWND hwnd = createSurfaceWindow(app, 0, WS_CHILD | WS_VISIBLE, 0, 0, width, height, parent,
                                    kWindowTitle);
    if (hwnd == nullptr) {
        return nullptr;
    }

    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

HWND createWindowedWindow(App& app) {
    RECT rect = {0, 0, 1280, 800};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = createSurfaceWindow(app, 0, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, y, width, height,
                                    nullptr, L"Starfield Screensaver (windowed test)");
    if (hwnd == nullptr) {
        return nullptr;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

void destroySurfaces(App& app) {
    app.destroyingSurfaces = true;

    for (Surface& surface : app.surfaces) {
        if (surface.dc != nullptr) {
            ReleaseDC(surface.hwnd, surface.dc);
            surface.dc = nullptr;
        }
        if (surface.hwnd != nullptr) {
            DestroyWindow(surface.hwnd);
            surface.hwnd = nullptr;
        }
    }

    app.surfaces.clear();
    app.destroyingSurfaces = false;
}

void rebuildFullscreenSurfaces(App& app) {
    destroySurfaces(app);
    createFullscreenSurfaces(app);

    app.startTick = GetTickCount();
    GetCursorPos(&app.initialCursor);
}

int runLoop(App& app, double targetFps) {
    const double frameTime = 1.0 / targetFps;
    double nextRender = 0.0;
    LARGE_INTEGER now;

    while (app.running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                app.running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!app.running) {
            break;
        }

        if (app.rebuildDisplays) {
            app.rebuildDisplays = false;
            rebuildFullscreenSurfaces(app);
        }

        if (app.surfaces.empty()) {
            break;
        }

        if (app.mode == Mode::Fullscreen) {
            // Give the screensaver a moment to become foreground, then exit after it
            // has been background for 1.2 s. The delay avoids closing when the shell
            // briefly steals focus during startup.
            if (GetTickCount() - app.startTick > 1000) {
                const HWND foreground = GetForegroundWindow();
                if (isOurWindow(app, foreground)) {
                    app.everForeground = true;
                    app.foregroundLostTick = 0;
                } else if (app.everForeground && foreground != nullptr) {
                    if (app.foregroundLostTick == 0) {
                        app.foregroundLostTick = GetTickCount();
                    } else if (GetTickCount() - app.foregroundLostTick > 1200) {
                        break;
                    }
                } else {
                    app.foregroundLostTick = 0;
                }
            }

            // Ignore tiny cursor jitter; only a cumulative movement of more than four
            // pixels counts as real user input.
            POINT pt;
            if (GetCursorPos(&pt) && GetTickCount() - app.startTick > 700) {
                const int dx = std::abs(pt.x - app.initialCursor.x);
                const int dy = std::abs(pt.y - app.initialCursor.y);
                if (dx + dy > 4) {
                    break;
                }
            }
        }

        QueryPerformanceCounter(&now);
        const double t = elapsedSeconds(app, now);

        if (t >= nextRender) {
            double prev = app.lastRender;
            if (prev <= 0.0) {
                prev = t - frameTime;
            }

            for (Surface& surface : app.surfaces) {
                app.scene.render(surface.renderer, t, prev, surface.view);
                surface.renderer.present(surface.dc, surface.width, surface.height);
            }

            app.lastRender = t;
            nextRender = t + frameTime;
            if (nextRender < t) {
                nextRender = t;
            }
        } else {
            double wait = (nextRender - t) * 1000.0;
            int waitMs = static_cast<int>(wait);
            if (waitMs < 1) {
                waitMs = 1;
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(waitMs), QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE);
        }
    }

    return 0;
}

bool saveBmp(const std::wstring& path, const sf::Framebuffer& fb) {
    const int rowSize = ((fb.width * 3 + 3) & ~3);
    const DWORD imageSize = static_cast<DWORD>(rowSize) * static_cast<DWORD>(fb.height);

    BITMAPFILEHEADER fileHeader = {};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + imageSize;

    BITMAPINFOHEADER infoHeader = {};
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = fb.width;
    infoHeader.biHeight = fb.height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = imageSize;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        return false;
    }

    fwrite(&fileHeader, sizeof(fileHeader), 1, file);
    fwrite(&infoHeader, sizeof(infoHeader), 1, file);

    std::vector<unsigned char> row(static_cast<size_t>(rowSize), 0u);
    for (int y = fb.height - 1; y >= 0; --y) {
        const uint32_t* src = fb.scanline(y);
        for (int x = 0; x < fb.width; ++x) {
            const uint32_t c = src[x];
            row[static_cast<size_t>(x) * 3 + 0] = static_cast<unsigned char>(c & 0xFFu);
            row[static_cast<size_t>(x) * 3 + 1] = static_cast<unsigned char>((c >> 8) & 0xFFu);
            row[static_cast<size_t>(x) * 3 + 2] = static_cast<unsigned char>((c >> 16) & 0xFFu);
        }
        fwrite(row.data(), 1, static_cast<size_t>(rowSize), file);
    }

    fclose(file);
    return true;
}

void attachConsole() {
    if (AllocConsole()) {
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
        SetConsoleTitleW(L"Starfield Screensaver");
    }
}

bool parseSize(const std::wstring& text, int& width, int& height) {
    const size_t x = text.find_first_of(L"xX");
    if (x == std::wstring::npos) {
        return false;
    }

    wchar_t* end = nullptr;
    const long w = wcstol(text.substr(0, x).c_str(), &end, 10);
    if (end == nullptr || *end != L'\0' || w <= 0) {
        return false;
    }
    const long h = wcstol(text.substr(x + 1).c_str(), &end, 10);
    if (end == nullptr || *end != L'\0' || h <= 0) {
        return false;
    }

    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

int runBench(int width, int height, double seconds) {
    attachConsole();
    std::printf("seed=%u\n", g_seed);

    sf::Renderer renderer;
    renderer.init();
    renderer.resize(width, height);

    // Async sky generation would make frame times depend on thread scheduling; force
    // deterministic generation so benchmark numbers stay comparable.
    sf::Scene scene;
    scene.setAsyncSky(false);
    scene.init(g_seed);
    scene.resize(renderer.width(), renderer.height());

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    const int frames = std::max(1, static_cast<int>(seconds * 60.0));
    double totalMs = 0.0;
    double maxMs = 0.0;

    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / 60.0;
        const double prev = t - 1.0 / 60.0;

        scene.prepare(t);

        LARGE_INTEGER a;
        LARGE_INTEGER b;
        QueryPerformanceCounter(&a);
        scene.render(renderer, t, prev);
        QueryPerformanceCounter(&b);

        const double ms = static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 /
                          static_cast<double>(freq.QuadPart);
        totalMs += ms;
        if (ms > maxMs) {
            maxMs = ms;
        }
    }

    const double avg = totalMs / static_cast<double>(frames);
    std::printf("Starfield benchmark, %dx%d, %d frames\n", renderer.width(), renderer.height(),
                frames);
    std::printf("avg render: %.2f ms (%.1f fps)\nmax render: %.2f ms\n", avg, 1000.0 / avg, maxMs);
    std::fflush(stdout);

    return 0;
}

int runDump(const std::wstring& directory, int width, int height) {
    attachConsole();
    std::wprintf(L"seed=%u\n", g_seed);
    CreateDirectoryW(directory.c_str(), nullptr);

    sf::Renderer renderer;
    renderer.init();
    renderer.resize(width, height);

    // Dumps must be reproducible, so use the same synchronous sky path as benchmarks.
    sf::Scene scene;
    scene.setAsyncSky(false);
    scene.init(g_seed);
    scene.resize(renderer.width(), renderer.height());

    std::vector<double> times;
    const int cycles = 4;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const double start = scene.cycleStart(cycle);
        const double length = scene.cycleLength(cycle);
        const int segments = scene.cycleSegmentCount(cycle);
        const double rotationSpan =
            static_cast<double>(segments) * static_cast<double>(cfg::kSettings.segmentSeconds);

        std::wprintf(L"cycle %d: %d segment(s), %.1f s\n", cycle, segments, length);

        times.push_back(start + 0.6);
        times.push_back(start + 2.5);
        for (int segment = 0; segment < segments; ++segment) {
            times.push_back(start + static_cast<double>(segment + 1) *
                                        static_cast<double>(cfg::kSettings.segmentSeconds) -
                            0.15);
        }
        times.push_back(start + rotationSpan + 0.5);
        times.push_back(start + length - 0.06);
    }

    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(),
                            [](double a, double b) { return std::fabs(a - b) < 1.0e-6; }),
                times.end());

    // Keep dumps reviewable and bounded. If the requested timeline has more samples,
    // keep evenly spaced frames instead of writing thousands of near-identical BMPs.
    constexpr size_t kMaxDumpFrames = 150;
    if (times.size() > kMaxDumpFrames) {
        std::vector<double> limited;
        limited.reserve(kMaxDumpFrames);
        for (size_t i = 0; i < kMaxDumpFrames; ++i) {
            const size_t index =
                i * (times.size() - 1) / (kMaxDumpFrames - 1);
            limited.push_back(times[index]);
        }
        times.swap(limited);
    }

    for (double t : times) {
        scene.render(renderer, t, t - 1.0 / 60.0);

        wchar_t fileName[64];
        swprintf_s(fileName, L"frame_%05d.bmp", static_cast<int>(t * 1000.0 + 0.5));

        std::wstring path = directory;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
            path += L'\\';
        }
        path += fileName;

        if (saveBmp(path, renderer.frame())) {
            std::wprintf(L"saved %s\n", path.c_str());
        } else {
            std::wprintf(L"failed %s (error %lu)\n", path.c_str(), GetLastError());
        }
    }

    std::fflush(stdout);
    return 0;
}

std::wstring toLower(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return value;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return 1;
    }

    Mode mode = Mode::Fullscreen;
    HWND previewParent = nullptr;
    HWND configOwner = nullptr;
    std::wstring dumpDirectory = L"dump";
    int dumpWidth = 1920;
    int dumpHeight = 1080;
    int benchWidth = 1920;
    int benchHeight = 1080;
    double benchSeconds = 20.0;
    std::vector<std::pair<std::wstring, std::wstring>> overrides;
    bool modeExplicit = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg.size() < 2 || (arg[0] != L'/' && arg[0] != L'-')) {
            continue;
        }

        std::wstring key = toLower(arg.substr(1));
        std::wstring value;
        const size_t colon = key.find(L':');
        if (colon != std::wstring::npos) {
            value = key.substr(colon + 1);
            key = key.substr(0, colon);
        }

        if (key == L"s") {
            mode = Mode::Fullscreen;
            modeExplicit = true;
        } else if (key == L"p") {
            if (value.empty() && i + 1 < argc) {
                value = argv[++i];
            }
            previewParent = reinterpret_cast<HWND>(
                static_cast<uintptr_t>(_wcstoui64(value.c_str(), nullptr, 10)));
            mode = Mode::Preview;
            modeExplicit = true;
        } else if (key == L"c" || key == L"a") {
            if (!value.empty()) {
                configOwner = reinterpret_cast<HWND>(
                    static_cast<uintptr_t>(_wcstoui64(value.c_str(), nullptr, 0)));
            }
            mode = Mode::Config;
            modeExplicit = true;
        } else if (key == L"w" || key == L"windowed") {
            mode = Mode::Windowed;
            modeExplicit = true;
        } else if (key == L"bench") {
            if (!value.empty()) {
                if (!parseSize(value, benchWidth, benchHeight)) {
                    benchWidth = 1920;
                    benchHeight = 1080;
                }
            } else if (i + 1 < argc && argv[i + 1][0] != L'/' && argv[i + 1][0] != L'-') {
                parseSize(argv[++i], benchWidth, benchHeight);
            }
            if (i + 1 < argc && argv[i + 1][0] != L'/' && argv[i + 1][0] != L'-') {
                benchSeconds = _wtof(argv[++i]);
                if (benchSeconds <= 0.0) {
                    benchSeconds = 20.0;
                }
            }
            mode = Mode::Bench;
            modeExplicit = true;
        } else if (key == L"seed") {
            if (value.empty() && i + 1 < argc && argv[i + 1][0] != L'/' &&
                argv[i + 1][0] != L'-') {
                value = argv[++i];
            }
            overrides.emplace_back(key, value);
        } else if (key == L"dump") {
            if (!value.empty()) {
                dumpDirectory = value;
            } else if (i + 1 < argc && argv[i + 1][0] != L'/' && argv[i + 1][0] != L'-') {
                dumpDirectory = argv[++i];
            }
            if (i + 1 < argc && argv[i + 1][0] != L'/' && argv[i + 1][0] != L'-') {
                int w = 0;
                int h = 0;
                if (parseSize(argv[i + 1], w, h)) {
                    dumpWidth = w;
                    dumpHeight = h;
                    ++i;
                }
            }
            mode = Mode::Dump;
            modeExplicit = true;
        } else {
            if (value.empty() && i + 1 < argc && argv[i + 1][0] != L'/' &&
                argv[i + 1][0] != L'-') {
                value = argv[++i];
            }
            overrides.emplace_back(key, value);
        }
    }

    LocalFree(argv);

    // Effective settings = defaults -> registry -> command-line overrides -> clamp.
    // Clamping here means malformed CLI input cannot crash the renderer later.
    cfg::Settings settings;
    sf::loadSettings(settings);

    for (const auto& entry : overrides) {
        std::wstring error;
        sf::applySettingOverride(settings, entry.first, entry.second, error);
    }

    sf::clampSettings(settings);
    cfg::kSettings = settings;
    g_seed = resolveSeed(settings.seed);

    if (!modeExplicit) {
        mode = Mode::Fullscreen;
    }

    if (mode == Mode::Config) {
        sf::showSettingsDialog(instance, configOwner, settings);
        return 0;
    }
    if (mode == Mode::Bench) {
        return runBench(benchWidth, benchHeight, benchSeconds);
    }
    if (mode == Mode::Dump) {
        return runDump(dumpDirectory, dumpWidth, dumpHeight);
    }

    if (mode == Mode::Preview && previewParent == nullptr) {
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    timeBeginPeriod(1);

    App app;
    app.instance = instance;
    app.mode = mode;
    app.surfaces.reserve(32);

    if (!registerWindowClass(instance)) {
        timeEndPeriod(1);
        return 1;
    }

    app.scene.init(g_seed);

    if (mode == Mode::Preview) {
        if (createPreviewWindow(app, previewParent) == nullptr) {
            timeEndPeriod(1);
            return 1;
        }
    } else if (mode == Mode::Windowed) {
        if (createWindowedWindow(app) == nullptr) {
            timeEndPeriod(1);
            return 1;
        }
    } else {
        if (!createFullscreenSurfaces(app)) {
            destroySurfaces(app);
            timeEndPeriod(1);
            return 1;
        }

    }

    QueryPerformanceFrequency(&app.frequency);
    QueryPerformanceCounter(&app.start);
    app.startTick = GetTickCount();
    GetCursorPos(&app.initialCursor);
    app.lastRender = 0.0;

    if (mode == Mode::Fullscreen) {
        hideCursor(app);
    }

    // The control-panel preview is small and runs at half rate to avoid stealing CPU
    // from the desktop; full screen targets 60 fps.
    const double fps = (mode == Mode::Preview) ? 30.0 : 60.0;
    const int result = runLoop(app, fps);

    showCursor(app);

    destroySurfaces(app);

    timeEndPeriod(1);

    return result;
}
