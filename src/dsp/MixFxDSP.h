/*
 * OpenUTau FX — DSP engine
 *
 * C++17 port of OpenUtau's per-track post-FX chain (OpenUtau.Core/SignalChain):
 *
 *   BiquadEQ          low-shelf 200 Hz / peaking / high-shelf 8 kHz  (RBJ cookbook)
 *   SimpleCompressor  soft-knee feed-forward, stereo-linked peak detector
 *   Freeverb          Schroeder/Moorer network (Jezar at Dreampoint)
 *   Chain             the three in series with the same click-free
 *                     crossfades OpenUtau's MixFxSource applies when a
 *                     module's power switch flips.
 *
 * Header-only.  All Process() calls take interleaved float buffers so the
 * plugin can share one scratch buffer between stages.
 *
 * Ported from OpenUtau (MIT).  Upstream sources:
 *   OpenUtau.Core/SignalChain/Effects/{BiquadEQ,SimpleCompressor,Freeverb}.cs
 *   OpenUtau.Core/SignalChain/MixFxSource.cs
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace oufx::dsp {

constexpr double kPi = 3.14159265358979323846;

// Fixed parameters shared by the DSP and the on-screen curve displays.
constexpr double kEqMidQ      = 0.707;
constexpr double kEqLowShelf  = 200.0;    // Hz
constexpr double kEqHighShelf = 8000.0;   // Hz
constexpr double kCompKneeDb  = 6.0;
constexpr int    kMaxChannels = 8;

// ── BiquadEQ ────────────────────────────────────────────────────────────────

class BiquadEQ
{
public:
  explicit BiquadEQ(double sampleRate = 44100.0, int channels = 2)
  : mSampleRate(sampleRate), mChannels(std::min(channels, kMaxChannels))
  {
  }

  /** Set EQ parameters.  All gains in dB. */
  void Configure(double lowDb, double midFreq, double midQ, double midDb, double highDb)
  {
    constexpr double eps = 0.01;
    const bool wasBypassed = mBypassed;
    mBypassed = std::abs(lowDb) < eps && std::abs(midDb) < eps && std::abs(highDb) < eps;

    if (mBypassed)
      return;

    // Filter memory went stale while bypassed (it can be reconfigured live).
    if (wasBypassed)
      Reset();

    for (int c = 0; c < mChannels; c++)
    {
      mStages[0][c].SetLowShelf(mSampleRate, kEqLowShelf, lowDb);
      mStages[1][c].SetPeak(mSampleRate, midFreq, midQ, midDb);
      mStages[2][c].SetHighShelf(mSampleRate, kEqHighShelf, highDb);
    }
  }

  bool IsBypassed() const { return mBypassed; }

  /** Magnitude response in dB at \p freq Hz (0 dB when bypassed).  For the UI curve. */
  double ResponseDb(double freq) const
  {
    if (mBypassed)
      return 0.0;

    const double w = 2.0 * kPi * freq / mSampleRate;
    return mStages[0][0].MagnitudeDb(w) + mStages[1][0].MagnitudeDb(w) + mStages[2][0].MagnitudeDb(w);
  }

  void Reset()
  {
    for (auto& stage : mStages)
      for (auto& bq : stage)
        bq.Reset();
  }

  /** \p numFrames frames of interleaved audio starting at \p offset samples. */
  void Process(float* buffer, int offset, int numFrames)
  {
    if (mBypassed)
      return;

    for (int i = 0; i < numFrames; i++)
    {
      const int base = offset + i * mChannels;

      for (int c = 0; c < mChannels; c++)
      {
        float x = buffer[base + c];
        x = mStages[0][c].Process(x);
        x = mStages[1][c].Process(x);
        x = mStages[2][c].Process(x);
        buffer[base + c] = x;
      }
    }
  }

