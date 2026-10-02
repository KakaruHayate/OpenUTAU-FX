#include "OpenUtauFX.h"
#include "IPlug_include_in_plug_src.h"

#ifdef OS_WIN
#include <windows.h>

// IDI_ICON1, the icon resources/main.rc embeds in the binaries.
#include "../../resources/resource.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "FxDisplays.h"
#include "MixFxPresets.h"
#include "RackLayout.h"

using namespace iplug;
using namespace igraphics;

using oufx::dsp::Chain;
using oufx::dsp::kCompPresets;
using oufx::dsp::kEqPresets;
using oufx::dsp::kNumRackPresets;
using oufx::dsp::kRackPresets;
using oufx::dsp::kReverbPresets;
using oufx::ui::ComboControl;
using oufx::ui::CompCurve;
using oufx::ui::DropdownLayer;
using oufx::ui::EqCurve;
using oufx::ui::kCompPalette;
using oufx::ui::kControlPalette;
using oufx::ui::kEqPalette;
using oufx::ui::NameControl;
using oufx::ui::PlatePalette;
using oufx::ui::PowerSwitchControl;
using oufx::ui::RackBackgroundControl;
using oufx::ui::RackButtonControl;
using oufx::ui::RackGeometry;
using oufx::ui::RackKnobControl;
using oufx::ui::RackLabelControl;
using oufx::ui::RackTheme;
using oufx::ui::ReverbCurve;
using oufx::ui::Rgb;
using oufx::ui::Rgba;
using oufx::ui::ValueReadoutControl;

// ── Value formatting ────────────────────────────────────────────────────────
// The readouts under the knobs, formatted the way the dialog's StringFormat
// expressions do it.

static void FormatSignedDb(char* buf, size_t len, double v)
{
  if (std::abs(v) < 0.05)
    snprintf(buf, len, "0.0 dB");
  else
    snprintf(buf, len, "%+.1f dB", v);
}

static void FormatDb1(char* buf, size_t len, double v) { snprintf(buf, len, "%.1f dB", v); }
static void FormatHz(char* buf, size_t len, double v) { snprintf(buf, len, "%.0f Hz", v); }
static void FormatRatio(char* buf, size_t len, double v) { snprintf(buf, len, "%.1f:1", v); }
static void FormatUnit2(char* buf, size_t len, double v) { snprintf(buf, len, "%.2f", v); }
static void FormatMs(char* buf, size_t len, double v) { snprintf(buf, len, "%.0f ms", v); }

// ── Window icon ─────────────────────────────────────────────────────────────

#ifdef OS_WIN
/** The standalone app's main window is a dialog, and a dialog carries no icon
 *  of its own: the title bar would be blank and the taskbar would fall back to
 *  whatever the shell happens to have cached for the executable.  Set it from
 *  the icon resource the .rc embeds, at both sizes Windows asks for. */
static void SetAppWindowIcon(IGraphics* pGraphics)
{
  auto* pView = static_cast<HWND>(pGraphics->GetWindow());
  if (!pView)
    return;

  HWND hTop = GetAncestor(pView, GA_ROOT);
  if (!hTop)
    return;

  auto* hInst = GetModuleHandleW(nullptr);
  // Not named "small": rpcndr.h defines that as a macro for char.
  const int bigSize = GetSystemMetrics(SM_CXICON);
  const int smallSize = GetSystemMetrics(SM_CXSMICON);

  if (auto* hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1),
                                                  IMAGE_ICON, bigSize, bigSize, LR_SHARED)))
    SendMessageW(hTop, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));

  if (auto* hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1),
                                                  IMAGE_ICON, smallSize, smallSize, LR_SHARED)))
    SendMessageW(hTop, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
}
#else
static void SetAppWindowIcon(IGraphics*) {}
#endif

// ── Plugin ──────────────────────────────────────────────────────────────────

