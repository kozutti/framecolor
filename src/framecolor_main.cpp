#include "draw.h"
#include "i18n.h"
#include "lut_control.hpp"
#include "openvr.h"
#include "vr_overlay.h"

#include <cairo.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kWidth = 1200;
constexpr int kHeight = 890;
// The colour-LUT card sits under the title; everything below it moves down by this much.
constexpr double kLutShift = 190;
constexpr int kThumb = 256;
constexpr const char* kFont = "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc";
constexpr const char* kBold = "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc";
constexpr double kTrackX = 405;
constexpr double kTrackW = 625;
constexpr Color kBg{0.051, 0.067, 0.090};
constexpr Color kCard{0.086, 0.106, 0.133};
constexpr Color kControl{0.129, 0.149, 0.176};
constexpr Color kHover{0.188, 0.208, 0.239};
constexpr Color kBorder{0.431, 0.463, 0.506};
constexpr Color kText{0.902, 0.929, 0.953};
constexpr Color kMuted{0.569, 0.596, 0.631};
constexpr Color kAccent{0.886, 0.490, 0.992};
constexpr Color kGreen{0.247, 0.725, 0.314};
constexpr Color kRed{0.973, 0.318, 0.286};

volatile sig_atomic_t gStop = 0;
void stopHandler(int) { gStop = 1; }

double clamp(double value, double lo, double hi) { return std::max(lo, std::min(hi, value)); }

struct Values {
    double panel = 1.0;
    double contrast = 0.0;
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
    std::array<double, 3> channelContrast{{0.0, 0.0, 0.0}};
    std::array<double, 3> gamma{{1.0, 1.0, 1.0}};
    std::array<double, 3> saturation{{1.0, 1.0, 1.0}};
};

struct Preset { Values values; bool valid = false; };

enum class HitKind { Slider, ChannelMenu, ChannelChoice, Preset, SaveMode, Reset, LanguageMenu, LanguageChoice,
                     LutToggle, LutPrevious, LutNext };
struct Hit { double x, y, w, h; HitKind kind; int arg; };

struct UiState {
    Values values;
    std::array<Preset, 3> custom{};
    int channel = 0;       // 0=all, 1=red, 2=green, 3=blue
    int activeSlider = -1;
    bool channelOpen = false;
    bool languageOpen = false;
    bool resetConfirm = false;
    Language language = Language::English;
    bool saveMode = false;
    bool lookDirty = false;
    bool vulkanLayerActive = false;
    bool lutEnabled = false;
    bool legacyLut = false;
    std::string lutFile;
    std::vector<std::string> lutFiles;
    vkBasalt::LutStatus lutStatus;
    bool lutPending = false;  // selection written; waiting for the layer to report it
    std::chrono::steady_clock::time_point lutPendingSince{};
    std::string message;
    bool messageIsError = false;
};

const std::array<Preset, 3> kBuiltins{{
    {{1.00, 0.00, 1.00, 1.00, 1.00, {0.00, 0.00, 0.00}, {1.00, 1.00, 1.00}, {1.00, 1.00, 1.00}}, true},
    {{1.10, 0.30, 1.05, 1.05, 1.05, {0.30, 0.30, 0.30}, {1.08, 1.08, 1.08}, {1.00, 1.00, 1.00}}, true},
    {{0.70, 0.15, 1.05, 0.95, 0.82, {0.15, 0.15, 0.15}, {0.92, 0.92, 0.92}, {1.00, 1.00, 1.00}}, true},
}};

float getSetting(const char* key, float fallback) {
    if (vr::VRSettings() == nullptr) return fallback;  // e.g. --render-png without SteamVR
    vr::EVRSettingsError error = vr::VRSettingsError_None;
    const float value = vr::VRSettings()->GetFloat("steamvr", key, &error);
    return error == vr::VRSettingsError_None ? value : fallback;
}

void readValues(Values& v) {
    v.panel = getSetting("analogGain", 1.0f);
}

double average(const std::array<double, 3>& values);
bool setSetting(const char* key, double value, UiState& ui);

double average(const std::array<double, 3>& values) {
    return (values[0] + values[1] + values[2]) / 3.0;
}

bool setSetting(const char* key, double value, UiState& ui) {
    const auto& tr = getTranslations(ui.language);
    vr::EVRSettingsError error = vr::VRSettingsError_None;
    vr::VRSettings()->SetFloat("steamvr", key, static_cast<float>(value), &error);
    if (error != vr::VRSettingsError_None) {
        ui.message = std::string(tr.errorSaveSettings) + vr::VRSettings()->GetSettingsErrorNameFromEnum(error);
        ui.messageIsError = true;
        return false;
    }
    ui.message = tr.applied;
    ui.messageIsError = false;
    return true;
}

