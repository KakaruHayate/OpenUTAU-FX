/*
 * OpenUTau FX — rack controls
 *
 * Hand-drawn replacements for the Avalonia controls the Mix-FX dialog uses
 * (Knob, the .power ToggleButton, the .preset ComboBox and the bar Buttons),
 * so the plugin can match the dialog's appearance on every platform.
 *
 * Everything paints through IGraphics paths rather than iPlug2's vector style
 * system, because the dialog's look depends on gradients and bevels the style
 * system does not express.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "IControl.h"
#include "IGraphics.h"
#include "RackTheme.h"

namespace oufx::ui {

constexpr double kPi = 3.14159265358979323846;

// ── Small drawing helpers ───────────────────────────────────────────────────
// IGraphics' Draw* methods all stroke; these fill.

inline void FillRect(IGraphics& g, const IRECT& b, const IPattern& p)
{
  g.PathClear();
  g.PathRect(b);
  g.PathFill(p);
}

inline void FillRoundRect(IGraphics& g, const IRECT& b, float cornerRadius, const IPattern& p)
{
  g.PathClear();
  g.PathRoundRect(b, cornerRadius);
  g.PathFill(p);
}

inline void FillCircle(IGraphics& g, float cx, float cy, float r, const IPattern& p)
{
  g.PathClear();
  g.PathCircle(cx, cy, r);
  g.PathFill(p);
}

inline void StrokeCircle(IGraphics& g, float cx, float cy, float r, const IColor& c, float thickness)
{
  g.PathClear();
  g.PathCircle(cx, cy, r);
  g.PathStroke(c, thickness);
}

inline void StrokeLine(IGraphics& g, const IColor& c, float x1, float y1, float x2, float y2,
                       float thickness, ELineCap cap = ELineCap::Butt)
{
  IStrokeOptions opts;
  opts.mCapOption = cap;
  g.PathClear();
  g.PathMoveTo(x1, y1);
  g.PathLineTo(x2, y2);
  g.PathStroke(c, thickness, opts);
}

inline void StrokeDashedLine(IGraphics& g, const IColor& c, float x1, float y1, float x2, float y2,
                             float thickness, float dash)
{
  IStrokeOptions opts;
  float dashes[2] = { dash, dash };
  opts.mDash.SetDash(dashes, 0.f, 2);
  g.PathClear();
  g.PathMoveTo(x1, y1);
  g.PathLineTo(x2, y2);
  g.PathStroke(c, thickness, opts);
}

/** A small downward triangle, as used for a combo box's drop-down glyph. */
inline void FillDownTriangle(IGraphics& g, float cx, float cy, float w, float h, const IColor& c)
{
  float xs[3] = { cx - w / 2.f, cx + w / 2.f, cx };
  float ys[3] = { cy - h / 2.f, cy - h / 2.f, cy + h / 2.f };
  g.PathClear();
  g.PathConvexPolygon(xs, ys, 3);
  g.PathFill(c);
}

/** A point at fraction \p t of the knob sweep, 0 = 7:30, 1 = 4:30. */
inline void KnobPointAt(float cx, float cy, float radius, double t, float& outX, float& outY)
{
  const double sweep = 270.0;
  const double rad = (-sweep / 2.0 + sweep * t) * kPi / 180.0;
  outX = static_cast<float>(cx + std::sin(rad) * radius);
  outY = static_cast<float>(cy - std::cos(rad) * radius);
}

// ── Knob ────────────────────────────────────────────────────────────────────
// Drag vertically to turn (Shift for fine), wheel to step, double-click to
// reset.  Sweeps 270 deg from 7:30 to 4:30; the tick dots light up from the
// origin, which is 0 when the range spans zero, else the minimum.

class RackKnobControl : public IControl
{
public:
  static constexpr int kTickCount = 21;
  static constexpr float kSweepDegrees = 270.f;
  static constexpr float kDragPixels = 200.f;
  static constexpr float kFineFactor = 0.1f;

  RackKnobControl(const IRECT& bounds, int paramIdx, const PlatePalette& palette,
                  const IColor& cap, const IColor& pointer)
  : IControl(bounds, paramIdx)
  , mPalette(palette)
  , mCap(cap)
  , mPointer(pointer)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IParam* pParam = GetParam();
    if (!pParam)
      return;

    const float size = std::min(mRECT.W(), mRECT.H());
    if (size <= 16.f)
      return;