private:
  // Single biquad section in Direct Form I.
  struct Biquad
  {
    float mB0 = 1.f, mB1 = 0.f, mB2 = 0.f, mA1 = 0.f, mA2 = 0.f;
    float mX1 = 0.f, mX2 = 0.f, mY1 = 0.f, mY2 = 0.f;

    void Reset() { mX1 = mX2 = mY1 = mY2 = 0.f; }

    float Process(float x)
    {
      const float y = mB0 * x + mB1 * mX1 + mB2 * mX2 - mA1 * mY1 - mA2 * mY2;
      mX2 = mX1; mX1 = x;
      mY2 = mY1; mY1 = y;
      return y;
    }

    /** |H(e^jw)| in dB. */
    double MagnitudeDb(double w) const
    {
      const double c1 = std::cos(w), s1 = std::sin(w);
      const double c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
      const double nr = mB0 + mB1 * c1 + mB2 * c2, ni = mB1 * s1 + mB2 * s2;
      const double dr = 1.0 + mA1 * c1 + mA2 * c2, di = mA1 * s1 + mA2 * s2;
      return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    // RBJ Audio EQ Cookbook coefficients.
    void SetPeak(double fs, double f0, double q, double gainDb)
    {
      const double a = std::pow(10.0, gainDb / 40.0);
      const double w0 = 2.0 * kPi * f0 / fs;
      const double cosw = std::cos(w0);
      const double alpha = std::sin(w0) / (2.0 * q);
      const double a0 = 1.0 + alpha / a;
      Apply(1.0 + alpha * a, -2.0 * cosw, 1.0 - alpha * a, a0, -2.0 * cosw, 1.0 - alpha / a);
    }

    void SetLowShelf(double fs, double f0, double gainDb)
    {
      const double a = std::pow(10.0, gainDb / 40.0);
      const double w0 = 2.0 * kPi * f0 / fs;
      const double cosw = std::cos(w0);
      const double sinw = std::sin(w0);
      const double s = 1.0;
      const double alpha = sinw / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / s - 1.0) + 2.0);
      const double beta = 2.0 * std::sqrt(a) * alpha;
      const double a0 = (a + 1.0) + (a - 1.0) * cosw + beta;
      Apply(a * ((a + 1.0) - (a - 1.0) * cosw + beta),
            2.0 * a * ((a - 1.0) - (a + 1.0) * cosw),
            a * ((a + 1.0) - (a - 1.0) * cosw - beta),
            a0,
            -2.0 * ((a - 1.0) + (a + 1.0) * cosw),
            (a + 1.0) + (a - 1.0) * cosw - beta);
    }

    void SetHighShelf(double fs, double f0, double gainDb)
    {
      const double a = std::pow(10.0, gainDb / 40.0);
      const double w0 = 2.0 * kPi * f0 / fs;
      const double cosw = std::cos(w0);
      const double sinw = std::sin(w0);
      const double s = 1.0;
      const double alpha = sinw / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / s - 1.0) + 2.0);
      const double beta = 2.0 * std::sqrt(a) * alpha;
      const double a0 = (a + 1.0) - (a - 1.0) * cosw + beta;
      Apply(a * ((a + 1.0) + (a - 1.0) * cosw + beta),
            -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw),
            a * ((a + 1.0) + (a - 1.0) * cosw - beta),
            a0,
            2.0 * ((a - 1.0) - (a + 1.0) * cosw),
            (a + 1.0) - (a - 1.0) * cosw - beta);
    }

    void Apply(double b0, double b1, double b2, double a0, double a1, double a2)
    {
      mB0 = static_cast<float>(b0 / a0);
      mB1 = static_cast<float>(b1 / a0);
      mB2 = static_cast<float>(b2 / a0);
      mA1 = static_cast<float>(a1 / a0);
      mA2 = static_cast<float>(a2 / a0);
    }
  };

  double mSampleRate;
  int mChannels;
  bool mBypassed = true;
  Biquad mStages[3][kMaxChannels];
};

// ── SimpleCompressor ────────────────────────────────────────────────────────
// Soft-knee feed-forward compressor / limiter, stereo-linked peak detector.

class SimpleCompressor
{
public:
  explicit SimpleCompressor(double sampleRate = 44100.0, int channels = 2)
  : mSampleRate(sampleRate), mChannels(std::min(channels, kMaxChannels))
  {
  }

  void Configure(double thresholdDb, double ratio, double attackMs, double releaseMs,
                 double makeupDb, double kneeDb = kCompKneeDb)
  {
    const bool wasBypassed = mBypassed;
    mBypassed = ratio <= 1.0001 && std::abs(makeupDb) < 0.01;

    if (wasBypassed && !mBypassed)
      Reset();

    mThresholdDb = thresholdDb;
    mRatio = std::max(1.0, ratio);
    mAtkCoef = std::exp(-1.0 / (std::max(0.05, attackMs) * 0.001 * mSampleRate));
    mRelCoef = std::exp(-1.0 / (std::max(0.05, releaseMs) * 0.001 * mSampleRate));
    mMakeupLinear = std::pow(10.0, makeupDb / 20.0);
    mKneeDb = kneeDb;
  }

