#define NOMINMAX
#include "settings.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <string>

#include "resource.h"

namespace sf {

namespace {

// Per-user location. There is deliberately no HKLM fallback: the screensaver should
// never require administrator rights.
constexpr wchar_t kRegistryPath[] = L"Software\\StarfieldScreensaver";

float clampFloat(float value, float low, float high) {
    return std::min(std::max(value, low), high);
}

int clampInt(int value, int low, int high) {
    return std::min(std::max(value, low), high);
}

bool parseFloat(const std::wstring& text, float& out) {
    // Accept the decimal comma used by the Russian UI as well as the registry's dot.
    std::wstring normalized = text;
    for (wchar_t& ch : normalized) {
        if (ch == L',') {
            ch = L'.';
        }
    }

    wchar_t* end = nullptr;
    const double value = wcstod(normalized.c_str(), &end);
    if (end == normalized.c_str() || *end != L'\0' || !std::isfinite(value)) {
        return false;
    }

    out = static_cast<float>(value);
    return true;
}

bool parseInt64(const std::wstring& text, int64_t& out) {
    wchar_t* end = nullptr;
    errno = 0;
    const long long value = wcstoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || errno == ERANGE) {
        return false;
    }

    out = static_cast<int64_t>(value);
    return true;
}

bool readDword(HKEY key, const wchar_t* name, DWORD& out) {
    DWORD size = sizeof(out);
    DWORD type = 0;
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, &type, &out, &size) == ERROR_SUCCESS;
}

bool readString(HKEY key, const wchar_t* name, std::wstring& out) {
    DWORD size = 0;
    DWORD type = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS ||
        size == 0) {
        return false;
    }

    std::wstring text(static_cast<size_t>(size) / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, text.data(), &size) !=
        ERROR_SUCCESS) {
        return false;
    }

    text.resize(std::wcslen(text.c_str()));
    out = text;
    return true;
}

bool writeString(HKEY key, const wchar_t* name, const std::wstring& value) {
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          bytes) == ERROR_SUCCESS;
}

bool writeDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                          sizeof(value)) == ERROR_SUCCESS;
}

std::wstring formatFloat(float value) {
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%.6g", static_cast<double>(value));
    return buffer;
}

std::wstring formatInt(int value) {
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%d", value);
    return buffer;
}

std::wstring formatInt64(int64_t value) {
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%lld", static_cast<long long>(value));
    return buffer;
}

bool readFloatControl(HWND hwnd, int id, const wchar_t* label, float& out,
                      std::wstring& error) {
    wchar_t buffer[128] = {};
    GetDlgItemTextW(hwnd, id, buffer, 128);
    if (buffer[0] == L'\0' || !parseFloat(buffer, out)) {
        error = std::wstring(L"Проверьте поле «") + label + L"».";
        return false;
    }
    return true;
}