OpenUtauFX::OpenUtauFX(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Master and per-module power, as UMixFx stores them.
  GetParam(kParamEnabled)->InitBool("Power", true);
  GetParam(kParamEqOn)->InitBool("EQ On", true);
  GetParam(kParamCompOn)->InitBool("Comp On", true);
  GetParam(kParamReverbOn)->InitBool("Reverb On", true);

  // Module presets.  The list order must match the preset tables in
  // MixFxPresets.h; the parameter's value is the index into those tables.
  std::initializer_list<const char*> eqNames = { "Off", "Vocal Air", "Warm", "Demud", "Telephone" };
  std::initializer_list<const char*> compNames = { "Off", "Gentle", "Pop", "Limit" };
  std::initializer_list<const char*> reverbNames = { "Off", "Small Room", "Vocal Plate", "Hall", "Ambient" };

  GetParam(kParamEqPreset)->InitEnum("EQ Preset", 1, eqNames);
  GetParam(kParamCompPreset)->InitEnum("Comp Preset", 1, compNames);
  GetParam(kParamReverbPreset)->InitEnum("Reverb Preset", 1, reverbNames);

  // Knob ranges, taken from the dialog's Minimum / Maximum / DefaultValue.
  GetParam(kParamEqLow)->InitDouble("EQ Low", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kParamEqMidFreq)->InitDouble("EQ Frequency", 3000.0, 200.0, 6000.0, 1.0, "Hz");
  GetParam(kParamEqMid)->InitDouble("EQ Mid", 1.5, -12.0, 12.0, 0.1, "dB");
  GetParam(kParamEqHigh)->InitDouble("EQ High", 3.0, -12.0, 12.0, 0.1, "dB");

  GetParam(kParamCompThreshold)->InitDouble("Threshold", -18.0, -40.0, 0.0, 0.1, "dB");
  GetParam(kParamCompRatio)->InitDouble("Ratio", 2.0, 1.0, 20.0, 0.1, ":1");
  GetParam(kParamCompMakeup)->InitDouble("Makeup", 2.5, 0.0, 12.0, 0.1, "dB");

  GetParam(kParamReverbSize)->InitDouble("Size", 0.30, 0.0, 1.0, 0.01, "");
  GetParam(kParamReverbDamp)->InitDouble("Damping", 0.70, 0.0, 1.0, 0.01, "");
  GetParam(kParamReverbWet)->InitDouble("Wet", 1.0, 0.0, 2.0, 0.01, "");
  GetParam(kParamReverbPreDelay)->InitDouble("Pre-Delay", 12.0, 0.0, 200.0, 1.0, "ms");

  mMakeGraphicsFunc = [&]()
  {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics)
  {
    pGraphics->LoadFont("Roboto", ROBOTO_FN);
    pGraphics->LoadFont("RobotoBold", ROBOTO_BOLD_FN);
    pGraphics->LoadFont("RobotoMono", ROBOTO_MONO_FN);
    pGraphics->EnableMouseOver(true);

    const IRECT bounds = pGraphics->GetBounds();
    mGeometry = RackGeometry::Compute(bounds);

    pGraphics->AttachControl(new RackBackgroundControl(bounds, mGeometry, mTheme), kCtrlTagBackground);

    // The drop-down list is attached last so it draws above the rack; the
    // combos hold a reference to it and hand it their item list on click.
    auto* pDropdown = new DropdownLayer(bounds);

    // ── Knob cell: label, knob, value readout ──────────────────────────────
    auto attachKnobCell = [&](const IRECT& cell, const char* label, int paramIdx,
                              const PlatePalette& palette, const IColor& cap, const IColor& pointer,
                              float maxSize, oufx::ui::ValueFormatter formatter)
    {
      constexpr float labelH = 14.f;
      constexpr float valueH = 14.f;

      const IRECT labelRect = cell.GetFromTop(labelH);
      const IRECT valueRect = cell.GetFromBottom(valueH);
      const IRECT knobArea(cell.L, labelRect.B, cell.R, valueRect.T);
      const float size = std::min({ maxSize, knobArea.W(), knobArea.H() });

      pGraphics->AttachControl(new RackLabelControl(labelRect, label,
        IText(11.f, palette.text, "RobotoBold", EAlign::Center, EVAlign::Middle)));
      pGraphics->AttachControl(new RackKnobControl(knobArea.GetCentredInside(size, size), paramIdx, palette, cap, pointer));
      pGraphics->AttachControl(new ValueReadoutControl(valueRect, paramIdx, formatter, palette.textDim));
    };

    auto attachFixedLabel = [&](const IRECT& rect, const char* text, const PlatePalette& palette)
    {
      pGraphics->AttachControl(new RackLabelControl(rect, text,
        IText(11.f, palette.textDim, "RobotoMono", EAlign::Center, EVAlign::Middle)));
    };

    // ── Top bar: track name, recommended rack, master power ────────────────
    {
      const IRECT bar = mGeometry.topBar.GetPadded(-12.f, -8.f, -12.f, -8.f);

      const IRECT caption(bar.L, bar.T, bar.L + 46.f, bar.B);
      pGraphics->AttachControl(new RackLabelControl(caption, "TRACK",
        IText(11.f, mTheme.textDim, "RobotoBold", EAlign::Near, EVAlign::Middle)));

      pGraphics->AttachControl(new NameControl(IRECT(caption.R + 10.f, bar.T, caption.R + 260.f, bar.B),
        mTrackName, IText(14.f, mTheme.text, "RobotoBold", EAlign::Near, EVAlign::Middle),
        [this](const std::string& name) { mTrackName = name; }));

      const IRECT power(bar.R - 100.f, bar.T, bar.R, bar.B);
      pGraphics->AttachControl(new PowerSwitchControl(power, kParamEnabled, "POWER", mTheme,
        [this]() { return !GetParam(kParamEnabled)->Bool(); }));

      const IRECT recommended(power.L - 20.f - 120.f, bar.T + 1.f, power.L - 20.f, bar.B - 1.f);
      pGraphics->AttachControl(new RackButtonControl(recommended, "RECOMMENDED",
        [this]() { ApplyLibraryEntry(0); }));
    }

    // ── Bottom bar: preset library ─────────────────────────────────────────
    {
      const IRECT bar = mGeometry.bottomBar.GetPadded(-12.f, -8.f, -12.f, -8.f);

      const IRECT caption(bar.L, bar.T, bar.L + 58.f, bar.B);
      pGraphics->AttachControl(new RackLabelControl(caption, "LIBRARY",
        IText(11.f, mTheme.textDim, "RobotoBold", EAlign::Near, EVAlign::Middle)));

      const IRECT combo(caption.R + 10.f, bar.MH() - 13.f, caption.R + 10.f + 240.f, bar.MH() + 13.f);

      pGraphics->AttachControl(new ComboControl(combo, kControlPalette, *pDropdown,
        [this]() { return LibraryNames(); },
        [this]() { return mLibraryIndex; },
        [this](int index) { ApplyLibraryEntry(index); },
        false), kCtrlTagLibraryCombo);

      const IRECT save(combo.R + 6.f, combo.T, combo.R + 76.f, combo.B);
      pGraphics->AttachControl(new RackButtonControl(save, "SAVE", [this]() { BeginSaveUserPreset(); }));

      const IRECT remove(save.R + 6.f, combo.T, save.R + 76.f, combo.B);
      pGraphics->AttachControl(new RackButtonControl(remove, "DELETE", [this]() { DeleteUserPreset(); }));

      const IRECT defaults(bar.R - 70.f, combo.T, bar.R, combo.B);
      pGraphics->AttachControl(new RackButtonControl(defaults, "DEFAULT",
        [this]() { ApplyLibraryEntry(0); }));

      const IRECT reset(defaults.L - 8.f - 70.f, combo.T, defaults.L - 8.f, combo.B);
      pGraphics->AttachControl(new RackButtonControl(reset, "RESET",
        [this]() { ApplyLibraryEntry(1); }));
    }

    // ── EQ faceplate ───────────────────────────────────────────────────────
    {
      const auto& p = mGeometry.plates[0];
      const PlatePalette& palette = kEqPalette;

      pGraphics->AttachControl(new PowerSwitchControl(
        IRECT(p.inner.R - 48.f, p.title.T, p.inner.R, p.title.B), kParamEqOn, "", mTheme,
        [this]() { return !GetParam(kParamEnabled)->Bool(); }));

      pGraphics->AttachControl(new EqCurve(p.display,
        { kParamEqLow, kParamEqMidFreq, kParamEqMid, kParamEqHigh }, palette,
        [this]() { return GetParam(kParamEqLow)->Value(); },
        [this]() { return GetParam(kParamEqMidFreq)->Value(); },
        [this]() { return GetParam(kParamEqMid)->Value(); },
        [this]() { return GetParam(kParamEqHigh)->Value(); }));

      pGraphics->AttachControl(new ComboControl(p.combo, palette, *pDropdown,
        [this]() { return std::vector<std::string>{ "Off", "Vocal Air", "Warm", "Demud", "Telephone" }; },
        [this]() { return static_cast<int>(GetParam(kParamEqPreset)->Value()); },
        [this](int index) { ApplyEqPreset(index); },
        true));

      const IRECT low = p.knobRow.SubRectHorizontal(3, 0);
      const IRECT mid = p.knobRow.SubRectHorizontal(3, 1);
      const IRECT high = p.knobRow.SubRectHorizontal(3, 2);

      attachKnobCell(low, "LOW", kParamEqLow, palette, palette.knobCap, palette.knobPointer, 68.f, FormatSignedDb);
      attachKnobCell(mid, "MID", kParamEqMid, palette, palette.knobCap, palette.knobPointer, 68.f, FormatSignedDb);
      attachKnobCell(high, "HIGH", kParamEqHigh, palette, palette.knobCap, palette.knobPointer, 68.f, FormatSignedDb);

      attachFixedLabel(p.bottomRow.SubRectHorizontal(3, 0), "200 Hz", palette);
      attachKnobCell(p.bottomRow.SubRectHorizontal(3, 1), "FREQUENCY", kParamEqMidFreq, palette,
                     Rgb(0x3F, 0x7F, 0xB0), Rgb(0xF2, 0xF6, 0xFA), 52.f, FormatHz);
      attachFixedLabel(p.bottomRow.SubRectHorizontal(3, 2), "8 kHz", palette);
    }

    // ── Compressor faceplate ───────────────────────────────────────────────
    {
      const auto& p = mGeometry.plates[1];
      const PlatePalette& palette = kCompPalette;

      pGraphics->AttachControl(new PowerSwitchControl(
        IRECT(p.inner.R - 48.f, p.title.T, p.inner.R, p.title.B), kParamCompOn, "", mTheme,
        [this]() { return !GetParam(kParamEnabled)->Bool(); }));

      pGraphics->AttachControl(new CompCurve(p.display,
        { kParamCompThreshold, kParamCompRatio, kParamCompMakeup }, palette,
        [this]() { return GetParam(kParamCompThreshold)->Value(); },
        [this]() { return GetParam(kParamCompRatio)->Value(); },
        [this]() { return GetParam(kParamCompMakeup)->Value(); }));

      pGraphics->AttachControl(new ComboControl(p.combo, palette, *pDropdown,
        [this]() { return std::vector<std::string>{ "Off", "Gentle", "Pop", "Limit" }; },
        [this]() { return static_cast<int>(GetParam(kParamCompPreset)->Value()); },
        [this](int index) { ApplyCompPreset(index); },
        true));

      attachKnobCell(p.knobRow.SubRectHorizontal(2, 0), "THRESHOLD", kParamCompThreshold, palette,
                     palette.knobCap, palette.knobPointer, 68.f, FormatDb1);
      attachKnobCell(p.knobRow.SubRectHorizontal(2, 1), "RATIO", kParamCompRatio, palette,
                     palette.knobCap, palette.knobPointer, 68.f, FormatRatio);

      attachKnobCell(p.bottomRow.GetCentredInside(120.f, p.bottomRow.H()), "MAKEUP", kParamCompMakeup, palette,
                     palette.knobCap, palette.knobPointer, 52.f, FormatDb1);
    }

    // ── Reverb faceplate ───────────────────────────────────────────────────
    {
      const auto& p = mGeometry.plates[2];
      const PlatePalette& palette = oufx::ui::kReverbPalette;

      pGraphics->AttachControl(new PowerSwitchControl(
        IRECT(p.inner.R - 48.f, p.title.T, p.inner.R, p.title.B), kParamReverbOn, "", mTheme,
        [this]() { return !GetParam(kParamEnabled)->Bool(); }));

      pGraphics->AttachControl(new ReverbCurve(p.display,
        { kParamReverbSize, kParamReverbDamp, kParamReverbWet, kParamReverbPreDelay, kParamReverbPreset }, palette,
        Rgb(0xF2, 0xDF, 0xA8),
        [this]() { return GetParam(kParamReverbSize)->Value(); },
        [this]() { return GetParam(kParamReverbDamp)->Value(); },
        [this]() { return GetParam(kParamReverbWet)->Value(); },
        [this]() { return GetParam(kParamReverbPreDelay)->Value(); },
        [this]() { return static_cast<int>(GetParam(kParamReverbPreset)->Value()); }));

      pGraphics->AttachControl(new ComboControl(p.combo, palette, *pDropdown,
        [this]() { return std::vector<std::string>{ "Off", "Small Room", "Vocal Plate", "Hall", "Ambient" }; },
        [this]() { return static_cast<int>(GetParam(kParamReverbPreset)->Value()); },
        [this](int index) { ApplyReverbPreset(index, 1.0); },
        true));

      attachKnobCell(p.knobRow.SubRectHorizontal(2, 0), "SIZE", kParamReverbSize, palette,
                     palette.knobCap, palette.knobPointer, 68.f, FormatUnit2);
      attachKnobCell(p.knobRow.SubRectHorizontal(2, 1), "DAMPING", kParamReverbDamp, palette,
                     palette.knobCap, palette.knobPointer, 68.f, FormatUnit2);

      attachKnobCell(p.bottomRow.SubRectHorizontal(2, 0), "PRE-DELAY", kParamReverbPreDelay, palette,
                     palette.knobCap, palette.knobPointer, 52.f, FormatMs);
      attachKnobCell(p.bottomRow.SubRectHorizontal(2, 1), "WET", kParamReverbWet, palette,
                     palette.knobCap, palette.knobPointer, 52.f, FormatUnit2);
    }

    pGraphics->AttachControl(pDropdown, kCtrlTagDropdown);

    SetAppWindowIcon(pGraphics);
  };
}