  bool IsBypassed() const { return mBypassed; }

  void Reset() { mEnvDb = 0.0; }

  /** Static-curve gain (dB, <= 0, before makeup) for a steady input at \p inputDb.  For the UI curve. */
  static double CurveGainDb(double inputDb, double thresholdDb, double ratio, double kneeDb = kCompKneeDb)
  {
    return StaticGainDb(inputDb - thresholdDb, (1.0 / std::max(1.0, ratio)) - 1.0, kneeDb);
  }

  /** Piecewise quadratic soft knee (DAFX / Zoelzer ch. 4). */
  static double StaticGainDb(double above, double slope, double kneeDb)
  {
    if (above <= -kneeDb / 2.0)
      return 0.0;

    if (above >= kneeDb / 2.0)
      return slope * above;

    const double k = above + kneeDb / 2.0;
    return slope * (k * k) / (2.0 * kneeDb);
  }

  void Process(float* buffer, int offset, int numFrames)
  {
    if (mBypassed)
      return;

    const double slope = (1.0 / mRatio) - 1.0;
    const double atk = mAtkCoef;
    const double rel = mRelCoef;
    const float makeup = static_cast<float>(mMakeupLinear);
    double env = mEnvDb;

    for (int i = 0; i < numFrames; i++)
    {
      const int base = offset + i * mChannels;

      float peak = 0.f;
      for (int c = 0; c < mChannels; c++)
        peak = std::max(peak, std::abs(buffer[base + c]));

      const double detDb = 20.0 * std::log10(static_cast<double>(peak) + 1e-12);
      const double gainDb = StaticGainDb(detDb - mThresholdDb, slope, mKneeDb);

      // Smooth in the log domain; attack when the reduction has to grow.
      const double coef = gainDb < env ? atk : rel;
      env = coef * env + (1.0 - coef) * gainDb;

      const float lin = static_cast<float>(std::pow(10.0, env / 20.0)) * makeup;
      for (int c = 0; c < mChannels; c++)
        buffer[base + c] *= lin;
    }

    mEnvDb = env;
  }

private:
  double mSampleRate;
  int mChannels;
  bool mBypassed = true;

  double mThresholdDb = -18.0;
  double mRatio = 2.0;
  double mAtkCoef = 0.9;
  double mRelCoef = 0.99;
  double mMakeupLinear = 1.0;
  double mKneeDb = kCompKneeDb;
  double mEnvDb = 0.0;
};

// ── Freeverb ────────────────────────────────────────────────────────────────
// 8 parallel combs -> 4 series allpasses per channel.  Mono sum -> network ->
// stereo spread.  Pre-delay on the wet input only; the dry path is untouched.

class Freeverb
{
public:
  static constexpr float kFixedGain = 0.015f;
  static constexpr float kScaleDamp = 0.4f;
  static constexpr float kScaleRoom = 0.28f;
  static constexpr float kOffsetRoom = 0.7f;
  static constexpr int kStereoSpread = 23;
  static constexpr int kMaxPreDelayMs = 200;

  explicit Freeverb(double sampleRate = 44100.0)
  : mSampleRate(sampleRate)
  {
    static const int combTuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int allpassTuning[4] = { 556, 441, 341, 225 };

    const double scale = sampleRate / 44100.0;

    for (int i = 0; i < 8; i++)
    {
      mCombsL[i].Init(static_cast<int>(combTuning[i] * scale));
      mCombsR[i].Init(static_cast<int>((combTuning[i] + kStereoSpread) * scale));
    }

    for (int i = 0; i < 4; i++)
    {
      mAllpassL[i].Init(static_cast<int>(allpassTuning[i] * scale));
      mAllpassR[i].Init(static_cast<int>((allpassTuning[i] + kStereoSpread) * scale));
      mAllpassL[i].SetFeedback(0.5f);
      mAllpassR[i].SetFeedback(0.5f);
    }

    mPreDelayBuf.assign(std::max(1, static_cast<int>(sampleRate * kMaxPreDelayMs / 1000.0)), 0.f);
  }