bool readIntControl(HWND hwnd, int id, const wchar_t* label, int& out, std::wstring& error) {
    wchar_t buffer[128] = {};
    GetDlgItemTextW(hwnd, id, buffer, 128);
    int64_t value = 0;
    if (buffer[0] == L'\0' || !parseInt64(buffer, value) || value < INT_MIN ||
        value > INT_MAX) {
        error = std::wstring(L"Проверьте поле «") + label + L"».";
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool readInt64Control(HWND hwnd, int id, const wchar_t* label, int64_t& out,
                      std::wstring& error) {
    wchar_t buffer[128] = {};
    GetDlgItemTextW(hwnd, id, buffer, 128);
    if (buffer[0] == L'\0' || !parseInt64(buffer, out)) {
        error = std::wstring(L"Проверьте поле «") + label + L"».";
        return false;
    }
    return true;
}

void setControlText(HWND hwnd, int id, const std::wstring& text) {
    SetDlgItemTextW(hwnd, id, text.c_str());
}

bool readControls(HWND hwnd, cfg::Settings& s, std::wstring& error) {
    if (!readFloatControl(hwnd, IDC_ROUND_SECONDS, L"Длительность раунда", s.segmentSeconds,
                          error) ||
        !readFloatControl(hwnd, IDC_ANGLE_MIN, L"Минимальный угол", s.segmentMinDeg, error) ||
        !readFloatControl(hwnd, IDC_ANGLE_MAX, L"Максимальный угол", s.segmentMaxDeg, error) ||
        !readIntControl(hwnd, IDC_ROUNDS_MIN, L"Минимум раундов", s.minSegments, error) ||
        !readIntControl(hwnd, IDC_ROUNDS_MAX, L"Максимум раундов", s.maxSegments, error) ||
        !readFloatControl(hwnd, IDC_WARP_SECONDS, L"Длительность варпа", s.warpSeconds, error) ||
        !readInt64Control(hwnd, IDC_SEED, L"Seed", s.seed, error)) {
        return false;
    }

    if (!readFloatControl(hwnd, IDC_NEB_INTENSITY, L"Интенсивность", s.nebulaIntensity, error) ||
        !readFloatControl(hwnd, IDC_NEB_SIZE, L"Размер", s.nebulaSize, error) ||
        !readFloatControl(hwnd, IDC_NEB_DUST, L"Пыль", s.nebulaDust, error) ||
        !readFloatControl(hwnd, IDC_NEB_WARP, L"Искажение", s.nebulaWarp, error) ||
        !readFloatControl(hwnd, IDC_NEB_EDGE, L"Рваность", s.nebulaEdge, error) ||
        !readIntControl(hwnd, IDC_NEB_LOBES_MIN, L"Минимум лопастей", s.nebulaLobesMin,
                        error) ||
        !readIntControl(hwnd, IDC_NEB_LOBES_MAX, L"Максимум лопастей", s.nebulaLobesMax,
                        error) ||
        !readIntControl(hwnd, IDC_NEB_ARCS_MIN, L"Минимум дуг", s.nebulaArcsMin, error) ||
        !readIntControl(hwnd, IDC_NEB_ARCS_MAX, L"Максимум дуг", s.nebulaArcsMax, error) ||
        !readIntControl(hwnd, IDC_NEB_JETS_MIN, L"Минимум джетов", s.nebulaJetsMin, error) ||
        !readIntControl(hwnd, IDC_NEB_JETS_MAX, L"Максимум джетов", s.nebulaJetsMax, error)) {
        return false;
    }

    const LRESULT palette = SendDlgItemMessageW(hwnd, IDC_NEB_PALETTE, CB_GETCURSEL, 0, 0);
    if (palette < 0 || palette > 4) {
        error = L"Выберите палитру туманности.";
        return false;
    }
    s.nebulaPalette = static_cast<int>(palette) - 1;

    s.nebulaTypes = 0;
    if (IsDlgButtonChecked(hwnd, IDC_NEB_TYPE_EMISSION) == BST_CHECKED) {
        s.nebulaTypes |= 1;
    }
    if (IsDlgButtonChecked(hwnd, IDC_NEB_TYPE_SHELL) == BST_CHECKED) {
        s.nebulaTypes |= 2;
    }
    if (IsDlgButtonChecked(hwnd, IDC_NEB_TYPE_BURST) == BST_CHECKED) {
        s.nebulaTypes |= 4;
    }

    if (!readIntControl(hwnd, IDC_STARS_FAINT, L"Слабые звёзды", s.starsFaint, error) ||
        !readIntControl(hwnd, IDC_STARS_MAIN, L"Основные звёзды", s.starsMain, error) ||
        !readIntControl(hwnd, IDC_STARS_BRIGHT, L"Яркие звёзды", s.starsBright, error) ||
        !readIntControl(hwnd, IDC_STARS_FEATURED, L"Избранные звёзды", s.starsFeatured, error) ||
        !readFloatControl(hwnd, IDC_STAR_SIZE, L"Размер звёзд", s.starSize, error) ||
        !readFloatControl(hwnd, IDC_STAR_BRIGHTNESS, L"Яркость звёзд", s.starBrightness,
                          error)) {
        return false;
    }

    // The UI shows probabilities as percent, while Settings stores normalized 0..1
    // values. All fields below use the same conversion.
    float percent = 0.0f;
    if (!readFloatControl(hwnd, IDC_STAR_BAND, L"Полоса", percent, error)) {
        return false;
    }
    s.starBand = percent * 0.01f;

    if (!readFloatControl(hwnd, IDC_STAR_TWINKLE, L"Мерцание", percent, error)) {
        return false;
    }
    s.starTwinkle = percent * 0.01f;

    if (!readFloatControl(hwnd, IDC_STAR_SPIKE, L"Лучи", percent, error)) {
        return false;
    }
    s.starSpikeMain = percent * 0.01f;

    if (!readFloatControl(hwnd, IDC_STAR_BOKEH_MAIN, L"Боке основных", percent, error)) {
        return false;
    }
    s.starBokehMain = percent * 0.01f;

    if (!readFloatControl(hwnd, IDC_STAR_BOKEH_BRIGHT, L"Боке ярких", percent, error)) {
        return false;
    }
    s.starBokehBright = percent * 0.01f;

    if (!readFloatControl(hwnd, IDC_STAR_DOPPLER, L"Доплер", percent, error)) {
        return false;
    }
    s.starDoppler = percent * 0.01f;

    if (!readFloatControl(hwnd, IDC_STAR_WARP, L"Варп", percent, error)) {
        return false;
    }
    s.starWarp = percent * 0.01f;

    if (!readFloatControl(hwnd, IDC_STAR_SCALE_MIN, L"Минимальный масштаб", s.starScaleMin,
                          error) ||
        !readFloatControl(hwnd, IDC_STAR_SCALE_MAX, L"Максимальный масштаб", s.starScaleMax,
                          error)) {
        return false;
    }

    return true;
}

void fillControls(HWND hwnd, const cfg::Settings& s) {
    setControlText(hwnd, IDC_ROUND_SECONDS, formatFloat(s.segmentSeconds));
    setControlText(hwnd, IDC_ANGLE_MIN, formatFloat(s.segmentMinDeg));
    setControlText(hwnd, IDC_ANGLE_MAX, formatFloat(s.segmentMaxDeg));
    setControlText(hwnd, IDC_ROUNDS_MIN, formatInt(s.minSegments));
    setControlText(hwnd, IDC_ROUNDS_MAX, formatInt(s.maxSegments));
    setControlText(hwnd, IDC_WARP_SECONDS, formatFloat(s.warpSeconds));
    setControlText(hwnd, IDC_SEED, formatInt64(s.seed));

    setControlText(hwnd, IDC_NEB_INTENSITY, formatFloat(s.nebulaIntensity));
    setControlText(hwnd, IDC_NEB_SIZE, formatFloat(s.nebulaSize));
    setControlText(hwnd, IDC_NEB_DUST, formatFloat(s.nebulaDust));
    setControlText(hwnd, IDC_NEB_WARP, formatFloat(s.nebulaWarp));
    setControlText(hwnd, IDC_NEB_EDGE, formatFloat(s.nebulaEdge));
    setControlText(hwnd, IDC_NEB_LOBES_MIN, formatInt(s.nebulaLobesMin));
    setControlText(hwnd, IDC_NEB_LOBES_MAX, formatInt(s.nebulaLobesMax));
    setControlText(hwnd, IDC_NEB_ARCS_MIN, formatInt(s.nebulaArcsMin));
    setControlText(hwnd, IDC_NEB_ARCS_MAX, formatInt(s.nebulaArcsMax));
    setControlText(hwnd, IDC_NEB_JETS_MIN, formatInt(s.nebulaJetsMin));
    setControlText(hwnd, IDC_NEB_JETS_MAX, formatInt(s.nebulaJetsMax));

    SendDlgItemMessageW(hwnd, IDC_NEB_PALETTE, CB_SETCURSEL,
                        static_cast<WPARAM>(s.nebulaPalette + 1), 0);
    CheckDlgButton(hwnd, IDC_NEB_TYPE_EMISSION,
                   (s.nebulaTypes & 1) != 0 ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_NEB_TYPE_SHELL,
                   (s.nebulaTypes & 2) != 0 ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_NEB_TYPE_BURST,
                   (s.nebulaTypes & 4) != 0 ? BST_CHECKED : BST_UNCHECKED);

    setControlText(hwnd, IDC_STARS_FAINT, formatInt(s.starsFaint));
    setControlText(hwnd, IDC_STARS_MAIN, formatInt(s.starsMain));
    setControlText(hwnd, IDC_STARS_BRIGHT, formatInt(s.starsBright));
    setControlText(hwnd, IDC_STARS_FEATURED, formatInt(s.starsFeatured));
    setControlText(hwnd, IDC_STAR_SIZE, formatFloat(s.starSize));
    setControlText(hwnd, IDC_STAR_BRIGHTNESS, formatFloat(s.starBrightness));
    setControlText(hwnd, IDC_STAR_BAND, formatFloat(s.starBand * 100.0f));
    setControlText(hwnd, IDC_STAR_TWINKLE, formatFloat(s.starTwinkle * 100.0f));
    setControlText(hwnd, IDC_STAR_SPIKE, formatFloat(s.starSpikeMain * 100.0f));
    setControlText(hwnd, IDC_STAR_BOKEH_MAIN, formatFloat(s.starBokehMain * 100.0f));
    setControlText(hwnd, IDC_STAR_BOKEH_BRIGHT, formatFloat(s.starBokehBright * 100.0f));
    setControlText(hwnd, IDC_STAR_DOPPLER, formatFloat(s.starDoppler * 100.0f));
    setControlText(hwnd, IDC_STAR_WARP, formatFloat(s.starWarp * 100.0f));
    setControlText(hwnd, IDC_STAR_SCALE_MIN, formatFloat(s.starScaleMin));
    setControlText(hwnd, IDC_STAR_SCALE_MAX, formatFloat(s.starScaleMax));
}

struct DialogState {
    cfg::Settings* target = nullptr;  // Caller's settings; updated only on successful OK.
    cfg::Settings edited;             // Working copy backing the controls.
};

INT_PTR CALLBACK settingsDlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    DialogState* state =
        reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));

    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<DialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));

            // Combo index 0 maps to the sentinel -1 (random palette); 1..4 map to the
            // four palettes. readControls() and fillControls() rely on this offset.
            SendDlgItemMessageW(hwnd, IDC_NEB_PALETTE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(L"Случайная (−1)"));
            SendDlgItemMessageW(hwnd, IDC_NEB_PALETTE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(L"1"));
            SendDlgItemMessageW(hwnd, IDC_NEB_PALETTE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(L"2"));
            SendDlgItemMessageW(hwnd, IDC_NEB_PALETTE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(L"3"));
            SendDlgItemMessageW(hwnd, IDC_NEB_PALETTE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(L"4"));

            fillControls(hwnd, state->edited);
            return TRUE;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDOK && state != nullptr) {
                // Parse into a copy so invalid input leaves both the controls and the
                // caller's settings untouched.
                cfg::Settings edited = state->edited;
                std::wstring error;
                if (!readControls(hwnd, edited, error)) {
                    MessageBoxW(hwnd, error.c_str(), L"Настройки", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                if (!validateSettings(edited, error)) {
                    MessageBoxW(hwnd, error.c_str(), L"Настройки", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }

                deriveTimings(edited);
                if (!saveSettings(edited)) {
                    MessageBoxW(hwnd, L"Не удалось сохранить настройки в реестр.",
                                L"Настройки", MB_OK | MB_ICONERROR);
                    return TRUE;
                }

                *state->target = edited;
                EndDialog(hwnd, IDOK);
                return TRUE;
            }

            if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }

            if (id == IDC_BTN_DEFAULTS && state != nullptr) {
                // Reset only the visible form. Nothing is written until the user clicks
                // OK, so Cancel can still discard the reset.
                state->edited = defaultSettings();
                fillControls(hwnd, state->edited);
                return TRUE;
            }
            break;
        }

        default:
            break;
    }

    return FALSE;
}

