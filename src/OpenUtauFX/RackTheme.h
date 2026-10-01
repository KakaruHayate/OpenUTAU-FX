/*
 * OpenUTau FX — rack palette
 *
 * The colours of OpenUtau's Mix-FX dialog ("Track Polish"), transcribed from
 * OpenUtau/Views/MixFxDialog.axaml.  Each faceplate carries its own palette:
 * the EQ is a blue plate with a dark screen, the compressor a near-black plate
 * with a cream screen, the reverb a cream plate with a dark screen.
 */

#pragma once

#include "IGraphics.h"

namespace oufx::ui {

using namespace iplug;
using namespace igraphics;

/** IColor takes its arguments as (a, r, g, b); this keeps the call sites readable. */
inline IColor Rgb(int r, int g, int b) { return IColor(255, r, g, b); }
inline IColor Rgba(int r, int g, int b, int a) { return IColor(a, r, g, b); }

// ── Rack chrome (window, bars) ──────────────────────────────────────────────

struct RackTheme
{
  IColor background = Rgb(0x18, 0x18, 0x18);
  IColor bar        = Rgb(0x26, 0x26, 0x26);
  IColor text       = Rgb(0xE0, 0xE0, 0xE0);
  IColor textDim    = Rgb(0xA8, 0xA8, 0xA8);
};

// ── Faceplate ───────────────────────────────────────────────────────────────

struct PlatePalette
{
  IColor plate;
  IColor title;
  IColor text;
  IColor textDim;
  IColor rule;
  IColor tick;
  IColor tickActive;
  IColor knobCap;
  IColor knobPointer;