  void Configure(double roomSize, double damp, double width, double wet, double dry, double preDelayMs = 0.0)
  {
    mWidth = static_cast<float>(std::clamp(width, 0.0, 1.0));
    mWet = static_cast<float>(std::max(0.0, wet));
    mDry = static_cast<float>(std::max(0.0, dry));

    const float fb = static_cast<float>(roomSize * kScaleRoom + kOffsetRoom);
    const float d1 = static_cast<float>(damp * kScaleDamp);

    for (int i = 0; i < 8; i++)
    {
      mCombsL[i].SetFeedback(fb);
      mCombsR[i].SetFeedback(fb);
      mCombsL[i].SetDamp(d1);
      mCombsR[i].SetDamp(d1);
    }

    // preDelaySamples == 0 skips the delay line entirely (no per-sample touch).
    const double pdMs = std::clamp(preDelayMs, 0.0, static_cast<double>(kMaxPreDelayMs));
    mPreDelaySamples = static_cast<int>(std::round(pdMs * mSampleRate / 1000.0));
    if (mPreDelaySamples >= static_cast<int>(mPreDelayBuf.size()))
      mPreDelaySamples = static_cast<int>(mPreDelayBuf.size()) - 1;

    const bool wasBypassed = mBypassed;
    mBypassed = mWet < 1e-4;
    if (wasBypassed && !mBypassed)
      Reset();  // don't replay a tail left over from before the bypass
  }

  bool IsBypassed() const { return mBypassed; }

  /**
   * Approximate RT60 (seconds) of the comb network for the given settings:
   * the low-frequency decay and the shorter high-frequency decay damping leaves.
   * For the UI curve display.
   */
  static void DecaySeconds(double roomSize, double damp, double& low, double& high)
  {
    static const int combTuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };

    const double fb = roomSize * kScaleRoom + kOffsetRoom;
    const double d = damp * kScaleDamp;

    double avgDelay = 0.0;
    for (int t : combTuning)
      avgDelay += t;
    avgDelay /= 8.0 * 44100.0;

    // Amplitude after t is g^(t/avgDelay); solve g^n = 10^-3.  The one-pole
    // damping filter scales loop gain at Nyquist by (1 - d)/(1 + d).
    const double fbHigh = fb * (1.0 - d) / (1.0 + d);
    low  = -3.0 * avgDelay / std::log10(fb);
    high = -3.0 * avgDelay / std::log10(fbHigh);
  }

  void Reset()
  {
    for (auto& c : mCombsL) c.Reset();
    for (auto& c : mCombsR) c.Reset();
    for (auto& a : mAllpassL) a.Reset();
    for (auto& a : mAllpassR) a.Reset();
    std::fill(mPreDelayBuf.begin(), mPreDelayBuf.end(), 0.f);
    mPreDelayIdx = 0;
  }

  /** Stereo only — matches the upstream implementation. */
  void Process(float* buffer, int offset, int numFrames)
  {
    if (mBypassed)
      return;

    const float wet1 = mWet * (mWidth / 2.f + 0.5f);
    const float wet2 = mWet * ((1.f - mWidth) / 2.f);
    const float dry = mDry;
    const int pdLen = mPreDelaySamples;
    const int pdSize = static_cast<int>(mPreDelayBuf.size());

    for (int i = 0; i < numFrames; i++)
    {
      const int idx = offset + i * 2;
      const float inL = buffer[idx];
      const float inR = buffer[idx + 1];
      float input = (inL + inR) * kFixedGain;

      if (pdLen > 0)
      {
        mPreDelayBuf[mPreDelayIdx] = input;
        int readIdx = mPreDelayIdx - pdLen;
        if (readIdx < 0)
          readIdx += pdSize;
        input = mPreDelayBuf[readIdx];
        if (++mPreDelayIdx == pdSize)
          mPreDelayIdx = 0;
      }

      float outL = 0.f, outR = 0.f;
      for (int k = 0; k < 8; k++)
      {
        outL += mCombsL[k].Process(input);
        outR += mCombsR[k].Process(input);
      }
      for (int k = 0; k < 4; k++)
      {
        outL = mAllpassL[k].Process(outL);
        outR = mAllpassR[k].Process(outR);
      }

      buffer[idx]     = outL * wet1 + outR * wet2 + inL * dry;
      buffer[idx + 1] = outR * wet1 + outL * wet2 + inR * dry;
    }
  }