std::filesystem::path presetPath() {
    const char* config = std::getenv("XDG_CONFIG_HOME");
    if (config != nullptr && config[0] != '\0') return std::filesystem::path(config) / "framecolor" / "presets.txt";
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : "/tmp") / ".config" / "framecolor" / "presets.txt";
}

std::filesystem::path currentPath() { return presetPath().parent_path() / "current.txt"; }

std::filesystem::path lutControlPath() { return presetPath().parent_path() / "lut.txt"; }
std::filesystem::path lutStatusPath() { return presetPath().parent_path() / "lut-status.txt"; }

std::filesystem::path lutLibraryPath() {
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : "/tmp") / "Documents" / "FrameColor" / "luts";
}

bool hasCubeExtension(const std::string& name) {
    if (name.size() < 5) return false;
    std::string extension = name.substr(name.size() - 5);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".cube";
}

std::vector<std::string> scanLutLibrary() {
    const auto directory = lutLibraryPath();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    std::vector<std::string> files;
    std::filesystem::directory_iterator it(directory, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name.front() == '.' || !hasCubeExtension(name)) continue;
        std::error_code statusError;
        if (it->symlink_status(statusError).type() != std::filesystem::file_type::regular || statusError) continue;
        files.push_back(name);
    }
    std::sort(files.begin(), files.end());
    if (files.size() > 64) files.resize(64);
    return files;
}

bool saveLutSelection(const UiState& ui) {
    if (!ui.lutFile.empty() && (!vkBasalt::isSafeLutBasename(ui.lutFile) || !hasCubeExtension(ui.lutFile)))
        return false;
    const auto path = lutControlPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    const auto temporary = path.string() + ".tmp";
    std::ofstream out(temporary, std::ios::trunc);
    if (!out) return false;
    out << "enabled " << (ui.lutEnabled ? '1' : '0') << '\n';
    if (!ui.lutFile.empty()) out << "file " << ui.lutFile << '\n';
    out.close();
    if (!out) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) std::filesystem::remove(temporary, ec);
    return !ec;
}

void loadLutSelection(UiState& ui) {
    std::ifstream input(lutControlPath());
    if (input) {
        const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const auto selection = vkBasalt::parseLutSelection(contents);
        if (selection.valid) {
            ui.lutEnabled = selection.enabled;
            ui.lutFile = selection.file;
        } else {
            ui.lutStatus = {vkBasalt::LutStatusState::Error, {}, selection.error};
        }
    } else {
        std::error_code ec;
        const auto legacy = presetPath().parent_path() / "external-lut.cube";
        if (std::filesystem::is_regular_file(legacy, ec) && !ec) {
            ui.lutEnabled = true;
            ui.lutFile = legacy.filename().string();
            ui.legacyLut = true;
        }
    }
}

bool materializeLegacyLut(UiState& ui) {
    if (!ui.legacyLut) return true;
    const auto source = presetPath().parent_path() / "external-lut.cube";
    const auto directory = lutLibraryPath();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;
    auto destination = directory / "migrated-external-lut.cube";
    for (unsigned suffix = 2; std::filesystem::exists(destination, ec) && !ec; ++suffix)
        destination = directory / ("migrated-external-lut-" + std::to_string(suffix) + ".cube");
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
    if (ec) return false;
    ui.lutFile = destination.filename().string();
    ui.legacyLut = false;
    ui.lutFiles = scanLutLibrary();
    return true;
}

void setLutSaveError(UiState& ui) {
    ui.message = getTranslations(ui.language).lutWriteError;
    ui.messageIsError = true;
}

void readLutStatus(UiState& ui) {
    std::ifstream input(lutStatusPath());
    if (!input) return;
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    vkBasalt::LutStatus status;
    if (vkBasalt::parseLutStatus(contents, status) && status != ui.lutStatus)
        ui.lutStatus = std::move(status);
}

void neutralizeLegacyStreamGains(UiState& ui) {
    // Earlier releases wrote these stream-only gains. Leave them neutral so
    // that existing settings cannot double-apply the Vulkan LUT correction.
    for (int channel = 0; channel < 3; ++channel) {
        setSetting(channel == 0 ? "hmdDisplayColorGainR" : (channel == 1 ? "hmdDisplayColorGainG" : "hmdDisplayColorGainB"), 1.0, ui);
    }
}

void loadAdvancedValues(Values& values) {
    std::ifstream in(currentPath());
    std::vector<double> stored;
    for (double value; in >> value;) stored.push_back(value);
    if (stored.size() >= 6) {
        std::copy_n(stored.begin(), 3, values.channelContrast.begin());
        std::copy_n(stored.begin() + 3, 3, values.gamma.begin());
        if (stored.size() >= 9) {
            values.red = stored[6];
            values.green = stored[7];
            values.blue = stored[8];
        }
        if (stored.size() >= 12) {
            for (int channel = 0; channel < 3; ++channel)
                values.saturation[channel] = clamp(stored[9 + channel], 0.0, 2.0);
        } else if (stored.size() >= 10) {
            values.saturation.fill(clamp(stored[9], 0.0, 2.0));
        }
    } else {
        values.channelContrast = {values.contrast, values.contrast, values.contrast};
        values.gamma = {1.0, 1.0, 1.0};
    }
    values.contrast = average(values.channelContrast);
}