bool OpenUtauFX::OnHostRequestingProductHelp()
{
  auto* pGraphics = GetUI();
  if (!pGraphics)
    return false;

  // The standalone app's Help menu.  Keep this to what fits a message box: it
  // is the whole manual a user gets without leaving the plug-in.
  static const char* kManual =
    "OpenUtau FX - Track Polish rack\n"
    "\n"
    "EQ > Compressor > Reverb, each behind its own power switch.\n"
    "POWER (top right) bypasses the whole rack.\n"
    "\n"
    "EQ: LOW / MID / HIGH are +/-12 dB. FREQUENCY (200 Hz - 6 kHz) is the\n"
    "MID band's centre. Fixed shelves sit at 200 Hz and 8 kHz.\n"
    "\n"
    "Compressor: THRESHOLD -40 - 0 dB, RATIO 1:1 - 20:1, MAKEUP 0 - 12 dB,\n"
    "with a fixed 6 dB soft knee. Attack and release come from the preset.\n"
    "\n"
    "Reverb: SIZE and DAMPING 0 - 1, PRE-DELAY 0 - 200 ms, WET 0 - 2, which\n"
    "trims the wet level the preset already carries.\n"
    "\n"
    "Knobs: drag vertically, hold Shift for fine, the wheel or the arrow keys\n"
    "step, and a double-click resets to the default.\n"
    "\n"
    "Presets: each faceplate's strip picks that module's preset. The LIBRARY\n"
    "strip loads a whole rack; SAVE and DELETE manage your own entries.\n"
    "RESET flattens the rack, DEFAULT and RECOMMENDED load the recommended\n"
    "one.\n"
    "\n"
    PLUG_URL_STR;

  pGraphics->ShowMessageBox(kManual, PLUG_NAME " - manual", kMB_OK);
  return true;
}