bool setFloatOverride(const std::wstring& value, float& field, const wchar_t* key,
                      std::wstring& error) {
    float parsed = 0.0f;
    if (!parseFloat(value, parsed)) {
        error = std::wstring(L"Некорректное значение /") + key + L": " + value;
        return false;
    }
    field = parsed;
    return true;
}

bool setIntOverride(const std::wstring& value, int& field, const wchar_t* key,
                    std::wstring& error) {
    int64_t parsed = 0;
    if (!parseInt64(value, parsed) || parsed < INT_MIN || parsed > INT_MAX) {
        error = std::wstring(L"Некорректное значение /") + key + L": " + value;
        return false;
    }
    field = static_cast<int>(parsed);
    return true;
}

bool setSeedOverride(const std::wstring& value, int64_t& field, const wchar_t* key,
                     std::wstring& error) {
    int64_t parsed = 0;
    if (!parseInt64(value, parsed)) {
        error = std::wstring(L"Некорректное значение /") + key + L": " + value;
        return false;
    }
    field = parsed;
    return true;
}

} // namespace

void deriveTimings(cfg::Settings& settings) {
    // Keep the motion envelope proportional to the user's round and warp lengths.
    // These values are not persisted because they can always be recomputed.
    settings.segmentEaseSeconds = std::min(0.5f, settings.segmentSeconds * 0.25f);
    settings.fadeInSeconds = std::min(0.7f, settings.segmentSeconds * 0.5f);
    settings.fadeOutSeconds = std::min(0.4f, settings.warpSeconds * 0.5f);
}