bool saveAdvancedValues(UiState& ui) {
    const auto path = currentPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto temporary = path.string() + ".tmp";
    std::ofstream out(temporary, std::ios::trunc);
    const auto& tr = getTranslations(ui.language);
    if (!out) {
        ui.message = tr.errorSaveAdvanced;
        ui.messageIsError = true;
        return false;
    }
    out << std::setprecision(9)
        << ui.values.channelContrast[0] << ' ' << ui.values.channelContrast[1] << ' ' << ui.values.channelContrast[2] << ' '
        << ui.values.gamma[0] << ' ' << ui.values.gamma[1] << ' ' << ui.values.gamma[2] << ' '
        << ui.values.red << ' ' << ui.values.green << ' ' << ui.values.blue << ' '
        << ui.values.saturation[0] << ' ' << ui.values.saturation[1] << ' '
        << ui.values.saturation[2] << '\n';
    out.close();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        ui.message = tr.errorSaveAdvanced;
        ui.messageIsError = true;
        return false;
    }
    return true;
}

void loadCustomPresets(UiState& ui) {
    std::ifstream in(presetPath());
    for (Preset& preset : ui.custom) {
        std::string line;
        if (!std::getline(in, line)) break;
        std::istringstream row(line);
        int valid = 0;
        if (!(row >> valid >> preset.values.panel >> preset.values.contrast >> preset.values.red
                  >> preset.values.green >> preset.values.blue)) break;
        if (!(row >> preset.values.channelContrast[0] >> preset.values.channelContrast[1] >> preset.values.channelContrast[2]
                  >> preset.values.gamma[0] >> preset.values.gamma[1] >> preset.values.gamma[2])) {
            preset.values.channelContrast = {preset.values.contrast, preset.values.contrast, preset.values.contrast};
            preset.values.gamma = {1.0, 1.0, 1.0};
        }
        std::vector<double> storedSaturation;
        for (double value; row >> value;) storedSaturation.push_back(value);
        if (storedSaturation.size() >= 3) {
            for (int channel = 0; channel < 3; ++channel)
                preset.values.saturation[channel] = clamp(storedSaturation[channel], 0.0, 2.0);
        } else if (!storedSaturation.empty()) {
            preset.values.saturation.fill(clamp(storedSaturation[0], 0.0, 2.0));
        }
        preset.valid = valid != 0;
    }
}

bool saveCustomPresets(UiState& ui) {
    const auto path = presetPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto temporary = path.string() + ".tmp";
    std::ofstream out(temporary, std::ios::trunc);
    const auto& tr = getTranslations(ui.language);
    if (!out) {
        ui.message = tr.errorSavePreset;
        ui.messageIsError = true;
        return false;
    }
    out << std::setprecision(9);
    for (const Preset& preset : ui.custom) {
        const Values& v = preset.values;
        out << (preset.valid ? 1 : 0) << ' ' << v.panel << ' ' << v.contrast << ' '
            << v.red << ' ' << v.green << ' ' << v.blue << ' '
            << v.channelContrast[0] << ' ' << v.channelContrast[1] << ' ' << v.channelContrast[2] << ' '
            << v.gamma[0] << ' ' << v.gamma[1] << ' ' << v.gamma[2] << ' '
            << v.saturation[0] << ' ' << v.saturation[1] << ' ' << v.saturation[2] << '\n';
    }
    out.close();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        ui.message = tr.errorSavePreset;
        ui.messageIsError = true;
        return false;
    }
    return true;
}

void applyPreset(const Preset& preset, UiState& ui) {
    const auto& tr = getTranslations(ui.language);
    if (!preset.valid) {
        ui.message = tr.slotEmpty;
        ui.messageIsError = true;
        return;
    }
    ui.values = preset.values;
    setSetting("analogGain", ui.values.panel, ui);
    ui.lookDirty = true;
    saveAdvancedValues(ui);
    readValues(ui.values);
    if (!ui.messageIsError) ui.message.clear();
}

void fillRect(const Pen& p, double x, double y, double w, double h, Color c, double radius = 12) {
    p.roundedRect(x, y, w, h, radius); p.color(c); cairo_fill(p.cr);
}

void strokeRect(const Pen& p, double x, double y, double w, double h, Color c, double radius = 12) {
    p.roundedRect(x, y, w, h, radius); p.color(c); cairo_set_line_width(p.cr, 2); cairo_stroke(p.cr);
}