// ── Parameters ──────────────────────────────────────────────────────────────

void OpenUtauFX::SetParamValue(int paramIdx, double value)
{
  const IParam* pParam = GetParam(paramIdx);
  if (!pParam)
    return;

  SendParameterValueFromUI(paramIdx, pParam->ToNormalized(value));

  // Nothing else marks the controls dirty on this path.  A control only
  // repaints itself when the change came from it, and a standalone host never
  // echoes the value back the way a DAW does, so the rack would otherwise keep
  // showing the values it had before the preset was applied.
  RefreshControls();
}

void OpenUtauFX::OnParamChangeUI(int paramIdx, EParamSource source)
{
  // Every module switch dims its LED while the rack's master power is off, so
  // they all have to repaint when it moves.  Only the master's own control is
  // linked to that parameter, which leaves the other three stale otherwise.
  if (paramIdx == kParamEnabled)
    RefreshControls();
}

// ── Module presets ──────────────────────────────────────────────────────────
// Selecting a preset writes the faceplate's knobs, not just the selector: the
// DSP reads the knobs, so setting the index alone would change nothing you can
// hear.  The compressor's attack and release, and the reverb's width and dry
// level, are not knobs and keep coming straight from the preset table.

void OpenUtauFX::ApplyEqPreset(int index)
{
  index = std::clamp(index, 0, oufx::dsp::kNumEqPresets - 1);
  const auto& preset = kEqPresets[index];

  SetParamValue(kParamEqPreset, static_cast<double>(index));
  SetParamValue(kParamEqLow, preset.lowDb);
  SetParamValue(kParamEqMidFreq, preset.midFreq);
  SetParamValue(kParamEqMid, preset.midDb);
  SetParamValue(kParamEqHigh, preset.highDb);
}

