// Params.h
// Parameter table shared by the VST wrapper, the editor and the preset store.
//
// Front-panel controls, left to right as on the panel: Dolby Tone, NR in-out
// (the on/off), STRESS, BAND 1-4 (on/off switch and level pot each), then the input
// and output trimpots on the module cover. The unit always records (encodes):
// the REC lamp is lit permanently; there is no play (decode) mode.

#ifndef TBTYPEA_PARAMS_H
#define TBTYPEA_PARAMS_H

namespace tbtypea
{

enum Param
{
    // Front panel
    kTone = 0, kNr, kStress,
    kB1On, kB1Level, kB2On, kB2Level, kB3On, kB3Level, kB4On, kB4Level,
    // Trimpots by the module
    kInput, kOutput,
    kNumParams
};

enum Kind { kToneSw, kNrSw, kStressKnob, kBandSw, kBandLevel, kInTrim, kOutTrim };

const char* const kParamNames[kNumParams] = {
    "Tone", "NR", "Stress",
    "B1 On", "B1 Level", "B2 On", "B2 Level", "B3 On", "B3 Level", "B4 On", "B4 Level",
    "Input", "Output"
};

const int kParamKind[kNumParams] = {
    kToneSw, kNrSw, kStressKnob,
    kBandSw, kBandLevel, kBandSw, kBandLevel, kBandSw, kBandLevel, kBandSw, kBandLevel,
    kInTrim, kOutTrim
};

const float kDefaults[kNumParams] = {
    0.0f,       // Dolby Tone off
    1.0f,       // NR in
    0.0f,       // STRESS 0 dB: the stock Cat. 22
    1.0f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f,   // every band on, pots at 5 = the stock resistor
    0.6666667f, 0.5f   // input, output trims at 0 dB
};

// Trims: input -40 .. +20 dB: lower = more of the A-type effect. The output
// is made back up by the same amount automatically (as re-trimming the
// output pot), so input changes only the effect, not the level.
// Output -20 .. +20 dB: the overall level.
const double kInMinDb = -40.0, kInMaxDb = 20.0;
const double kOutMinDb = -20.0, kOutMaxDb = 20.0;
inline double inputDb (double v)  { return kInMinDb + (kInMaxDb - kInMinDb) * v; }
inline double outputDb (double v) { return kOutMinDb + (kOutMaxDb - kOutMinDb) * v; }

// STRESS: 0 .. +24 dB more of the whole noise reduction signal (all bands)
const double kStressMaxDb = 24.0;
inline double stressDb (double v) { return kStressMaxDb * v; }

// Band b (0..3): its switch and level parameters
inline int bandOnParam (int b)    { return kB1On + 2 * b; }
inline int bandLevelParam (int b) { return kB1Level + 2 * b; }

inline bool isOn (float v) { return v >= 0.5f; }

} // namespace tbtypea

#endif
