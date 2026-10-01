/*
 * OpenUTau FX — faceplate screens
 *
 * C++ port of OpenUtau/Controls/MixFxDisplays.cs.  Each display is a recessed
 * bezel: a background, a faint grid with scale labels, the curve itself with a
 * translucent fill beneath it, and a glass highlight on top.
 *
 *   EqCurve      magnitude response, 20 Hz - 20 kHz log, +/-15 dB
 *   CompCurve    transfer curve, -60..0 dB in, -60..+6 dB out
 *   ReverbCurve  decay over 0-4 s on a dB scale, with the shorter
 *                high-frequency tail damping leaves
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>

#include "IGraphics.h"
#include "MixFxDSP.h"
#include "MixFxPresets.h"
#include "RackControls.h"
#include "RackTheme.h"

namespace oufx::ui {

using oufx::dsp::BiquadEQ;
using oufx::dsp::Freeverb;
using oufx::dsp::SimpleCompressor;

// ── Base ────────────────────────────────────────────────────────────────────

class FxDisplay : public IControl
{
public:
  static constexpr float kLabelSize = 9.f;
  // The displays design their filters at 44.1 kHz, as the upstream UI does.
  static constexpr double kDisplaySampleRate = 44100.0;

  FxDisplay(const IRECT& bounds, const std::initializer_list<int>& params, const PlatePalette& palette)
  : IControl(bounds, params)
  , mPalette(palette)
  {
  }

  void Draw(IGraphics& g) override
  {
    FillRect(g, mRECT, mPalette.screenBg);

    if (mRECT.W() < 16.f || mRECT.H() < 16.f)
      return;

    RenderPlot(g, mRECT.GetPadded(-6.f, -6.f, -6.f, -4.f));

    // Glass highlight across the top half of the screen.
    FillRect(g, mRECT, IPattern::CreateLinearGradient(mRECT.L, mRECT.T, mRECT.L, mRECT.T + mRECT.H() * 0.5f,
    {
      { Rgba(0xFF, 0xFF, 0xFF, 0x1C), 0.f },
      { Rgba(0xFF, 0xFF, 0xFF, 0x00), 1.f },
    }));
  }

protected:
  virtual void RenderPlot(IGraphics& g, const IRECT& r) = 0;

  IColor GridColor(float opacity) const
  {
    IColor c = mPalette.screenGrid;
    c.A = static_cast<int>(c.A * opacity);
    return c;
  }

  IColor CurveFillColor(float opacity) const
  {
    IColor c = mPalette.screenCurve;
    c.A = static_cast<int>(c.A * opacity);
    return c;
  }

  void DrawMarker(IGraphics& g, float x, float y) const
  {
    FillCircle(g, x, y, 3.5f, mPalette.screenBg);
    StrokeCircle(g, x, y, 3.5f, mPalette.screenCurve, 1.5f);
  }

  /** Draws a small scale label whose top-left (or top-centre / top-right) is the anchor. */
  void DrawLabel(IGraphics& g, const char* text, float anchorX, float anchorY,
                 EAlign align = EAlign::Near, const IColor* brush = nullptr) const
  {
    const IColor color = brush ? *brush : mPalette.screenGrid;
    const IText style(kLabelSize, color, "Roboto", align, EVAlign::Top);

    float l = anchorX, r = anchorX + 160.f;
    if (align == EAlign::Center) { l = anchorX - 80.f; r = anchorX + 80.f; }
    else if (align == EAlign::Far) { l = anchorX - 160.f; r = anchorX; }

    g.DrawText(style, text, IRECT(l, anchorY, r, anchorY + 12.f), nullptr);
  }

  /** Builds a polyline through \p n points; optionally closes it down to \p closeY. */
  template <typename PointFn>
  static void BuildPolyline(IGraphics& g, int n, PointFn pointAt, bool close, float closeY)
  {
    g.PathClear();

    float x = 0.f, y = 0.f;
    pointAt(0, x, y);

    if (close)
    {
      g.PathMoveTo(x, closeY);
      g.PathLineTo(x, y);
    }
    else
    {
      g.PathMoveTo(x, y);
    }

    for (int i = 1; i < n; i++)
    {
      pointAt(i, x, y);
      g.PathLineTo(x, y);
    }

    if (close)
    {
      g.PathLineTo(x, closeY);
      g.PathClose();
    }
  }

  const PlatePalette& mPalette;
};