cfg::Settings defaultSettings() {
    cfg::Settings settings;
    deriveTimings(settings);
    return settings;
}

void clampSettings(cfg::Settings& s) {
    // Lenient repair pass for registry/CLI input. Unlike validateSettings(), this
    // never rejects a value: it clamps ranges and swaps inverted min/max pairs.
    s.seed = std::min<int64_t>(std::max<int64_t>(s.seed, -1), 0xFFFFFFFFll);

    s.segmentSeconds = clampFloat(s.segmentSeconds, 1.0f, 60.0f);
    s.segmentMinDeg = clampFloat(s.segmentMinDeg, 10.0f, 360.0f);
    s.segmentMaxDeg = clampFloat(s.segmentMaxDeg, 10.0f, 360.0f);
    if (s.segmentMinDeg > s.segmentMaxDeg) {
        std::swap(s.segmentMinDeg, s.segmentMaxDeg);
    }

    s.minSegments = clampInt(s.minSegments, 1, 100);
    s.maxSegments = clampInt(s.maxSegments, 1, 100);
    if (s.minSegments > s.maxSegments) {
        std::swap(s.minSegments, s.maxSegments);
    }

    s.warpSeconds = clampFloat(s.warpSeconds, 0.1f, 5.0f);

    s.nebulaIntensity = clampFloat(s.nebulaIntensity, 0.1f, 3.0f);
    s.nebulaSize = clampFloat(s.nebulaSize, 0.25f, 3.0f);
    s.nebulaDust = clampFloat(s.nebulaDust, 0.0f, 2.0f);
    s.nebulaWarp = clampFloat(s.nebulaWarp, 0.0f, 2.0f);
    s.nebulaEdge = clampFloat(s.nebulaEdge, 0.0f, 2.0f);
    s.nebulaPalette = clampInt(s.nebulaPalette, -1, 3);
    s.nebulaTypes &= 7;
    if (s.nebulaTypes == 0) {
        s.nebulaTypes = 7;
    }

    s.nebulaLobesMin = clampInt(s.nebulaLobesMin, 1, 4);
    s.nebulaLobesMax = clampInt(s.nebulaLobesMax, 1, 4);
    if (s.nebulaLobesMin > s.nebulaLobesMax) {
        std::swap(s.nebulaLobesMin, s.nebulaLobesMax);
    }

    s.nebulaArcsMin = clampInt(s.nebulaArcsMin, 1, 3);
    s.nebulaArcsMax = clampInt(s.nebulaArcsMax, 1, 3);
    if (s.nebulaArcsMin > s.nebulaArcsMax) {
        std::swap(s.nebulaArcsMin, s.nebulaArcsMax);
    }

    s.nebulaJetsMin = clampInt(s.nebulaJetsMin, 1, 5);
    s.nebulaJetsMax = clampInt(s.nebulaJetsMax, 1, 5);
    if (s.nebulaJetsMin > s.nebulaJetsMax) {
        std::swap(s.nebulaJetsMin, s.nebulaJetsMax);
    }

    s.starsFaint = clampInt(s.starsFaint, 0, 60000);
    s.starsMain = clampInt(s.starsMain, 0, 30000);
    s.starsBright = clampInt(s.starsBright, 0, 3000);
    s.starsFeatured = clampInt(s.starsFeatured, 0, 3);

    // Preserve the featured stars and scale the three random populations down together
    // so their visual ratio stays intact.
    const int total = s.starsFaint + s.starsMain + s.starsBright + s.starsFeatured;
    if (total > cfg::kStarTotalCap) {
        const int scalable = s.starsFaint + s.starsMain + s.starsBright;
        const double factor =
            static_cast<double>(cfg::kStarTotalCap - s.starsFeatured) /
            static_cast<double>(std::max(1, scalable));
        s.starsFaint = static_cast<int>(static_cast<double>(s.starsFaint) * factor);
        s.starsMain = static_cast<int>(static_cast<double>(s.starsMain) * factor);
        s.starsBright = static_cast<int>(static_cast<double>(s.starsBright) * factor);
    }

    s.starSize = clampFloat(s.starSize, 0.25f, 5.0f);
    s.starBrightness = clampFloat(s.starBrightness, 0.1f, 5.0f);
    s.starBand = clampFloat(s.starBand, 0.0f, 1.0f);
    s.starTwinkle = clampFloat(s.starTwinkle, 0.0f, 0.5f);
    s.starSpikeMain = clampFloat(s.starSpikeMain, 0.0f, 1.0f);
    s.starBokehMain = clampFloat(s.starBokehMain, 0.0f, 1.0f);
    s.starBokehBright = clampFloat(s.starBokehBright, 0.0f, 1.0f);
    s.starDoppler = clampFloat(s.starDoppler, 0.0f, 3.0f);
    s.starWarp = clampFloat(s.starWarp, 0.25f, 3.0f);
    s.starScaleMin = clampFloat(s.starScaleMin, 0.1f, 8.0f);
    s.starScaleMax = clampFloat(s.starScaleMax, 0.1f, 8.0f);
    if (s.starScaleMin > s.starScaleMax) {
        std::swap(s.starScaleMin, s.starScaleMax);
    }

    deriveTimings(s);
}