void OpenUtauFX::ApplyCompPreset(int index)
{
  index = std::clamp(index, 0, oufx::dsp::kNumCompPresets - 1);
  const auto& preset = kCompPresets[index];

  SetParamValue(kParamCompPreset, static_cast<double>(index));
  SetParamValue(kParamCompThreshold, preset.thresholdDb);
  SetParamValue(kParamCompRatio, preset.ratio);
  SetParamValue(kParamCompMakeup, preset.makeupDb);
}

void OpenUtauFX::ApplyReverbPreset(int index, double wetTrim)
{
  index = std::clamp(index, 0, oufx::dsp::kNumReverbPresets - 1);
  const auto& preset = kReverbPresets[index];

  SetParamValue(kParamReverbPreset, static_cast<double>(index));
  SetParamValue(kParamReverbSize, preset.roomSize);
  SetParamValue(kParamReverbDamp, preset.damp);
  SetParamValue(kParamReverbWet, wetTrim);
  SetParamValue(kParamReverbPreDelay, preset.preDelayMs);
}

Chain::Params OpenUtauFX::BuildChainParams() const
{
  const int eqIndex = std::clamp(static_cast<int>(GetParam(kParamEqPreset)->Value()), 0, 4);
  const int compIndex = std::clamp(static_cast<int>(GetParam(kParamCompPreset)->Value()), 0, 3);
  const int reverbIndex = std::clamp(static_cast<int>(GetParam(kParamReverbPreset)->Value()), 0, 4);

  const auto& eqPreset = kEqPresets[eqIndex];
  const auto& compPreset = kCompPresets[compIndex];
  const auto& reverbPreset = kReverbPresets[reverbIndex];

  Chain::Params p;
  p.enabled = GetParam(kParamEnabled)->Bool();
  p.eqOn = GetParam(kParamEqOn)->Bool();
  p.compOn = GetParam(kParamCompOn)->Bool();
  p.reverbOn = GetParam(kParamReverbOn)->Bool();

  p.eqLowDb = GetParam(kParamEqLow)->Value();
  p.eqMidFreq = GetParam(kParamEqMidFreq)->Value();
  p.eqMidQ = oufx::dsp::kEqMidQ;
  p.eqMidDb = GetParam(kParamEqMid)->Value();
  p.eqHighDb = GetParam(kParamEqHigh)->Value();

  // Attack/release come from the preset table, as MixFxSource does.
  p.compThresholdDb = GetParam(kParamCompThreshold)->Value();
  p.compRatio = GetParam(kParamCompRatio)->Value();
  p.compAttackMs = compPreset.attackMs;
  p.compReleaseMs = compPreset.releaseMs;
  p.compMakeupDb = GetParam(kParamCompMakeup)->Value();

  // The WET knob trims the preset's own wet level.
  p.reverbRoomSize = GetParam(kParamReverbSize)->Value();
  p.reverbDamp = GetParam(kParamReverbDamp)->Value();
  p.reverbWidth = reverbPreset.width;
  p.reverbWet = reverbPreset.wet * GetParam(kParamReverbWet)->Value();
  p.reverbDry = reverbPreset.dry;
  p.reverbPreDelayMs = GetParam(kParamReverbPreDelay)->Value();

  return p;
}