// ── EQ magnitude response ───────────────────────────────────────────────────
// 20 Hz - 20 kHz log, +/-15 dB, with a marker per band.

class EqCurve : public FxDisplay
{
public:
  EqCurve(const IRECT& bounds, const std::initializer_list<int>& params, const PlatePalette& palette,
          std::function<double()> lowDb, std::function<double()> midFreq,
          std::function<double()> midDb, std::function<double()> highDb)
  : FxDisplay(bounds, params, palette)
  , mLowDb(std::move(lowDb))
  , mMidFreq(std::move(midFreq))
  , mMidDb(std::move(midDb))
  , mHighDb(std::move(highDb))
  {
  }

protected:
  void RenderPlot(IGraphics& g, const IRECT& r) override
  {
    constexpr double minFreq = 20.0, maxFreq = 20000.0, rangeDb = 15.0;

    auto xAt = [&](double f)
    {
      return static_cast<float>(r.L + std::log(f / minFreq) / std::log(maxFreq / minFreq) * r.W());
    };
    auto yAt = [&](double db)
    {
      const double clamped = std::clamp(db, -rangeDb, rangeDb);
      return static_cast<float>(r.MH() - clamped / rangeDb * r.H() / 2.0);
    };

    for (double f : { 50.0, 200.0, 500.0, 2000.0, 5000.0 })
      StrokeLine(g, GridColor(0.25f), xAt(f), r.T, xAt(f), r.B, 1.f);

    for (double db : { -12.0, -6.0, 6.0, 12.0 })
      StrokeLine(g, GridColor(0.25f), r.L, yAt(db), r.R, yAt(db), 1.f);

    struct MajorTick { double freq; const char* label; };
    for (const auto& tick : { MajorTick{ 100.0, "100" }, MajorTick{ 1000.0, "1k" }, MajorTick{ 10000.0, "10k" } })
    {
      StrokeLine(g, GridColor(0.6f), xAt(tick.freq), r.T, xAt(tick.freq), r.B, 1.f);
      DrawLabel(g, tick.label, xAt(tick.freq) + 2.f, r.B - 11.f);
    }

    StrokeLine(g, GridColor(0.6f), r.L, yAt(0.0), r.R, yAt(0.0), 1.f);
    DrawLabel(g, "+12", r.L, yAt(12.0) - 5.f);
    DrawLabel(g, "-12", r.L, yAt(-12.0) - 5.f);

    const double lowDb = mLowDb ? mLowDb() : 0.0;
    const double midFreq = mMidFreq ? mMidFreq() : 1000.0;
    const double midDb = mMidDb ? mMidDb() : 0.0;
    const double highDb = mHighDb ? mHighDb() : 0.0;

    mEq.Configure(lowDb, midFreq, dsp::kEqMidQ, midDb, highDb);

    const int n = std::max(2, static_cast<int>(r.W() / 2.f));
    auto pointAt = [&](int i, float& px, float& py)
    {
      const double f = minFreq * std::pow(maxFreq / minFreq, static_cast<double>(i) / (n - 1));
      px = xAt(f);
      py = yAt(mEq.ResponseDb(f));
    };

    BuildPolyline(g, n, pointAt, true, yAt(0.0));
    g.PathFill(CurveFillColor(0.22f));

    BuildPolyline(g, n, pointAt, false, 0.f);
    g.PathStroke(mPalette.screenCurve, 2.f);

    for (double f : { dsp::kEqLowShelf, midFreq, dsp::kEqHighShelf })
      DrawMarker(g, xAt(f), yAt(mEq.ResponseDb(f)));
  }

private:
  std::function<double()> mLowDb, mMidFreq, mMidDb, mHighDb;
  BiquadEQ mEq { kDisplaySampleRate, 2 };
};