private:
  // Comb with a one-pole low-pass in the feedback path.
  struct CombFilter
  {
    std::vector<float> mBuf;
    int mIdx = 0;
    float mFiltStore = 0.f;
    float mDamp1 = 0.f, mDamp2 = 1.f;
    float mFeedback = 0.5f;

    void Init(int size) { mBuf.assign(static_cast<size_t>(std::max(1, size)), 0.f); }
    void SetFeedback(float fb) { mFeedback = fb; }
    void SetDamp(float d) { mDamp1 = d; mDamp2 = 1.f - d; }

    void Reset()
    {
      std::fill(mBuf.begin(), mBuf.end(), 0.f);
      mFiltStore = 0.f;
      mIdx = 0;
    }

    float Process(float x)
    {
      const float y = mBuf[mIdx];
      mFiltStore = y * mDamp2 + mFiltStore * mDamp1;
      mBuf[mIdx] = x + mFiltStore * mFeedback;
      if (++mIdx == static_cast<int>(mBuf.size()))
        mIdx = 0;
      return y;
    }
  };

  // Standard Schroeder allpass section.
  struct AllpassFilter
  {
    std::vector<float> mBuf;
    int mIdx = 0;
    float mFeedback = 0.5f;

    void Init(int size) { mBuf.assign(static_cast<size_t>(std::max(1, size)), 0.f); }
    void SetFeedback(float fb) { mFeedback = fb; }

    void Reset()
    {
      std::fill(mBuf.begin(), mBuf.end(), 0.f);
      mIdx = 0;
    }

    float Process(float x)
    {
      const float bufOut = mBuf[mIdx];
      const float y = -x + bufOut;
      mBuf[mIdx] = x + bufOut * mFeedback;
      if (++mIdx == static_cast<int>(mBuf.size()))
        mIdx = 0;
      return y;
    }
  };

  double mSampleRate;
  int mPreDelaySamples = 0;
  int mPreDelayIdx = 0;
  std::vector<float> mPreDelayBuf;

  float mWidth = 1.f;
  float mWet = 0.3f;
  float mDry = 0.7f;
  bool mBypassed = true;

  CombFilter mCombsL[8], mCombsR[8];
  AllpassFilter mAllpassL[4], mAllpassR[4];
};

// ── Chain ───────────────────────────────────────────────────────────────────
// EQ -> compressor -> reverb with the master and per-module crossfades from
// MixFxSource: switching a power switch ramps over ~15 ms at 44.1 kHz instead
// of cutting, and a module that has just been switched off is reset so its
// stale tail cannot replay.

class Chain
{
public:
  static constexpr int kFadeFrames = 661;  // ~15 ms at 44.1 kHz

  struct Params
  {
    bool enabled = true;
    bool eqOn = true;
    bool compOn = true;
    bool reverbOn = true;

    double eqLowDb = 0.0;
    double eqMidFreq = 3000.0;
    double eqMidQ = kEqMidQ;
    double eqMidDb = 1.5;
    double eqHighDb = 3.0;

    double compThresholdDb = -18.0;
    double compRatio = 2.0;
    double compAttackMs = 10.0;
    double compReleaseMs = 120.0;
    double compMakeupDb = 2.5;

    double reverbRoomSize = 0.30;
    double reverbDamp = 0.7;
    double reverbWidth = 0.8;
    double reverbWet = 0.18;
    double reverbDry = 0.85;
    double reverbPreDelayMs = 12.0;
  };

  Chain(double sampleRate = 44100.0, int channels = 2)
  : mChannels(channels)
  , mEq(sampleRate, channels)
  , mComp(sampleRate, channels)
  , mReverb(sampleRate)
  , mFadeStep(1.f / static_cast<float>(std::max(1, static_cast<int>(kFadeFrames * sampleRate / 44100.0))))
  {
  }

  /** Pre-allocate the crossfade scratch so Process() never allocates. */
  void Prepare(int maxFrames)
  {
    if (maxFrames <= 0)
      return;

    mMasterDry.assign(static_cast<size_t>(maxFrames) * mChannels, 0.f);
    mStageDry.assign(static_cast<size_t>(maxFrames) * mChannels, 0.f);
  }

  void Reset()
  {
    mEq.Reset();
    mComp.Reset();
    mReverb.Reset();
    mEqGain = mParams.eqOn ? 1.f : 0.f;
    mCompGain = mParams.compOn ? 1.f : 0.f;
    mReverbGain = mParams.reverbOn ? 1.f : 0.f;
    mMasterGain = mParams.enabled ? 1.f : 0.f;
  }