void OpenUtauFX::OnParamChange(int paramIdx)
{
  mParamsDirty.store(true);
}

// ── Audio ───────────────────────────────────────────────────────────────────

void OpenUtauFX::OnReset()
{
  const double sampleRate = GetSampleRate() > 0.0 ? GetSampleRate() : 44100.0;

  if (!mChain || mChainSampleRate != sampleRate)
  {
    mChainSampleRate = sampleRate;
    mChain = std::make_unique<Chain>(sampleRate, 2);
    mChain->SetParams(BuildChainParams());
    mChain->Prepare(kMaxBlockFrames);
    mParamsDirty.store(false);
  }

  mChain->Reset();

  if (static_cast<int>(mScratch.size()) < kMaxBlockFrames * 2)
    mScratch.assign(static_cast<size_t>(kMaxBlockFrames) * 2, 0.f);
}

void OpenUtauFX::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nIn = NInChansConnected();
  const int nOut = NOutChansConnected();
  const int nCh = std::min(nIn, nOut);

  for (int ch = 0; ch < nOut; ch++)
  {
    if (ch < nCh)
      memcpy(outputs[ch], inputs[ch], static_cast<size_t>(nFrames) * sizeof(sample));
    else
      memset(outputs[ch], 0, static_cast<size_t>(nFrames) * sizeof(sample));
  }

  if (!mChain || nCh < 1 || nFrames <= 0)
    return;

  if (mParamsDirty.exchange(false))
    mChain->SetParams(BuildChainParams());

  const size_t needed = static_cast<size_t>(nFrames) * 2;
  if (mScratch.size() < needed)
    mScratch.resize(needed, 0.f);

  // The chain is stereo, as the upstream one is; a mono instance feeds the
  // same signal to both sides.
  for (int i = 0; i < nFrames; i++)
  {
    mScratch[static_cast<size_t>(i) * 2] = static_cast<float>(outputs[0][i]);
    mScratch[static_cast<size_t>(i) * 2 + 1] =
      static_cast<float>(nCh > 1 ? outputs[1][i] : outputs[0][i]);
  }

  mChain->Process(mScratch.data(), 0, nFrames);

  for (int i = 0; i < nFrames; i++)
  {
    outputs[0][i] = mScratch[static_cast<size_t>(i) * 2];

    if (nCh > 1)
      outputs[1][i] = mScratch[static_cast<size_t>(i) * 2 + 1];
  }
}