// ── Compressor transfer curve ───────────────────────────────────────────────
// Input -60..0 dB -> output -60..+6 dB, including makeup, threshold marked.

class CompCurve : public FxDisplay
{
public:
  CompCurve(const IRECT& bounds, const std::initializer_list<int>& params, const PlatePalette& palette,
            std::function<double()> thresholdDb, std::function<double()> ratio,
            std::function<double()> makeupDb)
  : FxDisplay(bounds, params, palette)
  , mThresholdDb(std::move(thresholdDb))
  , mRatio(std::move(ratio))
  , mMakeupDb(std::move(makeupDb))
  {
  }

protected:
  void RenderPlot(IGraphics& g, const IRECT& r) override
  {
    constexpr double inMin = -60.0, inMax = 0.0, outMin = -60.0, outMax = 6.0;

    auto xAt = [&](double db) { return static_cast<float>(r.L + (db - inMin) / (inMax - inMin) * r.W()); };
    auto yAt = [&](double db)
    {
      const double clamped = std::clamp(db, outMin, outMax);
      return static_cast<float>(r.B - (clamped - outMin) / (outMax - outMin) * r.H());
    };

    const double thresholdDb = mThresholdDb ? mThresholdDb() : 0.0;
    const double ratio = mRatio ? mRatio() : 1.0;
    const double makeupDb = mMakeupDb ? mMakeupDb() : 0.0;

    auto outDb = [&](double in)
    {
      return in + SimpleCompressor::CurveGainDb(in, thresholdDb, ratio) + makeupDb;
    };

    for (double db : { -48.0, -36.0, -24.0, -12.0 })
    {
      StrokeLine(g, GridColor(0.3f), xAt(db), r.T, xAt(db), r.B, 1.f);
      StrokeLine(g, GridColor(0.3f), r.L, yAt(db), r.R, yAt(db), 1.f);

      char label[8] = {};
      snprintf(label, sizeof(label), "%.0f", db);
      DrawLabel(g, label, xAt(db), r.B - 11.f, EAlign::Center);
    }

    StrokeDashedLine(g, GridColor(0.6f), xAt(inMin), yAt(inMin), xAt(inMax), yAt(inMax), 1.f, 3.f);
    StrokeDashedLine(g, GridColor(0.9f), xAt(thresholdDb), r.T, xAt(thresholdDb), r.B, 1.f, 2.f);

    const int n = std::max(2, static_cast<int>(r.W() / 2.f));
    auto pointAt = [&](int i, float& px, float& py)
    {
      const double input = inMin + (inMax - inMin) * i / (n - 1);
      px = xAt(input);
      py = yAt(outDb(input));
    };

    BuildPolyline(g, n, pointAt, true, r.B);
    g.PathFill(CurveFillColor(0.12f));

    BuildPolyline(g, n, pointAt, false, 0.f);
    g.PathStroke(mPalette.screenCurve, 2.f);

    DrawMarker(g, xAt(thresholdDb), yAt(outDb(thresholdDb)));

    char ratioLabel[16] = {};
    snprintf(ratioLabel, sizeof(ratioLabel), "%.1f:1", ratio);
    DrawLabel(g, ratioLabel, r.R, r.T, EAlign::Far, &mPalette.screenCurve);
  }

private:
  std::function<double()> mThresholdDb, mRatio, mMakeupDb;
};

// ── Reverb decay ────────────────────────────────────────────────────────────
// 0-4 s on a dB scale so the decay reads as a slope: the full-band tail, and
// the shorter high-frequency tail damping leaves, starting after the pre-delay
// gap at the wet level.

