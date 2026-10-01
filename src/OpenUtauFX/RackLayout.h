/*
 * OpenUTau FX — rack geometry and background
 *
 * The window is the Mix-FX dialog: two bars (track + power on top, preset
 * library at the bottom) around a rack of three faceplates, 8 px apart.  The
 * numbers below are the dialog's own metrics — 840 px wide, 10 px margins,
 * 40 px bars, faceplate padding 18/12/18/22, a 120 px screen and a 26 px
 * preset strip inside each bezel.
 */

#pragma once

#include "IGraphics.h"
#include "RackControls.h"
#include "RackTheme.h"

namespace oufx::ui {

struct PlateGeometry
{
  IRECT plate;      // the whole faceplate
  IRECT inner;      // content area, after the faceplate padding
  IRECT title;      // title row (the power switch shares it)
  IRECT bezel;      // the recessed screen surround
  IRECT display;    // curve screen inside the bezel
  IRECT combo;      // preset strip inside the bezel
  IRECT knobRow;    // first row of knobs
  IRECT bottomRow;  // second row: fine-tune knobs, or fixed corner labels
  float ruleY;      // y of the hairline between the two rows
};

struct RackGeometry
{
  IRECT topBar;
  IRECT bottomBar;
  IRECT rack;
  PlateGeometry plates[3];

  static constexpr float kMargin = 10.f;
  static constexpr float kBarHeight = 40.f;
  static constexpr float kBarGap = 10.f;
  static constexpr float kPlateGap = 8.f;
  static constexpr float kBezelHeight = 150.f;   // 2 + 120 + 26 + 2
  static constexpr float kDisplayHeight = 120.f;
  static constexpr float kComboHeight = 26.f;
  static constexpr float kBottomRowHeight = 80.f;
  static constexpr float kTitleHeight = 27.f;
  static constexpr float kKnobRowGap = 14.f;
  static constexpr float kRuleGap = 10.f;

  static RackGeometry Compute(const IRECT& bounds)
  {
    RackGeometry geo;
    geo.topBar = IRECT(bounds.L + kMargin, bounds.T + kMargin, bounds.R - kMargin, bounds.T + kMargin + kBarHeight);
    geo.bottomBar = IRECT(bounds.L + kMargin, bounds.B - kMargin - kBarHeight, bounds.R - kMargin, bounds.B - kMargin);
    geo.rack = IRECT(bounds.L + kMargin, geo.topBar.B + kBarGap, bounds.R - kMargin, geo.bottomBar.T - kBarGap);

    const float plateW = (geo.rack.W() - kPlateGap * 2.f) / 3.f;

    for (int i = 0; i < 3; i++)
    {
      PlateGeometry& p = geo.plates[i];
      const float left = geo.rack.L + static_cast<float>(i) * (plateW + kPlateGap);

      p.plate = IRECT(left, geo.rack.T, left + plateW, geo.rack.B);
      p.inner = p.plate.GetPadded(-18.f, -12.f, -18.f, -22.f);
      p.title = p.inner.GetFromTop(kTitleHeight);

      const float bezelTop = p.title.B + kRuleGap;
      p.bezel = IRECT(p.inner.L, bezelTop, p.inner.R, bezelTop + kBezelHeight);

      const IRECT bezelInner = p.bezel.GetPadded(-2.f);
      p.combo = bezelInner.GetFromBottom(kComboHeight);
      p.display = IRECT(bezelInner.L, bezelInner.T, bezelInner.R, p.combo.T);

      p.bottomRow = p.inner.GetFromBottom(kBottomRowHeight);
      p.ruleY = p.bottomRow.T - kRuleGap;
      p.knobRow = IRECT(p.inner.L, p.bezel.B + kKnobRowGap, p.inner.R, p.ruleY - kRuleGap);
    }

    return geo;
  }
};

/** Window, bars, faceplates, screws and rules — everything that never changes. */
class RackBackgroundControl : public IControl
{
public:
  RackBackgroundControl(const IRECT& bounds, const RackGeometry& geometry, const RackTheme& theme)
  : IControl(bounds)
  , mGeometry(geometry)
  , mTheme(theme)
  {
  }

  void Draw(IGraphics& g) override
  {
    FillRect(g, mRECT, mTheme.background);
    FillRoundRect(g, mGeometry.topBar, 6.f, mTheme.bar);
    FillRoundRect(g, mGeometry.bottomBar, 6.f, mTheme.bar);

    static const char* titles[3] = { "EQ", "COMPRESSOR", "REVERB" };
    const PlatePalette* palettes[3] = { &kEqPalette, &kCompPalette, &kReverbPalette };

    for (int i = 0; i < 3; i++)
    {
      const PlateGeometry& p = mGeometry.plates[i];
      const PlatePalette& palette = *palettes[i];

      FillRoundRect(g, p.plate, 6.f, palette.plate);

      // Bevel sheen, clipped to the rounded plate by painting the same shape.
      FillRoundRect(g, p.plate, 6.f, PlateSheen(p.plate));

      g.PathClear();
      g.PathRoundRect(p.plate, 6.f);
      g.PathStroke(Rgba(0x00, 0x00, 0x00, 0x80), 1.f);

      DrawScrew(g, IRECT(p.plate.L + 7.f, p.plate.T + 7.f, p.plate.L + 18.f, p.plate.T + 18.f));
      DrawScrew(g, IRECT(p.plate.R - 18.f, p.plate.T + 7.f, p.plate.R - 7.f, p.plate.T + 18.f));
      DrawScrew(g, IRECT(p.plate.L + 7.f, p.plate.B - 18.f, p.plate.L + 18.f, p.plate.B - 7.f));
      DrawScrew(g, IRECT(p.plate.R - 18.f, p.plate.B - 18.f, p.plate.R - 7.f, p.plate.B - 7.f));

      // Recessed screen surround; the curve display and preset strip sit inside it.
      FillRoundRect(g, p.bezel, 4.f, palette.screenBg);

      g.PathClear();
      g.PathRoundRect(p.bezel, 4.f);
      g.PathStroke(Rgba(0x08, 0x08, 0x08, 0xE0), 2.f);

      const IText titleStyle(17.f, palette.title, "RobotoBold", EAlign::Near, EVAlign::Middle);
      g.DrawText(titleStyle, titles[i], IRECT(p.inner.L + 8.f, p.title.T, p.title.R, p.title.B), nullptr);

      FillRect(g, IRECT(p.inner.L, p.ruleY, p.inner.R, p.ruleY + 1.f), palette.rule);
    }
  }

private:
  void DrawScrew(IGraphics& g, const IRECT& b) const
  {
    FillCircle(g, b.MW(), b.MH(), b.W() / 2.f, ScrewFill(b));
    StrokeCircle(g, b.MW(), b.MH(), b.W() / 2.f, Rgba(0x00, 0x00, 0x00, 0x60), 1.f);
  }

  const RackGeometry& mGeometry;
  const RackTheme& mTheme;
};

}  // namespace oufx::ui