  IColor screenBg;
  IColor screenCurve;
  IColor screenGrid;
  IColor screenHover;
};

inline const PlatePalette kEqPalette = {
  Rgb(0x1F, 0x36, 0x56),  // plate
  Rgb(0xF2, 0xF6, 0xFA),  // title
  Rgb(0xE6, 0xED, 0xF5),  // text
  Rgb(0xAF, 0xC2, 0xD9),  // textDim
  Rgba(0xFF, 0xFF, 0xFF, 0x33),  // rule
  Rgb(0x4D, 0x6A, 0x8E),  // tick
  Rgb(0x8F, 0xD8, 0xFF),  // tickActive
  Rgb(0xB9, 0xB1, 0xA4),  // knobCap
  Rgb(0x1A, 0x1A, 0x1A),  // knobPointer
  Rgb(0x0B, 0x16, 0x22),  // screenBg
  Rgb(0x6F, 0xC8, 0xFF),  // screenCurve
  Rgb(0x5C, 0x7A, 0x99),  // screenGrid
  Rgb(0x1A, 0x2C, 0x40),  // screenHover
};

inline const PlatePalette kCompPalette = {
  Rgb(0x1C, 0x1C, 0x1C),
  Rgb(0xD8, 0xD8, 0xD8),
  Rgb(0xE2, 0xE2, 0xE2),
  Rgb(0xAB, 0xAB, 0xAB),
  Rgba(0xFF, 0xFF, 0xFF, 0x26),
  Rgb(0x4A, 0x4A, 0x4A),
  Rgb(0xF2, 0xB5, 0x44),
  Rgb(0xCF, 0xCF, 0xCF),
  Rgb(0x1A, 0x1A, 0x1A),
  Rgb(0xEA, 0xDB, 0xAE),
  Rgb(0x8C, 0x24, 0x16),
  Rgb(0x7D, 0x6B, 0x3E),
  Rgb(0xDC, 0xCB, 0x98),
};

inline const PlatePalette kReverbPalette = {
  Rgb(0xE0, 0xD8, 0xC8),
  Rgb(0xA3, 0x28, 0x1E),
  Rgb(0x2B, 0x26, 0x21),
  Rgb(0x5E, 0x56, 0x4C),
  Rgba(0x00, 0x00, 0x00, 0x30),
  Rgb(0xB5, 0xAC, 0x9C),
  Rgb(0xB8, 0x34, 0x2A),
  Rgb(0x2E, 0x2E, 0x2E),
  Rgb(0xF0, 0xF0, 0xF0),
  Rgb(0x23, 0x1F, 0x1A),
  Rgb(0xD9, 0xB8, 0x72),
  Rgb(0x8C, 0x7D, 0x62),
  Rgb(0x33, 0x2D, 0x25),
};

// ── Shared details ──────────────────────────────────────────────────────────

// Faceplate bevel: highlight at the top edge fading out, shade at the bottom.
inline IPattern PlateSheen(const IRECT& b)
{
  return IPattern::CreateLinearGradient(b.L, b.T, b.L, b.B,
  {
    { Rgba(0xFF, 0xFF, 0xFF, 0x1A), 0.0f  },
    { Rgba(0xFF, 0xFF, 0xFF, 0x00), 0.35f },
    { Rgba(0x00, 0x00, 0x00, 0x00), 0.7f  },
    { Rgba(0x00, 0x00, 0x00, 0x22), 1.0f  },
  });
}

inline IPattern ScrewFill(const IRECT& b)
{
  return IPattern::CreateRadialGradient(b.MW(), b.MH(), b.W() * 0.75f,
  {
    { Rgb(0xE0, 0xE0, 0xE0), 0.0f },
    { Rgb(0x8A, 0x8A, 0x8A), 0.6f },
    { Rgb(0x3C, 0x3C, 0x3C), 1.0f },
  });
}

inline IPattern KnobSkirt(const IRECT& b)
{
  return IPattern::CreateLinearGradient(b.L, b.T, b.L, b.B,
  {
    { Rgb(0x4A, 0x4A, 0x4A), 0.0f },
    { Rgb(0x1A, 0x1A, 0x1A), 0.5f },
    { Rgb(0x05, 0x05, 0x05), 1.0f },
  });
}

/** A lit-from-top-left dome in the cap colour, matching the Knob control's CapFill. */
inline IPattern KnobCap(const IRECT& b, const IColor& cap)
{
  auto mix = [](int a, int c, float f) { return static_cast<int>(a + (c - a) * f); };

  const IColor light(mix(cap.R, 255, 0.45f), mix(cap.G, 255, 0.45f), mix(cap.B, 255, 0.45f));
  const IColor dark (mix(cap.R, 0, 0.45f),   mix(cap.G, 0, 0.45f),   mix(cap.B, 0, 0.45f));

  return IPattern::CreateRadialGradient(b.MW(), b.MH(), b.W() * 0.5f,
  {
    { light,    0.0f  },
    { cap,      0.55f },
    { dark,     1.0f  },
  });
}

// App-standard controls (the bars' buttons and combo boxes) keep the host
// theme's look rather than the faceplate palettes.
inline const IColor kButtonBg       = Rgb(0x33, 0x33, 0x33);
inline const IColor kButtonBgHover  = Rgb(0x3D, 0x3D, 0x3D);
inline const IColor kButtonBgDown   = Rgb(0x2A, 0x2A, 0x2A);
inline const IColor kButtonBorder   = Rgb(0x4D, 0x4D, 0x4D);
inline const IColor kButtonText     = Rgb(0xE0, 0xE0, 0xE0);
inline const IColor kButtonTextDim  = Rgb(0x80, 0x80, 0x80);

inline const IColor kPowerLedOff  = Rgb(0x1C, 0x2A, 0x1F);
inline const IColor kPowerLedOn   = Rgb(0x3F, 0xE0, 0x63);
inline const IColor kPowerLedIdle = Rgb(0x2F, 0x6B, 0x3C);
inline const IColor kPowerTrack   = Rgb(0x0A, 0x0A, 0x0A);
inline const IColor kPowerThumbOff = Rgb(0x7A, 0x7A, 0x7A);
inline const IColor kPowerThumbOn  = Rgb(0xED, 0xED, 0xED);

/** Palette for the combo boxes and drop-downs that use the app's standard style
 *  rather than a faceplate's screen colours (the preset library in the bottom bar). */
inline const PlatePalette kControlPalette = {
  kButtonBg,        // plate
  kButtonText,      // title
  kButtonText,      // text
  kButtonTextDim,   // textDim
  kButtonBorder,    // rule
  kButtonBorder,    // tick
  kButtonText,      // tickActive
  kButtonText,      // knobCap
  kButtonText,      // knobPointer
  Rgb(0x2B, 0x2B, 0x2B),  // screenBg
  kButtonText,            // screenCurve
  kButtonBorder,          // screenGrid
  Rgb(0x3D, 0x3D, 0x3D),  // screenHover
};

}  // namespace oufx::ui