// ── Rack library ────────────────────────────────────────────────────────────

std::vector<std::string> OpenUtauFX::LibraryNames() const
{
  std::vector<std::string> names;
  names.reserve(static_cast<size_t>(kNumRackPresets) + mUserPresets.size());

  for (int i = 0; i < kNumRackPresets; i++)
    names.emplace_back(kRackPresets[i].label);

  for (const auto& preset : mUserPresets)
    names.push_back(preset.name);

  return names;
}

void OpenUtauFX::RefreshControls()
{
  // Setting parameters programmatically bypasses the controls, so nothing is
  // marked dirty and the rack would keep showing its old pixels.  A standalone
  // host never echoes the change back either, which is what would normally do
  // that marking.
  if (auto* pGraphics = GetUI())
    pGraphics->SetAllControlsDirty();
}

void OpenUtauFX::ApplyLibraryEntry(int index)
{
  const int count = kNumRackPresets + static_cast<int>(mUserPresets.size());

  if (index < 0 || index >= count)
    return;

  if (index < kNumRackPresets)
  {
    const auto& rack = kRackPresets[index];

    SetParamValue(kParamEqOn, 1.0);
    SetParamValue(kParamCompOn, 1.0);
    SetParamValue(kParamReverbOn, 1.0);
    SetParamValue(kParamEnabled, 1.0);

    ApplyEqPreset(oufx::dsp::FindEqPreset(rack.eqKey));
    ApplyCompPreset(oufx::dsp::FindCompPreset(rack.compKey));
    ApplyReverbPreset(oufx::dsp::FindReverbPreset(rack.reverbKey), rack.reverbWet);
  }
  else
  {
    const UserPreset& preset = mUserPresets[static_cast<size_t>(index - kNumRackPresets)];

    for (int i = 0; i < kNumParams && i < static_cast<int>(preset.values.size()); i++)
      SetParamValue(i, preset.values[static_cast<size_t>(i)]);
  }

  mLibraryIndex = index;
  RefreshControls();
}