bool validateSettings(const cfg::Settings& s, std::wstring& error) {
    // Strict dialog validation. The message is shown verbatim and the dialog stays
    // open, so each branch should identify the field and its valid range.
    if (s.segmentSeconds < 1.0f || s.segmentSeconds > 60.0f) {
        error = L"Длительность раунда должна быть от 1 до 60 с.";
        return false;
    }
    if (s.segmentMinDeg < 10.0f || s.segmentMinDeg > 360.0f ||
        s.segmentMaxDeg < 10.0f || s.segmentMaxDeg > 360.0f) {
        error = L"Угол обзора должен быть от 10 до 360°.";
        return false;
    }
    if (s.segmentMinDeg > s.segmentMaxDeg) {
        error = L"Минимальный угол не должен превышать максимальный.";
        return false;
    }
    if (s.minSegments < 1 || s.minSegments > 100 || s.maxSegments < 1 ||
        s.maxSegments > 100) {
        error = L"Количество раундов должно быть от 1 до 100.";
        return false;
    }
    if (s.minSegments > s.maxSegments) {
        error = L"Минимум раундов не должен превышать максимум.";
        return false;
    }
    if (s.warpSeconds < 0.1f || s.warpSeconds > 5.0f) {
        error = L"Длительность варпа должна быть от 0.1 до 5 с.";
        return false;
    }
    if (s.seed < -1 || s.seed > 0xFFFFFFFFll) {
        error = L"Seed должен быть −1 или от 0 до 4294967295.";
        return false;
    }

    if (s.nebulaIntensity < 0.1f || s.nebulaIntensity > 3.0f) {
        error = L"Интенсивность туманности должна быть от 0.1 до 3.";
        return false;
    }
    if (s.nebulaSize < 0.25f || s.nebulaSize > 3.0f) {
        error = L"Размер туманности должен быть от 0.25 до 3.";
        return false;
    }
    if (s.nebulaDust < 0.0f || s.nebulaDust > 2.0f) {
        error = L"Пыль туманности должна быть от 0 до 2.";
        return false;
    }
    if (s.nebulaWarp < 0.0f || s.nebulaWarp > 2.0f) {
        error = L"Искажение туманности должно быть от 0 до 2.";
        return false;
    }
    if (s.nebulaEdge < 0.0f || s.nebulaEdge > 2.0f) {
        error = L"Рваность туманности должна быть от 0 до 2.";
        return false;
    }
    if (s.nebulaPalette < -1 || s.nebulaPalette > 3) {
        error = L"Палитра должна быть от −1 до 3.";
        return false;
    }
    if ((s.nebulaTypes & ~7) != 0 || (s.nebulaTypes & 7) == 0) {
        error = L"Выберите хотя бы один тип туманности.";
        return false;
    }
    if (s.nebulaLobesMin < 1 || s.nebulaLobesMin > 4 || s.nebulaLobesMax < 1 ||
        s.nebulaLobesMax > 4) {
        error = L"Количество лопастей должно быть от 1 до 4.";
        return false;
    }
    if (s.nebulaLobesMin > s.nebulaLobesMax) {
        error = L"Минимум лопастей не должен превышать максимум.";
        return false;
    }
    if (s.nebulaArcsMin < 1 || s.nebulaArcsMin > 3 || s.nebulaArcsMax < 1 ||
        s.nebulaArcsMax > 3) {
        error = L"Количество дуг должно быть от 1 до 3.";
        return false;
    }
    if (s.nebulaArcsMin > s.nebulaArcsMax) {
        error = L"Минимум дуг не должен превышать максимум.";
        return false;
    }
    if (s.nebulaJetsMin < 1 || s.nebulaJetsMin > 5 || s.nebulaJetsMax < 1 ||
        s.nebulaJetsMax > 5) {
        error = L"Количество джетов должно быть от 1 до 5.";
        return false;
    }
    if (s.nebulaJetsMin > s.nebulaJetsMax) {
        error = L"Минимум джетов не должен превышать максимум.";
        return false;
    }

    if (s.starsFaint < 0 || s.starsFaint > 60000) {
        error = L"Количество слабых звёзд должно быть от 0 до 60000.";
        return false;
    }
    if (s.starsMain < 0 || s.starsMain > 30000) {
        error = L"Количество основных звёзд должно быть от 0 до 30000.";
        return false;
    }
    if (s.starsBright < 0 || s.starsBright > 3000) {
        error = L"Количество ярких звёзд должно быть от 0 до 3000.";
        return false;
    }
    if (s.starsFeatured < 0 || s.starsFeatured > 3) {
        error = L"Количество избранных звёзд должно быть от 0 до 3.";
        return false;
    }
    if (s.starsFaint + s.starsMain + s.starsBright + s.starsFeatured > cfg::kStarTotalCap) {
        error = L"Суммарное количество звёзд не должно превышать 120000.";
        return false;
    }
    if (s.starSize < 0.25f || s.starSize > 5.0f) {
        error = L"Размер звёзд должен быть от 0.25 до 5.";
        return false;
    }
    if (s.starBrightness < 0.1f || s.starBrightness > 5.0f) {
        error = L"Яркость звёзд должна быть от 0.1 до 5.";
        return false;
    }
    if (s.starBand < 0.0f || s.starBand > 1.0f) {
        error = L"Полоса должна быть от 0 до 100%.";
        return false;
    }
    if (s.starTwinkle < 0.0f || s.starTwinkle > 0.5f) {
        error = L"Мерцание должно быть от 0 до 50%.";
        return false;
    }
    if (s.starSpikeMain < 0.0f || s.starSpikeMain > 1.0f ||
        s.starBokehMain < 0.0f || s.starBokehMain > 1.0f || s.starBokehBright < 0.0f ||
        s.starBokehBright > 1.0f) {
        error = L"Вероятности лучей и боке должны быть от 0 до 100%.";
        return false;
    }
    if (s.starDoppler < 0.0f || s.starDoppler > 3.0f) {
        error = L"Доплер должен быть от 0 до 300%.";
        return false;
    }
    if (s.starWarp < 0.25f || s.starWarp > 3.0f) {
        error = L"Варп-чувствительность должна быть от 25 до 300%.";
        return false;
    }
    if (s.starScaleMin < 0.1f || s.starScaleMin > 8.0f || s.starScaleMax < 0.1f ||
        s.starScaleMax > 8.0f) {
        error = L"Масштаб звёзд должен быть от 0.1 до 8 px.";
        return false;
    }
    if (s.starScaleMin > s.starScaleMax) {
        error = L"Минимальный масштаб не должен превышать максимальный.";
        return false;
    }

    return true;
}

