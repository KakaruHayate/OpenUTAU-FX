/*
 * OpenUTau FX — factory presets
 *
 * Mirrors OpenUtau.Core/SignalChain/Effects/FxPresets.cs: the same three
 * tables of module presets (EQ / compressor / reverb) with the same values,
 * plus a small rack library that pairs one entry from each table.
 *
 * A module preset only carries the values the dialog exposes as knobs; the
 * compressor attack/release and the reverb width/dry come along silently, the
 * way MixFxSource pulls them out of the table when it configures the chain.
 */

#pragma once

#include <cstddef>
#include <string_view>

namespace oufx::dsp {

struct EqPreset
{
  const char* key;
  const char* label;
  double lowDb;
  double midFreq;
  double midQ;
  double midDb;
  double highDb;
};

struct CompPreset
{
  const char* key;
  const char* label;
  double thresholdDb;
  double ratio;
  double attackMs;
  double releaseMs;
  double makeupDb;
};

struct ReverbPreset
{
  const char* key;
  const char* label;
  double roomSize;
  double damp;
  double width;
  double wet;
  double dry;
  double preDelayMs;
};

inline constexpr EqPreset kEqPresets[] = {
  // key            label          lowDb  midFreq  midQ   midDb  highDb
  { "off",          "Off",          0.0,   1000.0,  0.707,  0.0,   0.0  },
  { "vocal_air",    "Vocal Air",    0.0,   3000.0,  0.707,  1.5,   3.0  },
  { "warm",         "Warm",         2.0,    500.0,  0.707,  0.0,  -1.0  },
  { "demud",        "Demud",        0.0,    200.0,  1.0,   -3.0,   0.0  },
  { "telephone",    "Telephone",   -6.0,   1500.0,  0.707,  4.0,  -6.0  },
};

inline constexpr CompPreset kCompPresets[] = {
  // key       label      threshold  ratio  attack  release  makeup
  { "off",     "Off",        0.0,    1.0,   10.0,   100.0,   0.0  },
  { "gentle",  "Gentle",   -18.0,    2.0,   10.0,   120.0,   2.5  },
  { "pop",     "Pop",      -14.0,    3.0,    5.0,    80.0,   4.0  },
  { "limit",   "Limit",     -3.0,   10.0,    1.0,    50.0,   0.0  },
};

inline constexpr ReverbPreset kReverbPresets[] = {
  // key            label           size   damp  width  wet    dry    preDelay
  { "off",          "Off",          0.0,   0.0,   1.0,   0.0,   1.0,    0.0 },
  { "small_room",   "Small Room",   0.30,  0.7,   0.8,   0.18,  0.85,  12.0 },
  { "vocal_plate",  "Vocal Plate",  0.55,  0.3,   1.0,   0.22,  0.85,  25.0 },
  { "hall",         "Hall",         0.85,  0.4,   1.0,   0.28,  0.80,  40.0 },
  { "ambient",      "Ambient",      0.92,  0.2,   1.0,   0.35,  0.70,  60.0 },
};

inline constexpr int kNumEqPresets     = static_cast<int>(sizeof(kEqPresets) / sizeof(kEqPresets[0]));
inline constexpr int kNumCompPresets   = static_cast<int>(sizeof(kCompPresets) / sizeof(kCompPresets[0]));
inline constexpr int kNumReverbPresets = static_cast<int>(sizeof(kReverbPresets) / sizeof(kReverbPresets[0]));

/** A rack is one module preset per faceplate plus the reverb's wet trim. */
struct RackPreset
{
  const char* label;
  const char* eqKey;
  const char* compKey;
  const char* reverbKey;
  double reverbWet;  // multiplies the reverb table's wet, as the dialog's WET knob does
};

inline constexpr RackPreset kRackPresets[] = {
  // label        EQ            Comp      Reverb         wet
  { "Default",    "vocal_air",  "gentle", "small_room",  1.0 },
  { "Flat",       "off",        "off",    "off",         1.0 },
  { "Vocal Air",  "vocal_air",  "gentle", "vocal_plate", 1.0 },
  { "Pop Lead",   "vocal_air",  "pop",    "vocal_plate", 1.0 },
  { "Warm Hall",  "warm",       "gentle", "hall",        1.0 },
  { "Ambient",    "vocal_air",  "gentle", "ambient",     1.0 },
};

inline constexpr int kNumRackPresets = static_cast<int>(sizeof(kRackPresets) / sizeof(kRackPresets[0]));

inline int FindEqPreset(const char* key)
{
  if (!key)
    return 0;

  for (int i = 0; i < kNumEqPresets; i++)
    if (std::string_view(kEqPresets[i].key) == key)
      return i;

  return 0;
}

inline int FindCompPreset(const char* key)
{
  if (!key)
    return 0;

  for (int i = 0; i < kNumCompPresets; i++)
    if (std::string_view(kCompPresets[i].key) == key)
      return i;

  return 0;
}

inline int FindReverbPreset(const char* key)
{
  if (!key)
    return 0;

  for (int i = 0; i < kNumReverbPresets; i++)
    if (std::string_view(kReverbPresets[i].key) == key)
      return i;

  return 0;
}

}  // namespace oufx::dsp