    const float cx = mRECT.MW();
    const float cy = mRECT.MH();
    const float tickRadius = size / 2.f - 2.f;
    const float skirtRadius = size / 2.f - 8.f;
    const float capRadius = skirtRadius * 0.78f;

    const double min = pParam->GetMin();
    const double max = pParam->GetMax();
    const double range = std::max(1e-9, max - min);
    const double t = std::clamp((pParam->Value() - min) / range, 0.0, 1.0);
    const double origin = (min < 0.0 && max > 0.0) ? -min / range : 0.0;
    const double litFrom = std::min(origin, t) - 1e-6;
    const double litTo = std::max(origin, t) + 1e-6;

    const float dot = std::max(1.2f, size / 44.f);

    for (int i = 0; i < kTickCount; i++)
    {
      const double ti = static_cast<double>(i) / (kTickCount - 1);
      const IColor& brush = (ti >= litFrom && ti <= litTo) ? mPalette.tickActive : mPalette.tick;
      float px = 0.f, py = 0.f;
      KnobPointAt(cx, cy, tickRadius, ti, px, py);
      FillCircle(g, px, py, dot, brush);
    }

    // Shadow, then the skirt and the domed cap.
    FillCircle(g, cx, cy + 2.f, skirtRadius + 1.f, Rgba(0x00, 0x00, 0x00, 0x60));
    FillCircle(g, cx, cy, skirtRadius, KnobSkirt(IRECT(cx - skirtRadius, cy - skirtRadius, cx + skirtRadius, cy + skirtRadius)));
    StrokeCircle(g, cx, cy, skirtRadius, Rgba(0x00, 0x00, 0x00, 0x50), 1.f);
    FillCircle(g, cx, cy, capRadius, KnobCap(IRECT(cx - capRadius, cy - capRadius, cx + capRadius, cy + capRadius), mCap));
    StrokeCircle(g, cx, cy, capRadius, Rgba(0x00, 0x00, 0x00, 0x50), 1.f);

    float fromX = 0.f, fromY = 0.f, toX = 0.f, toY = 0.f;
    KnobPointAt(cx, cy, capRadius * 0.25f, t, fromX, fromY);
    KnobPointAt(cx, cy, skirtRadius - 2.f, t, toX, toY);
    StrokeLine(g, mPointer, fromX, fromY, toX, toY, std::max(2.f, size / 22.f), ELineCap::Round);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    mDragging = true;
    mLastY = y;
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    const IParam* pParam = GetParam();
    if (!pParam)
      return;

    mDragging = false;
    SetValueFromUserInput(pParam->ToNormalized(pParam->GetDefault()), 0);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (!mDragging)
      return;

    Nudge((mLastY - y) * static_cast<double>(Step(mod.S)));
    mLastY = y;
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override { mDragging = false; }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    Nudge(d * static_cast<double>(Step(mod.S)));
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    // One press is one step, the same unit the wheel uses.
    switch (key.VK)
    {
      case kVK_LEFT:
      case kVK_DOWN:  Nudge(-static_cast<double>(Step(key.S))); return true;
      case kVK_RIGHT:
      case kVK_UP:    Nudge(static_cast<double>(Step(key.S))); return true;
      default:        return false;
    }
  }

private:
  /** Value delta for one pixel of drag; the wheel and the arrow keys use the
   *  same unit, and Shift makes it fine. */
  float Step(bool fine) const
  {
    const IParam* pParam = GetParam();
    if (!pParam)
      return 0.f;

    float step = static_cast<float>((pParam->GetMax() - pParam->GetMin()) / kDragPixels);
    if (fine)
      step *= kFineFactor;

    return step;
  }

  void Nudge(double delta)
  {
    const IParam* pParam = GetParam();
    if (!pParam)
      return;

    const double v = std::clamp(pParam->Value() + delta, pParam->GetMin(), pParam->GetMax());
    SetValueFromUserInput(pParam->ToNormalized(v), 0);
  }

  const PlatePalette& mPalette;
  IColor mCap, mPointer;
  bool mDragging = false;
  float mLastY = 0.f;
};

// ── Power switch ────────────────────────────────────────────────────────────
// Green LED plus a slide toggle, with an optional caption.  The idle state
// dims the LED when a module is on but the rack's master power is off.

class PowerSwitchControl : public IControl
{
public:
  static constexpr float kLedSize = 9.f;
  static constexpr float kGap = 7.f;
  static constexpr float kTrackW = 32.f;
  static constexpr float kTrackH = 17.f;
  static constexpr float kThumbSize = 13.f;