bool loadSettings(cfg::Settings& settings) {
    // Missing key is not an error: start from defaults. A malformed individual value
    // keeps its default and is logged, so one bad registry entry cannot block startup.
    settings = cfg::Settings{};

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        clampSettings(settings);
        return false;
    }

    const auto loadFloat = [&](const wchar_t* name, float& field) {
        std::wstring text;
        float value = 0.0f;
        if (readString(key, name, text) && parseFloat(text, value)) {
            field = value;
        }
    };

    const auto loadInt = [&](const wchar_t* name, int& field) {
        DWORD value = 0;
        if (readDword(key, name, value)) {
            field = static_cast<int>(static_cast<int32_t>(value));
        }
    };

    const auto loadInt64 = [&](const wchar_t* name, int64_t& field) {
        std::wstring text;
        int64_t value = 0;
        if (readString(key, name, text) && parseInt64(text, value)) {
            field = value;
        }
    };

    loadInt64(L"seed", settings.seed);
    loadFloat(L"round_seconds", settings.segmentSeconds);
    loadInt(L"rounds_min", settings.minSegments);
    loadInt(L"rounds_max", settings.maxSegments);
    loadFloat(L"angle_min", settings.segmentMinDeg);
    loadFloat(L"angle_max", settings.segmentMaxDeg);
    loadFloat(L"warp_seconds", settings.warpSeconds);

    loadFloat(L"nebula_intensity", settings.nebulaIntensity);
    loadFloat(L"nebula_size", settings.nebulaSize);
    loadInt(L"nebula_types", settings.nebulaTypes);
    loadFloat(L"nebula_dust", settings.nebulaDust);
    loadFloat(L"nebula_warp", settings.nebulaWarp);
    loadFloat(L"nebula_edge", settings.nebulaEdge);
    loadInt(L"nebula_palette", settings.nebulaPalette);
    loadInt(L"nebula_lobes_min", settings.nebulaLobesMin);
    loadInt(L"nebula_lobes_max", settings.nebulaLobesMax);
    loadInt(L"nebula_arcs_min", settings.nebulaArcsMin);
    loadInt(L"nebula_arcs_max", settings.nebulaArcsMax);
    loadInt(L"nebula_jets_min", settings.nebulaJetsMin);
    loadInt(L"nebula_jets_max", settings.nebulaJetsMax);

    loadInt(L"stars_faint", settings.starsFaint);
    loadInt(L"stars_main", settings.starsMain);
    loadInt(L"stars_bright", settings.starsBright);
    loadInt(L"stars_featured", settings.starsFeatured);
    loadFloat(L"star_size", settings.starSize);
    loadFloat(L"star_brightness", settings.starBrightness);
    loadFloat(L"star_band", settings.starBand);
    loadFloat(L"star_twinkle", settings.starTwinkle);
    loadFloat(L"star_spike_main", settings.starSpikeMain);
    loadFloat(L"star_bokeh_main", settings.starBokehMain);
    loadFloat(L"star_bokeh_bright", settings.starBokehBright);
    loadFloat(L"star_doppler", settings.starDoppler);
    loadFloat(L"star_warp", settings.starWarp);
    loadFloat(L"star_scale_min", settings.starScaleMin);
    loadFloat(L"star_scale_max", settings.starScaleMax);

    RegCloseKey(key);
    clampSettings(settings);
    return true;
}