bool inside(double px, double py, double x, double y, double w, double h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

void button(const Pen& p, std::vector<Hit>& hits, double x, double y, double w, double h,
            const std::string& label, HitKind kind, int arg, bool selected, bool usable,
            double pointerX, double pointerY) {
    const bool hovered = usable && inside(pointerX, pointerY, x, y, w, h);
    fillRect(p, x, y, w, h, selected ? kAccent : (hovered ? kHover : kControl));
    strokeRect(p, x, y, w, h, hovered ? kAccent : kBorder);
    const Color color = usable ? (selected ? kBg : kText) : kMuted;
    double fontSize = 21;
    double tw = p.measure(label, fontSize, true);
    if (tw > w - 12) {
        fontSize = std::max(13.0, std::floor(fontSize * (w - 12) / tw));
        tw = p.measure(label, fontSize, true);
    }
    p.text(x + (w - tw) / 2, y + h / 2 + fontSize * 0.38, label, fontSize, color, true);
    if (usable) hits.push_back({x, y, w, h, kind, arg});
}

std::string percent(double value) {
    char text[32]; std::snprintf(text, sizeof(text), "%.0f%%", value * 100.0); return text;
}

void slider(const Pen& p, std::vector<Hit>& hits, double y, const std::string& title,
            double value, double lo, double hi, int index, bool usable) {
    fillRect(p, 45, y, 1110, 78, kCard);
    p.text(70, y + 49, title, 25, usable ? kText : kMuted, true);
    const double cy = y + 39;
    p.color(usable ? kBorder : kControl);
    cairo_set_line_width(p.cr, 10); cairo_set_line_cap(p.cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(p.cr, kTrackX, cy); cairo_line_to(p.cr, kTrackX + kTrackW, cy); cairo_stroke(p.cr);
    const double fraction = clamp((value - lo) / (hi - lo), 0.0, 1.0);
    const double knobX = kTrackX + fraction * kTrackW;
    if (usable) {
        p.color(kAccent); cairo_set_line_width(p.cr, 10);
        cairo_move_to(p.cr, kTrackX, cy); cairo_line_to(p.cr, knobX, cy); cairo_stroke(p.cr);
    }
    p.color(usable ? kAccent : kMuted); cairo_arc(p.cr, knobX, cy, 16, 0, 2 * M_PI); cairo_fill(p.cr);
    std::string shown = percent(value);
    if (index == 3) { char text[24]; std::snprintf(text, sizeof(text), "%.2f", value); shown = text; }
    p.text(1120, y + 49, shown, 24, usable ? kText : kMuted, true, true);
    if (usable) hits.push_back({kTrackX - 20, y + 8, kTrackW + 40, 62, HitKind::Slider, index});
}

bool lutStatusMatchesSelection(const UiState& ui) {
    using vkBasalt::LutStatusState;
    if (!ui.lutEnabled || ui.lutFile.empty()) return ui.lutStatus.state == LutStatusState::Off;
    return ui.lutStatus.state != LutStatusState::Off && ui.lutStatus.file == ui.lutFile;
}

// Returns the status line and its colour for the colour-LUT card.
std::pair<std::string, Color> lutStatusLine(const UiState& ui, const Translations& tr) {
    using vkBasalt::LutStatusState;
    if (!ui.vulkanLayerActive) return {tr.lutLayerInactive, kMuted};
    if (ui.lutPending) return {tr.lutPending, kMuted};
    std::string text;
    Color color = kMuted;
    switch (ui.lutStatus.state) {
        case LutStatusState::Active: text = tr.lutStatusActive; color = kGreen; break;
        case LutStatusState::Off: text = tr.lutStatusOff; break;
        case LutStatusState::Error: text = tr.lutStatusError; color = kRed; break;
        case LutStatusState::Unsupported: text = tr.lutStatusUnsupported; color = kRed; break;
    }
    if (ui.lutStatus.state != LutStatusState::Off && !ui.lutStatus.message.empty() && ui.lutStatus.message != "-")
        text += " (" + ui.lutStatus.message + ")";
    return {text, color};
}

// Shortens text with a trailing ellipsis until it fits in width.
std::string fitText(const Pen& p, std::string text, double size, double width) {
    if (p.measure(text, size, true) <= width) return text;
    while (!text.empty()) {
        // Drop one UTF-8 code point: continuation bytes first, then the lead byte.
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (!text.empty()) text.pop_back();
        if (p.measure(text + "…", size, true) <= width) return text + "…";
    }
    return "…";
}

void lutCard(const Pen& p, std::vector<Hit>& hits, const UiState& ui, const Translations& tr,
             double pointerX, double pointerY) {
    const double y = 92;
    const bool usable = ui.vulkanLayerActive;
    const bool hasFiles = !ui.lutFiles.empty();
    fillRect(p, 45, y, 1110, 172, kCard);
    p.text(70, y + 40, tr.measuredLut, 25, usable ? kText : kMuted, true);
    const auto [statusText, statusColor] = lutStatusLine(ui, tr);
    p.text(1130, y + 40, fitText(p, statusText, 21, 760), 21, statusColor, true, true);

    button(p, hits, 70, y + 62, 160, 60, ui.lutEnabled ? tr.lutOn : tr.lutOff, HitKind::LutToggle, 0,
           ui.lutEnabled && usable, usable && (hasFiles || !ui.lutFile.empty()), pointerX, pointerY);
    button(p, hits, 250, y + 62, 64, 60, "<", HitKind::LutPrevious, 0, false, usable && hasFiles,
           pointerX, pointerY);
    button(p, hits, 1066, y + 62, 64, 60, ">", HitKind::LutNext, 0, false, usable && hasFiles,
           pointerX, pointerY);

    // File name field (not clickable; the arrows change it)
    const double fieldX = 324, fieldW = 732;
    fillRect(p, fieldX, y + 62, fieldW, 60, kBg);
    strokeRect(p, fieldX, y + 62, fieldW, 60, kBorder);
    std::string name = !ui.lutFile.empty() ? ui.lutFile : (hasFiles ? tr.lutNoSelection : tr.lutNoFiles);
    const bool showingFile = !ui.lutFile.empty();
    name = fitText(p, name, 22, fieldW - 40);
    p.text(fieldX + 20, y + 100, name, 22, showingFile && usable ? kText : kMuted, showingFile);
    p.text(70, y + 156, fitText(p, tr.lutFolderHint, 18, 1060), 18, kMuted);
}

double selectedGain(const UiState& ui) {
    if (ui.channel == 1) return ui.values.red;
    if (ui.channel == 2) return ui.values.green;
    if (ui.channel == 3) return ui.values.blue;
    return (ui.values.red + ui.values.green + ui.values.blue) / 3.0;
}

double selectedArrayValue(const std::array<double, 3>& values, int channel) {
    return channel == 0 ? average(values) : values[channel - 1];
}

void setSelectedArrayValue(std::array<double, 3>& values, int channel, double next, double lo, double hi) {
    if (channel == 0) {
        const double delta = next - average(values);
        for (double& value : values) value = clamp(value + delta, lo, hi);
    } else {
        values[channel - 1] = next;
    }
}

void updateSlider(UiState& ui, int index, double pointerX) {
    const double fraction = clamp((pointerX - kTrackX) / kTrackW, 0.0, 1.0);
    if (index == 0) {
        ui.values.panel = std::round((0.40 + fraction * 0.85) * 100.0) / 100.0;
        setSetting("analogGain", ui.values.panel, ui);
    } else if (index == 1) {
        const double next = std::round((0.50 + fraction) * 100.0) / 100.0;
        if (ui.channel == 0) {
            const double delta = next - selectedGain(ui);
            ui.values.red = clamp(ui.values.red + delta, 0.50, 1.50);
            ui.values.green = clamp(ui.values.green + delta, 0.50, 1.50);
            ui.values.blue = clamp(ui.values.blue + delta, 0.50, 1.50);
        } else {
            double* value = ui.channel == 1 ? &ui.values.red : (ui.channel == 2 ? &ui.values.green : &ui.values.blue);
            *value = next;
        }
        ui.lookDirty = true;
    } else if (index == 2) {
        const double next = std::round(fraction * 100.0) / 100.0;
        setSelectedArrayValue(ui.values.channelContrast, ui.channel, next, 0.0, 1.0);
        ui.lookDirty = true;
    } else if (index == 3) {
        const double next = std::round((0.50 + fraction * 2.0) * 100.0) / 100.0;
        setSelectedArrayValue(ui.values.gamma, ui.channel, next, 0.50, 2.50);
        ui.lookDirty = true;
    } else if (index == 4) {
        const double next = std::round(fraction * 200.0) / 100.0;
        setSelectedArrayValue(ui.values.saturation, ui.channel, next, 0.0, 2.0);
        ui.lookDirty = true;
    }
}

void render(const FontSet& fonts, const UiState& ui, double pointerX, double pointerY,
            std::vector<uint8_t>& rgba, std::vector<Hit>& hits, const char* pngPath = nullptr) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kWidth, kHeight);
    cairo_t* cr = cairo_create(surface); Pen p{cr, &fonts};
    cairo_set_source_rgb(cr, kBg.r, kBg.g, kBg.b); cairo_paint(cr);

    const auto& tr = getTranslations(ui.language);
    p.text(45, 51, tr.title, 34, kText, true);
    hits.clear();

    // Language dropdown button (top-right, aligned with Reset button right edge at 1155)
    const double langBtnW = 160;
    const double langBtnH = 44;
    const double langBtnX = 1155 - langBtnW;  // 995
    const double langBtnY = 20;
    const std::string langLabel = std::string(tr.displayName) + "    v";
    button(p, hits, langBtnX, langBtnY, langBtnW, langBtnH, langLabel,
           HitKind::LanguageMenu, 0, ui.languageOpen, true, pointerX, pointerY);

    lutCard(p, hits, ui, tr, pointerX, pointerY);

    const double d = kLutShift;
    const std::array<std::string, 6> names{{
        tr.builtinPresets[0],
        tr.builtinPresets[1],
        tr.builtinPresets[2],
        "P1", "P2", "P3"
    }};
    for (int i = 0; i < 6; ++i) {
        const bool valid = i < 3 || ui.custom[i - 3].valid;
        std::string label = names[i];
        if (i >= 3 && !valid) label += tr.emptySlotSuffix;
        button(p, hits, 45 + i * 137, d + 101, 125, 48, label, HitKind::Preset, i, false,
               !ui.saveMode || i >= 3, pointerX, pointerY);
    }
    button(p, hits, 885, d + 101, 125, 48, ui.saveMode ? tr.selectSlot : tr.save, HitKind::SaveMode, 0,
           ui.saveMode, true, pointerX, pointerY);
    button(p, hits, 1025, d + 101, 130, 48, ui.resetConfirm ? tr.resetConfirm : tr.reset,
           HitKind::Reset, 0, ui.resetConfirm, true, pointerX, pointerY);

    slider(p, hits, d + 174, tr.panelBrightness, ui.values.panel, 0.40, 1.25, 0, true);

    p.text(45, d + 289, tr.adjustmentChannel, 22, kText, true);
    button(p, hits, 885, d + 251, 270, 54, std::string(tr.channelNames[ui.channel]) + "    v",
           HitKind::ChannelMenu, 0, ui.channelOpen, true, pointerX, pointerY);

    slider(p, hits, d + 320, tr.brightness, selectedGain(ui), 0.50, 1.50, 1, ui.vulkanLayerActive);
    slider(p, hits, d + 410, tr.saturation, selectedArrayValue(ui.values.saturation, ui.channel),
           0.0, 2.0, 4, ui.vulkanLayerActive);
    slider(p, hits, d + 500, tr.contrast,
           selectedArrayValue(ui.values.channelContrast, ui.channel), 0.0, 1.0, 2, ui.vulkanLayerActive);
    slider(p, hits, d + 590, tr.gamma,
           selectedArrayValue(ui.values.gamma, ui.channel), 0.50, 2.50, 3, ui.vulkanLayerActive);

    if (!ui.message.empty()) {
        p.text(45, d + 680, ui.message, 20, ui.messageIsError ? kRed : kGreen);
    }

    // Dropdown popups are rendered last so they appear on top and hit-test first
    if (ui.channelOpen) {
        fillRect(p, 885, d + 307, 270, 224, kBg, 10);
        strokeRect(p, 885, d + 307, 270, 224, kAccent, 10);
        for (int i = 0; i < 4; ++i) {
            button(p, hits, 895, d + 317 + i * 51, 250, 43, tr.channelNames[i], HitKind::ChannelChoice, i,
                   ui.channel == i, true, pointerX, pointerY);
        }
    }

    if (ui.languageOpen) {
        const auto& langs = availableLanguages();
        const double menuX = langBtnX;
        const double menuY = langBtnY + langBtnH + 4;
        const double menuW = langBtnW;
        const double itemH = 42;
        const double menuH = 10 + langs.size() * itemH;
        fillRect(p, menuX, menuY, menuW, menuH, kBg, 10);
        strokeRect(p, menuX, menuY, menuW, menuH, kAccent, 10);
        for (size_t i = 0; i < langs.size(); ++i) {
            const auto& itemTr = getTranslations(langs[i]);
            button(p, hits, menuX + 8, menuY + 6 + i * itemH, menuW - 16, itemH - 4,
                   itemTr.displayName, HitKind::LanguageChoice, static_cast<int>(langs[i]),
                   ui.language == langs[i], true, pointerX, pointerY);
        }
    }

    if (pngPath != nullptr) cairo_surface_write_to_png(surface, pngPath);
    surfaceToRgba(surface, rgba); cairo_destroy(cr); cairo_surface_destroy(surface);
}