void OpenUtauFX::BeginSaveUserPreset()
{
  auto* pGraphics = GetUI();
  if (!pGraphics)
    return;

  auto* pCombo = pGraphics->GetControlWithTag(kCtrlTagLibraryCombo);
  if (!pCombo)
    return;

  static_cast<ComboControl*>(pCombo)->SetTextEntryHandler([this](const char* name) { CommitUserPreset(name); });
  pGraphics->CreateTextEntry(*pCombo, IText(12.f, oufx::ui::kButtonText, "Roboto"),
                             pCombo->GetRECT(), "Preset", kNoValIdx);
}

void OpenUtauFX::CommitUserPreset(const char* name)
{
  if (!name || name[0] == '\0')
    return;

  UserPreset preset;
  preset.name = name;
  preset.values.reserve(kNumParams);

  for (int i = 0; i < kNumParams; i++)
    preset.values.push_back(GetParam(i)->Value());

  // Saving over a name replaces that entry, as the dialog's library does.
  for (auto& existing : mUserPresets)
  {
    if (existing.name == preset.name)
    {
      existing = preset;
      mLibraryIndex = kNumRackPresets + static_cast<int>(&existing - mUserPresets.data());
      RefreshControls();
      return;
    }
  }

  mUserPresets.push_back(std::move(preset));
  mLibraryIndex = kNumRackPresets + static_cast<int>(mUserPresets.size()) - 1;
  RefreshControls();
}

void OpenUtauFX::DeleteUserPreset()
{
  const int userIndex = mLibraryIndex - kNumRackPresets;

  if (userIndex < 0 || userIndex >= static_cast<int>(mUserPresets.size()))
    return;

  mUserPresets.erase(mUserPresets.begin() + userIndex);
  mLibraryIndex = 0;
  RefreshControls();
}

// ── State ───────────────────────────────────────────────────────────────────

bool OpenUtauFX::SerializeState(IByteChunk& chunk) const
{
  if (!Plugin::SerializeState(chunk))
    return false;

  chunk.PutStr(mTrackName.c_str());

  const int presetCount = static_cast<int>(mUserPresets.size());
  chunk.Put(&presetCount);

  for (const auto& preset : mUserPresets)
  {
    chunk.PutStr(preset.name.c_str());

    const int valueCount = static_cast<int>(preset.values.size());
    chunk.Put(&valueCount);

    for (double value : preset.values)
      chunk.Put(&value);
  }

  chunk.Put(&mLibraryIndex);
  return true;
}

int OpenUtauFX::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = Plugin::UnserializeState(chunk, startPos);

  WDL_String name;
  pos = chunk.GetStr(name, pos);
  mTrackName = name.Get();

  int count = 0;
  pos = chunk.Get(&count, pos);
  mUserPresets.clear();

  for (int i = 0; i < count; i++)
  {
    UserPreset preset;

    pos = chunk.GetStr(name, pos);
    preset.name = name.Get();

    int valueCount = 0;
    pos = chunk.Get(&valueCount, pos);

    for (int j = 0; j < valueCount; j++)
    {
      double value = 0.0;
      pos = chunk.Get(&value, pos);
      preset.values.push_back(value);
    }

    mUserPresets.push_back(std::move(preset));
  }

  pos = chunk.Get(&mLibraryIndex, pos);

  RefreshControls();
  mParamsDirty.store(true);

  return pos;
}