  PowerSwitchControl(const IRECT& bounds, int paramIdx, const char* caption,
                     const RackTheme& theme, std::function<bool()> idle = nullptr)
  : IControl(bounds, paramIdx)
  , mCaption(caption ? caption : "")
  , mIdle(std::move(idle))
  , mText(11.f, theme.text, "RobotoBold", EAlign::Near, EVAlign::Middle)
  {
  }

  /** Width the control needs for its LED, track and caption. */
  float RequiredWidth() const
  {
    float w = kLedSize + kGap + kTrackW;
    if (!mCaption.empty())
      w += kGap + static_cast<float>(mCaption.size()) * 8.f;
    return w;
  }

  void Draw(IGraphics& g) override
  {
    const IParam* pParam = GetParam();
    if (!pParam)
      return;

    const bool on = pParam->Bool();
    const bool idle = on && mIdle && mIdle();

    const float cy = mRECT.MH();
    float x = mRECT.L;

    FillCircle(g, x + kLedSize / 2.f, cy, kLedSize / 2.f, on ? (idle ? kPowerLedIdle : kPowerLedOn) : kPowerLedOff);
    x += kLedSize + kGap;

    const IRECT track(x, cy - kTrackH / 2.f, x + kTrackW, cy + kTrackH / 2.f);
    FillRoundRect(g, track, kTrackH / 2.f, kPowerTrack);

    const float thumbR = kThumbSize / 2.f;
    const float thumbX = on ? track.R - 2.f - thumbR : track.L + 2.f + thumbR;
    FillCircle(g, thumbX, cy, thumbR, on ? kPowerThumbOn : kPowerThumbOff);

    x += kTrackW + kGap;

    if (!mCaption.empty())
      g.DrawText(mText, mCaption.c_str(), IRECT(x, mRECT.T, mRECT.R, mRECT.B), nullptr);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const IParam* pParam = GetParam();
    if (!pParam)
      return;

    SetValueFromUserInput(pParam->Bool() ? 0.0 : 1.0, 0);
  }

private:
  std::string mCaption;
  std::function<bool()> mIdle;
  IText mText;
};

// ── Button ──────────────────────────────────────────────────────────────────

class RackButtonControl : public IControl
{
public:
  RackButtonControl(const IRECT& bounds, const char* caption, std::function<void()> action)
  : IControl(bounds)
  , mCaption(caption ? caption : "")
  , mAction(std::move(action))
  , mText(12.f, kButtonText, "Roboto", EAlign::Center, EVAlign::Middle)
  {
  }

  void Draw(IGraphics& g) override
  {
    FillRoundRect(g, mRECT, 3.f, mPressed ? kButtonBgDown : (mHover ? kButtonBgHover : kButtonBg));

    g.PathClear();
    g.PathRoundRect(mRECT, 3.f);
    g.PathStroke(kButtonBorder, 1.f);

    mText.mFGColor = mEnabled ? kButtonText : kButtonTextDim;
    g.DrawText(mText, mCaption.c_str(), mRECT, nullptr);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (!mEnabled)
      return;

    mPressed = true;
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (!mEnabled)
      return;

    mPressed = false;
    SetDirty(false);

    if (mRECT.Contains(x, y) && mAction)
      mAction();
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    if (!mHover) { mHover = true; SetDirty(false); }
  }

  void OnMouseOut() override
  {
    if (mHover || mPressed) { mHover = false; mPressed = false; SetDirty(false); }
  }

  void SetEnabled(bool enabled)
  {
    mEnabled = enabled;
    SetIgnoreMouse(!enabled);
    SetDirty(false);
  }

private:
  std::string mCaption;
  std::function<void()> mAction;
  IText mText;
  bool mHover = false, mPressed = false, mEnabled = true;
};

// ── Value readout ───────────────────────────────────────────────────────────
// The monospaced number under a knob.  Formatted by the caller so each module
// can print dB / Hz / ratio / seconds its own way.

using ValueFormatter = std::function<void(char* buf, size_t len, double value)>;

class ValueReadoutControl : public IControl
{
public:
  ValueReadoutControl(const IRECT& bounds, int paramIdx, ValueFormatter formatter, const IColor& color)
  : IControl(bounds, paramIdx)
  , mFormatter(std::move(formatter))
  , mText(11.f, color, "RobotoMono", EAlign::Center, EVAlign::Middle)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IParam* pParam = GetParam();
    if (!pParam || !mFormatter)
      return;