void thumbnail(const FontSet& fonts, std::vector<uint8_t>& rgba) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kThumb, kThumb);
    cairo_t* cr = cairo_create(surface); Pen p{cr, &fonts};
    cairo_set_source_rgb(cr, kBg.r, kBg.g, kBg.b); cairo_paint(cr);
    fillRect(p, 30, 44, 196, 150, kCard, 26); strokeRect(p, 30, 44, 196, 150, kAccent, 26);
    p.text(128, 137, "RGB", 57, kAccent, true, true);
    p.text(128, 224, "Display", 25, kText, true, true);
    surfaceToRgba(surface, rgba); cairo_destroy(cr); cairo_surface_destroy(surface);
}

const Hit* hitTest(const std::vector<Hit>& hits, double x, double y) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (inside(x, y, it->x, it->y, it->w, it->h)) return &*it;
    return nullptr;
}

void activate(const Hit& hit, double x, UiState& ui, VrOverlay& overlay) {
    const auto& tr = getTranslations(ui.language);
    switch (hit.kind) {
        case HitKind::Slider:
            ui.activeSlider = hit.arg;
            ui.channelOpen = false;
            ui.languageOpen = false;
            ui.resetConfirm = false;
            updateSlider(ui, hit.arg, x);
            break;
        case HitKind::ChannelMenu:
            ui.channelOpen = !ui.channelOpen;
            ui.languageOpen = false;
            ui.resetConfirm = false;
            break;
        case HitKind::ChannelChoice:
            ui.channel = hit.arg;
            ui.channelOpen = false;
            ui.resetConfirm = false;
            ui.message.clear();
            break;
        case HitKind::LanguageMenu:
            ui.languageOpen = !ui.languageOpen;
            ui.channelOpen = false;
            ui.resetConfirm = false;
            break;
        case HitKind::LanguageChoice:
            ui.language = static_cast<Language>(hit.arg);
            ui.languageOpen = false;
            ui.resetConfirm = false;
            ui.message.clear();
            saveLanguage(presetPath().parent_path(), ui.language);
            overlay.setOverlayName(getTranslations(ui.language).dashboardName);
            break;
        case HitKind::SaveMode:
            ui.saveMode = !ui.saveMode;
            ui.message = ui.saveMode ? tr.promptSaveSlot : tr.saveCanceled;
            ui.messageIsError = false;
            ui.channelOpen = false;
            ui.languageOpen = false;
            ui.resetConfirm = false;
            break;
        case HitKind::Preset:
            ui.resetConfirm = false;
            if (ui.saveMode) {
                if (hit.arg >= 3) {
                    ui.custom[hit.arg - 3] = {ui.values, true};
                    if (saveCustomPresets(ui)) {
                        ui.message = tr.formatSavedToSlot(hit.arg - 2);
                        ui.messageIsError = false;
                    }
                    ui.saveMode = false;
                }
            } else {
                applyPreset(hit.arg < 3 ? kBuiltins[hit.arg] : ui.custom[hit.arg - 3], ui);
            }
            ui.channelOpen = false;
            ui.languageOpen = false;
            break;
        case HitKind::Reset:
            if (!ui.resetConfirm) {
                ui.resetConfirm = true;
                ui.saveMode = false;
                ui.channelOpen = false;
                ui.languageOpen = false;
            } else {
                applyPreset(kBuiltins[0], ui);
                ui.resetConfirm = false;
                ui.channelOpen = false;
                ui.languageOpen = false;
                ui.saveMode = false;
            }
            break;
        case HitKind::LutToggle:
        case HitKind::LutPrevious:
        case HitKind::LutNext: {
            ui.channelOpen = false;
            ui.languageOpen = false;
            ui.resetConfirm = false;
            if (!materializeLegacyLut(ui)) {
                setLutSaveError(ui);
                break;
            }
            const std::string previousFile = ui.lutFile;
            const bool previousEnabled = ui.lutEnabled;
            if (hit.kind == HitKind::LutToggle) {
                ui.lutEnabled = !ui.lutEnabled;
                // Turning on with nothing selected picks the first file in the folder.
                if (ui.lutEnabled && ui.lutFile.empty() && !ui.lutFiles.empty()) ui.lutFile = ui.lutFiles.front();
            } else {
                if (ui.lutFiles.empty()) break;
                const auto current = std::find(ui.lutFiles.begin(), ui.lutFiles.end(), ui.lutFile);
                int index = current == ui.lutFiles.end() ? -1 : static_cast<int>(current - ui.lutFiles.begin());
                const int count = static_cast<int>(ui.lutFiles.size());
                if (hit.kind == HitKind::LutPrevious)
                    index = index < 0 ? count - 1 : (index + count - 1) % count;
                else
                    index = index < 0 ? 0 : (index + 1) % count;
                ui.lutFile = ui.lutFiles[static_cast<std::size_t>(index)];
                ui.lutEnabled = true;  // choosing a file implies ON
            }
            if (!saveLutSelection(ui)) {
                ui.lutFile = previousFile;
                ui.lutEnabled = previousEnabled;
                setLutSaveError(ui);
                break;
            }
            ui.lutPending = !lutStatusMatchesSelection(ui);
            ui.lutPendingSince = std::chrono::steady_clock::now();
            ui.message.clear();
            break;
        }
    }
}