bool saveSettings(const cfg::Settings& s) {
    // Writes every value, not just changed ones. Floats use REG_SZ because the registry
    // has no portable float type; ints use REG_DWORD.
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = true;
    const auto saveFloat = [&](const wchar_t* name, float value) {
        ok = writeString(key, name, formatFloat(value)) && ok;
    };
    const auto saveInt = [&](const wchar_t* name, int value) {
        ok = writeDword(key, name, static_cast<DWORD>(value)) && ok;
    };
    const auto saveInt64 = [&](const wchar_t* name, int64_t value) {
        ok = writeString(key, name, formatInt64(value)) && ok;
    };

    saveInt64(L"seed", s.seed);
    saveFloat(L"round_seconds", s.segmentSeconds);
    saveFloat(L"angle_min", s.segmentMinDeg);
    saveFloat(L"angle_max", s.segmentMaxDeg);
    saveInt(L"rounds_min", s.minSegments);
    saveInt(L"rounds_max", s.maxSegments);
    saveFloat(L"warp_seconds", s.warpSeconds);

    saveFloat(L"nebula_intensity", s.nebulaIntensity);
    saveFloat(L"nebula_size", s.nebulaSize);
    saveInt(L"nebula_types", s.nebulaTypes);
    saveFloat(L"nebula_dust", s.nebulaDust);
    saveFloat(L"nebula_warp", s.nebulaWarp);
    saveFloat(L"nebula_edge", s.nebulaEdge);
    saveInt(L"nebula_palette", s.nebulaPalette);
    saveInt(L"nebula_lobes_min", s.nebulaLobesMin);
    saveInt(L"nebula_lobes_max", s.nebulaLobesMax);
    saveInt(L"nebula_arcs_min", s.nebulaArcsMin);
    saveInt(L"nebula_arcs_max", s.nebulaArcsMax);
    saveInt(L"nebula_jets_min", s.nebulaJetsMin);
    saveInt(L"nebula_jets_max", s.nebulaJetsMax);

    saveInt(L"stars_faint", s.starsFaint);
    saveInt(L"stars_main", s.starsMain);
    saveInt(L"stars_bright", s.starsBright);
    saveInt(L"stars_featured", s.starsFeatured);
    saveFloat(L"star_size", s.starSize);
    saveFloat(L"star_brightness", s.starBrightness);
    saveFloat(L"star_band", s.starBand);
    saveFloat(L"star_twinkle", s.starTwinkle);
    saveFloat(L"star_spike_main", s.starSpikeMain);
    saveFloat(L"star_bokeh_main", s.starBokehMain);
    saveFloat(L"star_bokeh_bright", s.starBokehBright);
    saveFloat(L"star_doppler", s.starDoppler);
    saveFloat(L"star_warp", s.starWarp);
    saveFloat(L"star_scale_min", s.starScaleMin);
    saveFloat(L"star_scale_max", s.starScaleMax);

    RegCloseKey(key);
    return ok;
}