    char buf[64] = {};
    mFormatter(buf, sizeof(buf), pParam->Value());
    g.DrawText(mText, buf, mRECT, nullptr);
  }

private:
  ValueFormatter mFormatter;
  IText mText;
};

// ── Static label ────────────────────────────────────────────────────────────

class RackLabelControl : public IControl
{
public:
  RackLabelControl(const IRECT& bounds, const char* text, const IText& style)
  : IControl(bounds)
  , mStr(text ? text : "")
  , mStyle(style)
  {
  }

  void Draw(IGraphics& g) override { g.DrawText(mStyle, mStr.c_str(), mRECT, nullptr); }

private:
  std::string mStr;
  IText mStyle;
};

// ── Editable name ───────────────────────────────────────────────────────────

class NameControl : public IControl
{
public:
  NameControl(const IRECT& bounds, std::string name, const IText& style,
              std::function<void(const std::string&)> onChange)
  : IControl(bounds)
  , mName(std::move(name))
  , mStyle(style)
  , mOnChange(std::move(onChange))
  {
  }

  void Draw(IGraphics& g) override { g.DrawText(mStyle, mName.c_str(), mRECT, nullptr); }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    GetUI()->CreateTextEntry(*this, mStyle, mRECT, mName.c_str(), kNoValIdx);
  }

  void OnTextEntryCompletion(const char* str, int valIdx) override
  {
    if (!str)
      return;

    mName = str;
    SetDirty(false);

    if (mOnChange)
      mOnChange(mName);
  }

  const std::string& GetName() const { return mName; }

private:
  std::string mName;
  IText mStyle;
  std::function<void(const std::string&)> mOnChange;
};

// ── Drop-down layer ─────────────────────────────────────────────────────────
// One full-window overlay, attached last so it draws above the rack.  It stays
// hidden and mouse-transparent until a preset selector opens it; the panel is
// then painted in that faceplate's screen colours, matching the dialog's
// themed ComboBox popup.

class DropdownLayer : public IControl
{
public:
  static constexpr float kItemHeight = 24.f;
  static constexpr float kPanelPad = 4.f;

  explicit DropdownLayer(const IRECT& bounds)
  : IControl(bounds)
  {
    Close();
  }

  void Open(const IRECT& anchor, const std::vector<std::string>& items, int selected,
            const PlatePalette& palette, std::function<void(int)> onSelect)
  {
    mItems = items;
    mSelected = selected;
    mPalette = palette;
    mOnSelect = std::move(onSelect);

    const float w = std::max(anchor.W(), 140.f);
    const float h = kPanelPad * 2.f + kItemHeight * static_cast<float>(mItems.size());

    float top = anchor.B + 2.f;
    if (top + h > mRECT.B - 4.f)
      top = anchor.T - 2.f - h;  // flip above when there is no room below

    mPanel = IRECT(anchor.L, top, anchor.L + w, top + h);
    mHover = -1;
    mOpen = true;

    Hide(false);
    SetIgnoreMouse(false);
    SetDirty(false);
  }

  void Close()
  {
    mOpen = false;
    mItems.clear();
    mOnSelect = nullptr;
    mHover = -1;

    Hide(true);
    SetIgnoreMouse(true);
    SetDirty(false);
  }

  bool IsOpen() const { return mOpen; }