  /** Push a new parameter set: configures the effects and retargets the crossfades. */
  void SetParams(const Params& p)
  {
    mParams = p;

    mEq.Configure(p.eqLowDb, p.eqMidFreq, p.eqMidQ, p.eqMidDb, p.eqHighDb);
    mComp.Configure(p.compThresholdDb, p.compRatio, p.compAttackMs, p.compReleaseMs, p.compMakeupDb);
    mReverb.Configure(p.reverbRoomSize, p.reverbDamp, p.reverbWidth, p.reverbWet, p.reverbDry, p.reverbPreDelayMs);

    mConfigured = true;
  }

  const Params& GetParams() const { return mParams; }

  /** True when no enabled module would change the signal. */
  bool IsAnythingEnabled() const
  {
    return (mParams.eqOn && !mEq.IsBypassed())
        || (mParams.compOn && !mComp.IsBypassed())
        || (mParams.reverbOn && !mReverb.IsBypassed());
  }

  // UI curve helpers — they see the same coefficients the audio thread uses.
  const BiquadEQ& Eq() const { return mEq; }
  const SimpleCompressor& Comp() const { return mComp; }

  /** \p numFrames frames of interleaved audio starting at \p offset samples, in place. */
  void Process(float* buffer, int offset, int numFrames)
  {
    if (!mConfigured)
      return;

    const float masterTarget = mParams.enabled ? 1.f : 0.f;

    if (mMasterGain <= 0.f && masterTarget <= 0.f)
      return;

    if (static_cast<int>(mMasterDry.size()) < numFrames * mChannels)
    {
      mMasterDry.resize(static_cast<size_t>(numFrames) * mChannels);
      mStageDry.resize(static_cast<size_t>(numFrames) * mChannels);
    }

    const bool masterSteady = mMasterGain == 1.f && masterTarget == 1.f;

    if (!masterSteady)
      std::copy(buffer + offset, buffer + offset + numFrames * mChannels, mMasterDry.begin());

    RunStage(mEq, mEqGain, mParams.eqOn, buffer, offset, numFrames);
    RunStage(mComp, mCompGain, mParams.compOn, buffer, offset, numFrames);
    RunStage(mReverb, mReverbGain, mParams.reverbOn, buffer, offset, numFrames);

    if (!masterSteady)
    {
      Crossfade(mMasterDry.data(), buffer + offset, mMasterGain, masterTarget, numFrames);

      if (mMasterGain == 0.f)
      {
        mEq.Reset();
        mComp.Reset();
        mReverb.Reset();
      }
    }
  }

private:
  template <typename Effect>
  void RunStage(Effect& effect, float& gain, bool enabled, float* buffer, int offset, int numFrames)
  {
    const float target = enabled ? 1.f : 0.f;

    if (gain == 0.f && target == 0.f)
      return;

    if (gain == 1.f && target == 1.f)
    {
      effect.Process(buffer, offset, numFrames);
      return;
    }

    std::copy(buffer + offset, buffer + offset + numFrames * mChannels, mStageDry.begin());
    effect.Process(buffer, offset, numFrames);
    Crossfade(mStageDry.data(), buffer + offset, gain, target, numFrames);

    if (gain == 0.f)
      effect.Reset();  // drop stale state so switching back on doesn't replay it
  }

  /** wet[i] = dry[i] + (wet[i] - dry[i]) * g, with g ramping toward target. */
  void Crossfade(const float* dry, float* wet, float& gain, float target, int numFrames)
  {
    float g = gain;

    for (int i = 0; i < numFrames; i++)
    {
      if (g < target)
        g = std::min(target, g + mFadeStep);
      else if (g > target)
        g = std::max(target, g - mFadeStep);

      const int base = i * mChannels;
      for (int c = 0; c < mChannels; c++)
      {
        const float d = dry[base + c];
        wet[base + c] = d + (wet[base + c] - d) * g;
      }
    }

    gain = g;
  }

  int mChannels;
  BiquadEQ mEq;
  SimpleCompressor mComp;
  Freeverb mReverb;

  Params mParams;
  bool mConfigured = false;

  float mMasterGain = 0.f, mEqGain = 0.f, mCompGain = 0.f, mReverbGain = 0.f;
  float mFadeStep;

  std::vector<float> mMasterDry, mStageDry;
};

}  // namespace oufx::dsp