class ReverbCurve : public FxDisplay
{
public:
  ReverbCurve(const IRECT& bounds, const std::initializer_list<int>& params, const PlatePalette& palette,
              const IColor& highBrush, std::function<double()> roomSize, std::function<double()> damp,
              std::function<double()> wet, std::function<double()> preDelayMs,
              std::function<int()> presetIdx)
  : FxDisplay(bounds, params, palette)
  , mHighBrush(highBrush)
  , mRoomSize(std::move(roomSize))
  , mDamp(std::move(damp))
  , mWet(std::move(wet))
  , mPreDelayMs(std::move(preDelayMs))
  , mPresetIdx(std::move(presetIdx))
  {
  }

protected:
  void RenderPlot(IGraphics& g, const IRECT& r) override
  {
    constexpr double seconds = 4.0, floorDb = -48.0, fullScaleWet = 0.5;

    const int presetIndex = std::clamp(mPresetIdx ? mPresetIdx() : 0, 0, dsp::kNumReverbPresets - 1);
    const double presetWet = dsp::kReverbPresets[presetIndex].wet;
    const double wet = presetWet * std::clamp(mWet ? mWet() : 1.0, 0.0, 2.0) / fullScaleWet;

    double decayLow = 0.0, decayHigh = 0.0;
    Freeverb::DecaySeconds(mRoomSize ? mRoomSize() : 0.0, mDamp ? mDamp() : 0.0, decayLow, decayHigh);

    const double preDelay = (mPreDelayMs ? mPreDelayMs() : 0.0) / 1000.0;
    const float top = r.T + 12.f;

    auto xAt = [&](double t) { return static_cast<float>(r.L + t / seconds * r.W()); };
    auto yAt = [&](double db)
    {
      return static_cast<float>(top + std::clamp(db / floorDb, 0.0, 1.0) * (r.B - top));
    };

    for (double t = 0.5; t < seconds; t += 0.5)
    {
      StrokeLine(g, GridColor(0.3f), xAt(t), r.T, xAt(t), r.B, 1.f);

      if (std::fmod(t, 1.0) == 0.0)
      {
        char label[8] = {};
        snprintf(label, sizeof(label), "%.0fs", t);
        DrawLabel(g, label, xAt(t) + 2.f, r.B - 11.f);
      }
    }

    for (double db : { -12.0, -24.0, -36.0 })
      StrokeLine(g, GridColor(0.3f), r.L, yAt(db), r.R, yAt(db), 1.f);

    if (wet > 1e-4)
    {
      const double levelDb = 20.0 * std::log10(wet);
      const int n = std::max(2, static_cast<int>(r.W() / 2.f));

      auto envelope = [&](double rt60)
      {
        return [&, rt60](int i, float& px, float& py)
        {
          const double t = seconds * i / (n - 1);
          const double db = t < preDelay ? floorDb : levelDb - 60.0 * (t - preDelay) / rt60;
          px = xAt(t);
          py = yAt(db);
        };
      };

      BuildPolyline(g, n, envelope(decayLow), true, r.B);
      g.PathFill(CurveFillColor(0.35f));
      BuildPolyline(g, n, envelope(decayLow), false, 0.f);
      g.PathStroke(mPalette.screenCurve, 1.5f);

      IColor high = mHighBrush;
      high.A = static_cast<int>(high.A * 0.55f);
      BuildPolyline(g, n, envelope(decayHigh), true, r.B);
      g.PathFill(high);

      DrawMarker(g, xAt(preDelay), yAt(levelDb));

      char label[32] = {};
      snprintf(label, sizeof(label), "RT60 %.1f s", decayLow);
      DrawLabel(g, label, r.R, r.T, EAlign::Far, &mPalette.screenCurve);
    }
    else
    {
      DrawLabel(g, "DRY", r.R, r.T, EAlign::Far, &mPalette.screenCurve);
    }
  }

private:
  IColor mHighBrush;
  std::function<double()> mRoomSize, mDamp, mWet, mPreDelayMs;
  std::function<int()> mPresetIdx;
};

}  // namespace oufx::ui