void loadUiState(UiState& ui) {
    ui.language = loadSavedLanguage(presetPath().parent_path());
    readValues(ui.values); loadAdvancedValues(ui.values); loadCustomPresets(ui);
    const char* dataHome = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    const std::filesystem::path dataRoot = dataHome && *dataHome ? dataHome :
        std::filesystem::path(home ? home : "/tmp") / ".local/share";
    ui.vulkanLayerActive = std::filesystem::exists(dataRoot / "framecolor/layer/vkBasalt.json");
    ui.lutFiles = scanLutLibrary();
    loadLutSelection(ui);
    readLutStatus(ui);
}

} // namespace

int main(int argc, char** argv) {
    // Development aid: `framecolor --render-png out.png [en|ja]` draws the panel
    // from the current settings without SteamVR.
    if (argc >= 3 && std::string(argv[1]) == "--render-png") {
        FontSet fonts; fonts.load(kFont, kBold);
        UiState ui;
        loadUiState(ui);
        if (argc >= 4) ui.language = std::string(argv[3]) == "ja" ? Language::Japanese : Language::English;
        std::vector<uint8_t> pixels; std::vector<Hit> hits;
        render(fonts, ui, -1, -1, pixels, hits, argv[2]);
        return 0;
    }
    signal(SIGINT, stopHandler); signal(SIGTERM, stopHandler);
    VrOverlay overlay; std::string error;
    while (!gStop) {
        const auto result = overlay.connect(kWidth, kHeight, error);
        if (result == VrOverlay::ConnectResult::Ok) break;
        if (result == VrOverlay::ConnectResult::Error) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    if (gStop) return 0;
    FontSet fonts; fonts.load(kFont, kBold);
    std::vector<uint8_t> pixels; thumbnail(fonts, pixels); overlay.submitThumbnail(pixels.data(), kThumb);
    UiState ui;
    loadUiState(ui);
    overlay.setOverlayName(getTranslations(ui.language).dashboardName);
    setSetting("contrast", 0.0, ui);
    neutralizeLegacyStreamGains(ui);
    auto lastLookSave = std::chrono::steady_clock::now();
    auto nextLutScan = lastLookSave + std::chrono::seconds(2);
    auto nextLutStatus = lastLookSave + std::chrono::milliseconds(500);
    std::vector<Hit> hits; double px = -1, py = -1; bool dirty = true; bool wasVisible = false;
    while (!gStop && overlay.steamVrAlive()) {
        const VrEvents events = overlay.pollEvents();
        if (events.quit || events.closeRequested) break;
        for (const auto& e : events.pointer) {
            if (e.type == PointerInput::Type::Move) {
                px = e.x; py = e.y;
                if (ui.activeSlider >= 0) updateSlider(ui, ui.activeSlider, px);
                dirty = true;
            } else if (e.type == PointerInput::Type::Leave) {
                px = py = -1; ui.activeSlider = -1; dirty = true;
            } else if (e.type == PointerInput::Type::Down) {
                if (const Hit* hit = hitTest(hits, e.x, e.y)) activate(*hit, e.x, ui, overlay);
                else {
                    ui.channelOpen = false;
                    ui.languageOpen = false;
                    ui.resetConfirm = false;
                }
                dirty = true;
            } else if (e.type == PointerInput::Type::Up) {
                ui.activeSlider = -1; readValues(ui.values);
                if (saveAdvancedValues(ui)) ui.lookDirty = false;
                dirty = true;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextLutScan) {
            const auto previousFiles = ui.lutFiles;
            ui.lutFiles = scanLutLibrary();
            if (ui.lutFiles != previousFiles) dirty = true;
            nextLutScan = now + std::chrono::seconds(2);
        }
        if (now >= nextLutStatus || (ui.lutPending && now - ui.lutPendingSince >= std::chrono::milliseconds(150))) {
            const auto previousStatus = ui.lutStatus;
            readLutStatus(ui);
            if (ui.lutStatus != previousStatus) dirty = true;
            // Stop showing "switching" once the layer reports the new selection,
            // or after 3 s so a missing layer cannot leave it stuck.
            if (ui.lutPending && (lutStatusMatchesSelection(ui) || now - ui.lutPendingSince >= std::chrono::seconds(3))) {
                ui.lutPending = false;
                dirty = true;
            }
            nextLutStatus = now + std::chrono::milliseconds(ui.lutPending ? 150 : 500);
        }
        if (ui.vulkanLayerActive && ui.lookDirty &&
            (ui.activeSlider < 0 || now - lastLookSave >= std::chrono::milliseconds(150))) {
            if (saveAdvancedValues(ui)) {
                ui.lookDirty = false;
                lastLookSave = now;
            }
        }
        const bool visible = overlay.panelVisible();
        if (visible && (!wasVisible || dirty)) {
            render(fonts, ui, px, py, pixels, hits); overlay.submitPanel(pixels.data()); dirty = false;
        }
        wasVisible = visible;
        std::this_thread::sleep_for(std::chrono::milliseconds(visible ? 33 : 250));
    }
    overlay.shutdown();
    return 0;
}
