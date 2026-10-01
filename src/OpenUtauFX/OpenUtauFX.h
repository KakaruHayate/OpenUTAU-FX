#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "IPlug_include_in_plug_hdr.h"
#include "MixFxDSP.h"
#include "RackLayout.h"

const int kNumPresets = 1;

enum EParams
{
  kParamEnabled = 0,
  kParamEqOn,
  kParamCompOn,
  kParamReverbOn,
  kParamEqPreset,
  kParamCompPreset,
  kParamReverbPreset,
  kParamEqLow,
  kParamEqMidFreq,
  kParamEqMid,
  kParamEqHigh,
  kParamCompThreshold,
  kParamCompRatio,
  kParamCompMakeup,
  kParamReverbSize,
  kParamReverbDamp,
  kParamReverbWet,
  kParamReverbPreDelay,
  kNumParams
};

enum ECtrlTags
{
  kCtrlTagBackground = 1,
  kCtrlTagLibraryCombo,
  kCtrlTagDropdown
};

/** A rack snapshot the user saved from the preset library. */
struct UserPreset
{
  std::string name;
  std::vector<double> values;  // kNumParams entries, non-normalised
};

using namespace iplug;
using namespace igraphics;

class OpenUtauFX final : public Plugin
{
public:
  OpenUtauFX(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnParamChange(int paramIdx) override;
#endif

  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  static constexpr int kMaxBlockFrames = 8192;

  void SetParamValue(int paramIdx, double value);
  oufx::dsp::Chain::Params BuildChainParams() const;

  // Rack library ------------------------------------------------------------
  std::vector<std::string> LibraryNames() const;
  void ApplyLibraryEntry(int index);
  void BeginSaveUserPreset();
  void CommitUserPreset(const char* name);
  void DeleteUserPreset();
  void RefreshLibraryCombo();

  std::unique_ptr<oufx::dsp::Chain> mChain;
  double mChainSampleRate = 0.0;
  std::vector<float> mScratch;
  std::atomic<bool> mParamsDirty { true };

  std::vector<UserPreset> mUserPresets;
  int mLibraryIndex = 0;
  std::string mTrackName = "Track";

  // Held so controls can take references to them that outlive a layout pass.
  oufx::ui::RackTheme mTheme;
  oufx::ui::RackGeometry mGeometry;
};