  void Draw(IGraphics& g) override
  {
    if (!mOpen)
      return;

    FillRoundRect(g, mPanel, 3.f, mPalette.screenBg);

    g.PathClear();
    g.PathRoundRect(mPanel, 3.f);
    g.PathStroke(mPalette.screenGrid, 1.f);

    const IText item(11.5f, mPalette.screenCurve, "Roboto", EAlign::Near, EVAlign::Middle);

    for (size_t i = 0; i < mItems.size(); i++)
    {
      const float top = mPanel.T + kPanelPad + kItemHeight * static_cast<float>(i);
      const IRECT row(mPanel.L + kPanelPad, top, mPanel.R - kPanelPad, top + kItemHeight);

      if (static_cast<int>(i) == mHover || static_cast<int>(i) == mSelected)
        FillRect(g, row, mPalette.screenHover);

      g.DrawText(item, mItems[i].c_str(), row.GetPadded(-8.f, 0.f, -8.f, 0.f), nullptr);
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const int hover = IndexAt(y);
    if (hover != mHover)
    {
      mHover = hover;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    if (mHover != -1)
    {
      mHover = -1;
      SetDirty(false);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int index = IndexAt(y);

    if (index >= 0 && index < static_cast<int>(mItems.size()))
    {
      const auto onSelect = mOnSelect;
      Close();
      if (onSelect)
        onSelect(index);
      return;
    }

    Close();  // a click anywhere else dismisses the list
  }

private:
  int IndexAt(float y) const
  {
    if (!mOpen || y < mPanel.T + kPanelPad || y >= mPanel.B - kPanelPad)
      return -1;

    const int index = static_cast<int>((y - mPanel.T - kPanelPad) / kItemHeight);
    return (index >= 0 && index < static_cast<int>(mItems.size())) ? index : -1;
  }

  bool mOpen = false;
  int mSelected = -1;
  int mHover = -1;
  IRECT mPanel;
  std::vector<std::string> mItems;
  PlatePalette mPalette;
  std::function<void(int)> mOnSelect;
};

// ── Combo box ───────────────────────────────────────────────────────────────
// Two looks, one control.  In screen style it is the closed half of a module's
// preset ComboBox: a transparent strip under the curve screen with a hairline
// rule on top, the preset name in the screen colour and a drop-down glyph on
// the right.  Otherwise it is the app-standard combo the preset library uses.
// Either way the list itself is drawn by the shared DropdownLayer.

class ComboControl : public IControl
{
public:
  ComboControl(const IRECT& bounds, const PlatePalette& palette, DropdownLayer& dropdown,
               std::function<std::vector<std::string>()> items,
               std::function<int()> selected,
               std::function<void(int)> onSelect,
               bool screenStyle)
  : IControl(bounds)
  , mPalette(palette)
  , mDropdown(dropdown)
  , mItems(std::move(items))
  , mSelected(std::move(selected))
  , mOnSelect(std::move(onSelect))
  , mScreenStyle(screenStyle)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mScreenStyle)
    {
      if (mHover)
        FillRect(g, mRECT, mPalette.screenHover);

      StrokeLine(g, mPalette.screenGrid, mRECT.L, mRECT.T, mRECT.R, mRECT.T, 1.f);
    }
    else
    {
      FillRoundRect(g, mRECT, 3.f, mHover ? kButtonBgHover : kButtonBg);

      g.PathClear();
      g.PathRoundRect(mRECT, 3.f);
      g.PathStroke(kButtonBorder, 1.f);
    }

    const auto list = mItems ? mItems() : std::vector<std::string>();
    const int index = std::clamp(mSelected ? mSelected() : 0, 0, std::max(0, static_cast<int>(list.size()) - 1));
    const char* label = list.empty() ? "" : list[static_cast<size_t>(index)].c_str();

    const IColor textColor = mScreenStyle ? mPalette.screenCurve : kButtonText;
    const IText style(11.f, textColor, mScreenStyle ? "RobotoBold" : "Roboto", EAlign::Near, EVAlign::Middle);

    g.DrawText(style, label, IRECT(mRECT.L + 8.f, mRECT.T, mRECT.R - 20.f, mRECT.B), nullptr);
    FillDownTriangle(g, mRECT.R - 10.f, mRECT.MH(), 8.f, 5.f, textColor);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const auto list = mItems ? mItems() : std::vector<std::string>();

    mDropdown.Open(mRECT, list, mSelected ? mSelected() : 0, mPalette,
                   [this](int index)
                   {
                     if (mOnSelect)
                       mOnSelect(index);
                     SetDirty(false);
                   });
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    if (!mHover) { mHover = true; SetDirty(false); }
  }

  void OnMouseOut() override
  {
    if (mHover) { mHover = false; SetDirty(false); }
  }

  /** The library combo doubles as the "name your preset" prompt. */
  void SetTextEntryHandler(std::function<void(const char*)> handler)
  {
    mTextEntryHandler = std::move(handler);
  }

  void OnTextEntryCompletion(const char* str, int valIdx) override
  {
    if (mTextEntryHandler)
      mTextEntryHandler(str);
  }

private:
  const PlatePalette& mPalette;
  DropdownLayer& mDropdown;
  std::function<std::vector<std::string>()> mItems;
  std::function<int()> mSelected;
  std::function<void(int)> mOnSelect;
  std::function<void(const char*)> mTextEntryHandler;
  bool mScreenStyle;
  bool mHover = false;
};

}  // namespace oufx::ui