bool applySettingOverride(cfg::Settings& s, const std::wstring& key,
                          const std::wstring& value, std::wstring& error) {
    // Keys deliberately match registry value names. A few short aliases exist for
    // interactive benchmarking. Unknown keys are reported and do not abort startup.
    if (key == L"seed") {
        return setSeedOverride(value, s.seed, L"seed", error);
    }
    if (key == L"round" || key == L"round_seconds" || key == L"segment_seconds") {
        return setFloatOverride(value, s.segmentSeconds, L"round", error);
    }
    if (key == L"angle_min") {
        return setFloatOverride(value, s.segmentMinDeg, L"angle_min", error);
    }
    if (key == L"angle_max") {
        return setFloatOverride(value, s.segmentMaxDeg, L"angle_max", error);
    }
    if (key == L"rounds_min") {
        return setIntOverride(value, s.minSegments, L"rounds_min", error);
    }
    if (key == L"rounds_max") {
        return setIntOverride(value, s.maxSegments, L"rounds_max", error);
    }
    if (key == L"warp" || key == L"warp_seconds") {
        return setFloatOverride(value, s.warpSeconds, L"warp", error);
    }

    if (key == L"nebula_intensity") {
        return setFloatOverride(value, s.nebulaIntensity, L"nebula_intensity", error);
    }
    if (key == L"nebula_size") {
        return setFloatOverride(value, s.nebulaSize, L"nebula_size", error);
    }
    if (key == L"nebula_types") {
        return setIntOverride(value, s.nebulaTypes, L"nebula_types", error);
    }
    if (key == L"nebula_dust") {
        return setFloatOverride(value, s.nebulaDust, L"nebula_dust", error);
    }
    if (key == L"nebula_warp") {
        return setFloatOverride(value, s.nebulaWarp, L"nebula_warp", error);
    }
    if (key == L"nebula_edge") {
        return setFloatOverride(value, s.nebulaEdge, L"nebula_edge", error);
    }
    if (key == L"nebula_palette") {
        return setIntOverride(value, s.nebulaPalette, L"nebula_palette", error);
    }
    if (key == L"nebula_lobes_min") {
        return setIntOverride(value, s.nebulaLobesMin, L"nebula_lobes_min", error);
    }
    if (key == L"nebula_lobes_max") {
        return setIntOverride(value, s.nebulaLobesMax, L"nebula_lobes_max", error);
    }
    if (key == L"nebula_arcs_min") {
        return setIntOverride(value, s.nebulaArcsMin, L"nebula_arcs_min", error);
    }
    if (key == L"nebula_arcs_max") {
        return setIntOverride(value, s.nebulaArcsMax, L"nebula_arcs_max", error);
    }
    if (key == L"nebula_jets_min") {
        return setIntOverride(value, s.nebulaJetsMin, L"nebula_jets_min", error);
    }
    if (key == L"nebula_jets_max") {
        return setIntOverride(value, s.nebulaJetsMax, L"nebula_jets_max", error);
    }

    if (key == L"stars_faint") {
        return setIntOverride(value, s.starsFaint, L"stars_faint", error);
    }
    if (key == L"stars_main") {
        return setIntOverride(value, s.starsMain, L"stars_main", error);
    }
    if (key == L"stars_bright") {
        return setIntOverride(value, s.starsBright, L"stars_bright", error);
    }
    if (key == L"stars_featured") {
        return setIntOverride(value, s.starsFeatured, L"stars_featured", error);
    }
    if (key == L"star_size") {
        return setFloatOverride(value, s.starSize, L"star_size", error);
    }
    if (key == L"star_brightness") {
        return setFloatOverride(value, s.starBrightness, L"star_brightness", error);
    }
    if (key == L"star_band") {
        return setFloatOverride(value, s.starBand, L"star_band", error);
    }
    if (key == L"star_twinkle") {
        return setFloatOverride(value, s.starTwinkle, L"star_twinkle", error);
    }
    if (key == L"star_spike_main") {
        return setFloatOverride(value, s.starSpikeMain, L"star_spike_main", error);
    }
    if (key == L"star_bokeh_main") {
        return setFloatOverride(value, s.starBokehMain, L"star_bokeh_main", error);
    }
    if (key == L"star_bokeh_bright") {
        return setFloatOverride(value, s.starBokehBright, L"star_bokeh_bright", error);
    }
    if (key == L"star_doppler") {
        return setFloatOverride(value, s.starDoppler, L"star_doppler", error);
    }
    if (key == L"star_warp") {
        return setFloatOverride(value, s.starWarp, L"star_warp", error);
    }
    if (key == L"star_scale_min") {
        return setFloatOverride(value, s.starScaleMin, L"star_scale_min", error);
    }
    if (key == L"star_scale_max") {
        return setFloatOverride(value, s.starScaleMax, L"star_scale_max", error);
    }

    error = L"Неизвестный параметр: " + key;
    return false;
}

bool showSettingsDialog(HINSTANCE instance, HWND owner, cfg::Settings& settings) {
    DialogState state;
    state.target = &settings;
    state.edited = settings;

    const INT_PTR result =
        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, settingsDlgProc,
                        reinterpret_cast<LPARAM>(&state));

    return result == IDOK;
}

} // namespace sf
