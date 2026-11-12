// typea_test.cpp
// Offline checks of the TB Type A 360 DSP core: the A-type encode curve,
// encode -> decode returning the input, the band mod switches and pots,
// NR out, the Dolby Tone, the meter, line amp clipping, attack and release,
// stability, automation, CPU; and demo renders (WAV) into the folder given.
// Plain C++98.

#include "../Source/DSP/TypeA360.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

using namespace tbtypea;

namespace
{
int failures = 0;
const int kSr = 44100;
const double kTwoPi = 6.283185307179586;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (! ok) ++failures;
}

bool finite (const std::vector<float>& v)
{
    for (size_t i = 0; i < v.size(); ++i)
        if (! (v[i] == v[i]) || std::fabs (v[i]) > 100.0f) return false;
    return true;
}

double dbfsToAmp (double dB) { return std::pow (10.0, dB / 20.0) * std::sqrt (2.0); }

std::vector<float> sine (double hz, double dbfs, int n)
{
    std::vector<float> v (n);
    const double a = dbfsToAmp (dbfs);
    for (int i = 0; i < n; ++i) v[i] = (float) (a * std::sin (kTwoPi * hz * i / kSr));
    return v;
}

double rmsDb (const std::vector<float>& v, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double) v[i] * v[i];
    return 10.0 * std::log10 (s / (to - from) + 1e-30);
}

struct Settings
{
    bool rec, nr, tone;
    double stress, inDb;
    bool on[kNumBands];
    double lev[kNumBands];
    Settings() : rec (true), nr (true), tone (false), stress (0.0), inDb (0.0)
    {
        for (int b = 0; b < kNumBands; ++b) { on[b] = true; lev[b] = 0.5; }
    }
};

void apply (TypeA360& u, const Settings& s)
{
    u.setRecord (s.rec); u.setNrIn (s.nr); u.setDolbyTone (s.tone);
    u.setStressDb (s.stress); u.setInputGainDb (s.inDb);
    for (int b = 0; b < kNumBands; ++b) { u.setBandOn (b, s.on[b]); u.setBandLevel (b, s.lev[b]); }
}

std::vector<float> run (const Settings& s, const std::vector<float>& in)
{
    TypeA360* u = new TypeA360();
    apply (*u, s);
    u->prepare (kSr);
    std::vector<float> out (in.size());
    u->process (&in[0], &out[0], (int) in.size());
    delete u;
    return out;
}

// Encode boost (dB) of a steady sine, against NR out
double encodeBoost (double hz, double dbfs, const Settings& s)
{
    std::vector<float> in = sine (hz, dbfs, kSr);
    Settings off = s; off.nr = false;
    const std::vector<float> a = run (s, in), b = run (off, in);
    return rmsDb (a, kSr / 2, kSr) - rmsDb (b, kSr / 2, kSr);
}

// Encode with s, decode with the same band settings; error re NR-out path, dB
double roundTripErr (const std::vector<float>& in, const Settings& s)
{
    Settings enc = s; enc.rec = true;
    Settings dec = s; dec.rec = false;
    Settings thru; thru.nr = false;
    const std::vector<float> m = run (enc, in);
    const std::vector<float> o = run (dec, m);
    const std::vector<float> r = run (thru, run (thru, in));
    double e = 0.0, p = 0.0;
    for (size_t i = in.size() / 4; i < in.size(); ++i) { e += (double) (o[i] - r[i]) * (o[i] - r[i]); p += (double) r[i] * r[i]; }
    return 10.0 * std::log10 (e / p + 1e-30);
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const int n = (int) l.size(), bytes = n * 4;
    unsigned char h[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                            0,0,0,0,0,0,0,0,4,0,16,0,'d','a','t','a',0,0,0,0 };
    const int riff = 36 + bytes, sr = kSr, br = kSr * 4;
    std::memcpy (h + 4, &riff, 4); std::memcpy (h + 24, &sr, 4); std::memcpy (h + 28, &br, 4); std::memcpy (h + 40, &bytes, 4);
    std::fwrite (h, 1, 44, f);
    for (int i = 0; i < n; ++i)
    {
        const float a[2] = { l[i], r[i] };
        for (int c = 0; c < 2; ++c)
        {
            float v = a[c] < -1.0f ? -1.0f : (a[c] > 1.0f ? 1.0f : a[c]);
            const short sv = (short) (v * 32767.0f);
            std::fwrite (&sv, 2, 1, f);
        }
    }
    std::fclose (f);
}

// A test programme: a plucked, breathy "vocal" with consonant-like noise bursts
std::vector<float> programme (int n, double dbfs)
{
    std::vector<float> v (n);
    unsigned int seed = 12345u;
    const double a = dbfsToAmp (dbfs);
    double lp = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        const double note = std::fmod (t, 0.5);
        const double env = std::exp (-note * 4.0);
        const double f0 = (std::fmod (t, 2.0) < 1.0) ? 220.0 : 165.0;
        double s = 0.0;
        for (int k = 1; k <= 12; ++k) s += std::sin (kTwoPi * f0 * k * t) / k;
        seed = seed * 1664525u + 1013904223u;
        const double w = ((seed >> 8) & 0xFFFF) / 32768.0 - 1.0;
        lp += (w - lp) * 0.6;
        const double burst = (note < 0.04) ? 0.6 : 0.05;
        v[i] = (float) (a * (0.5 * env * s + burst * (w - lp)));
    }
    return v;
}
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";

    std::printf ("\nEncode curve (boost in dB against NR out; levels re Dolby level = -18 dBFS)\n");
    {
        Settings s;
        const double hz[4] = { 100.0, 1000.0, 5000.0, 15000.0 };
        const int lv[6] = { -50, -40, -30, -20, -10, 0 };
        double c[4][6];
        for (int f = 0; f < 4; ++f)
        {
            std::printf ("  %6.0f Hz:", hz[f]);
            for (int k = 0; k < 6; ++k)
            {
                c[f][k] = encodeBoost (hz[f], -18.0 + lv[k], s);
                std::printf (" %+5.1f", c[f][k]);
            }
            std::printf ("\n");
        }
        check (c[1][0] > 9.0 && c[1][0] < 11.0, "1 kHz at -50 dB: about +10 dB");
        check (c[0][0] > 9.0 && c[0][0] < 11.0, "100 Hz at -50 dB: about +10 dB");
        check (c[3][0] > 14.0 && c[3][0] < 17.0, "15 kHz at -50 dB: about +15 dB");
        check (c[1][2] > 5.0 && c[1][2] < 7.5, "1 kHz at -30 dB: about +6 dB");
        check (c[1][3] > 2.5 && c[1][3] < 4.5, "1 kHz at -20 dB: about +3.5 dB");
        check (c[1][5] < 1.5, "1 kHz at Dolby level: about +1 dB or less");
        bool mono = true;
        for (int f = 0; f < 4; ++f)
            for (int k = 1; k < 6; ++k)
                if (c[f][k] > c[f][k - 1] + 0.05) mono = false;
        check (mono, "boost falls as the level rises, every band");
    }

    std::printf ("\nEncode -> decode returns the input\n");
    {
        std::vector<float> p = programme (kSr * 2, -24.0);
        Settings s;
        const double e0 = roundTripErr (p, s);
        Settings m; m.on[3] = false; m.lev[1] = 0.25; m.lev[2] = 0.35;
        const double e1 = roundTripErr (p, m);
        std::vector<float> q = sine (440.0, -48.0, kSr);
        const double e2 = roundTripErr (q, s);
        std::printf ("  programme %.1f dB, programme with the bands changed %.1f dB, quiet sine %.1f dB\n", e0, e1, e2);
        check (e0 < -35.0, "programme decodes back to the input");
        check (e1 < -35.0, "with band mod settings matched on both units");
        check (e2 < -50.0, "quiet sine decodes back exactly");
    }

    std::printf ("\nBand mod: switches and pots\n");
    {
        Settings s;
        const double full15 = encodeBoost (15000.0, -60.0, s);
        Settings s4 = s; s4.on[3] = false;
        const double no4 = encodeBoost (15000.0, -60.0, s4);
        Settings s34 = s4; s34.on[2] = false;
        const double no34 = encodeBoost (15000.0, -60.0, s34);
        std::printf ("  15 kHz quiet: all bands %+.1f, BAND 4 off %+.1f, BANDS 3+4 off %+.1f\n", full15, no4, no34);
        check (no4 < full15 - 3.0, "BAND 4 off takes away most of the top boost");
        check (no34 < 3.0, "BANDS 3 and 4 off: little boost left at 15 kHz (band 2's edge)");

        const double full50 = encodeBoost (50.0, -60.0, s);
        Settings s1 = s; s1.on[0] = false;
        const double no1 = encodeBoost (50.0, -60.0, s1);
        std::printf ("  50 Hz quiet: all bands %+.1f, BAND 1 off %+.1f\n", full50, no1);
        check (no1 < full50 - 1.5, "BAND 1 off lowers the bass boost (band 2 overlaps it, as on the card)");

        double prev = -1e9; bool rising = true;
        std::printf ("  1 kHz quiet, BAND 2 pot 0, 2.5, 5 (stock), 7.5, 10:");
        for (int k = 0; k <= 4; ++k)
        {
            Settings sp = s; sp.lev[1] = k / 4.0;
            const double b = encodeBoost (1000.0, -60.0, sp);
            std::printf (" %+.1f", b);
            if (b < prev - 0.01) rising = false;
            prev = b;
        }
        std::printf ("\n");
        check (rising, "BAND 2 pot raises the 1 kHz boost from off, past stock");
        Settings z = s; for (int b = 0; b < kNumBands; ++b) z.lev[b] = 0.0;
        check (std::fabs (encodeBoost (1000.0, -60.0, z)) < 0.05, "all pots at zero: no boost");
        check (std::fabs (TypeA360::bandLevelDb (0.25) + 12.04) < 0.05, "pot at 2.5 = -12 dB of the stock band signal");
        check (std::fabs (TypeA360::bandLevelDb (0.5)) < 0.01, "pot at 5 = the stock resistor");
        check (std::fabs (TypeA360::bandLevelDb (1.0) - 18.0) < 0.01, "pot at 10 = +18 dB over stock");
    }

    std::printf ("\nSTRESS and the input trim (bands 3 and 4 only)\n");
    {
        Settings s; s.on[0] = false; s.on[1] = false;
        const double b0 = encodeBoost (12000.0, -18.0, s);
        Settings s18 = s; s18.stress = 18.0;
        const double b18 = encodeBoost (12000.0, -18.0, s18);
        Settings sIn = s; sIn.inDb = -20.0;
        const double bIn = encodeBoost (12000.0, -18.0, sIn);
        std::printf ("  12 kHz at Dolby level: stock %+.1f dB, STRESS +18 %+.1f dB, input -20 %+.1f dB\n", b0, b18, bIn);
        check (b18 > b0 + 8.0, "STRESS +18 adds a lot more of the compressed top");
        check (bIn > b0 + 4.0, "turning the input down gives more of the effect");
        Settings off; off.nr = false; off.inDb = -30.0;
        std::vector<float> in = sine (1000.0, -18.0, kSr);
        const double g = rmsDb (run (off, in), kSr / 2, kSr) - rmsDb (in, kSr / 2, kSr);
        std::printf ("  NR out with input -30 dB: level %+.2f dB\n", g);
        check (std::fabs (g) < 0.3, "the input trim is made back up at the output");
    }

    std::printf ("\nNR out, meter, line amp\n");
    {
        Settings off; off.nr = false;
        std::vector<float> in = sine (1000.0, -18.0, kSr);
        TypeA360 u; apply (u, off); u.prepare (kSr);
        std::vector<float> o (kSr);
        u.process (&in[0], &o[0], kSr);
        const double g = rmsDb (o, kSr / 2, kSr) - rmsDb (in, kSr / 2, kSr);
        std::printf ("  NR out gain at Dolby level %+.2f dB, meter %+.2f dB\n", g, u.meterDb());
        check (std::fabs (g) < 0.3, "NR out: unity gain at Dolby level");
        check (std::fabs (u.meterDb()) < 0.5, "meter reads Dolby level for a -18 dBFS sine");

        std::vector<float> hot = sine (1000.0, -18.0 + 26.0, kSr);
        std::vector<float> oh (kSr);
        TypeA360 v; apply (v, off); v.prepare (kSr);
        v.process (&hot[0], &oh[0], kSr);
        double pk = 0.0;
        for (int i = kSr / 2; i < kSr; ++i) pk = std::max (pk, (double) std::fabs (oh[i]));
        const double pkDb = 20.0 * std::log10 (pk / dbfsToAmp (-18.0));
        std::printf ("  +26 dB over Dolby level in: output peak %.1f dB over Dolby level peak\n", pkDb);
        check (pkDb > 15.0 && pkDb < 20.0, "line amp clips at about +17 dB over Dolby level (+21 dBm)");
    }

    std::printf ("\nDolby Tone\n");
    {
        Settings t; t.tone = true;
        std::vector<float> z (kSr * 2, 0.0f);
        std::vector<float> o = run (t, z);
        const double lvl = rmsDb (o, kSr, kSr * 2) - 20.0 * std::log10 (dbfsToAmp (-18.0) / std::sqrt (2.0));
        // Frequency by zero crossings over 0.1 s away from the warble
        int zc = 0;
        for (int i = kSr + 3000; i < kSr + 3000 + kSr / 10; ++i)
            if (o[i - 1] < 0.0f && o[i] >= 0.0f) ++zc;
        std::printf ("  REC: level %+.2f dB re Dolby level, about %d Hz\n", lvl, zc * 10);
        check (std::fabs (lvl) < 0.5, "tone at Dolby level");
        check (zc >= 84 && zc <= 87, "tone at 850 Hz");
        Settings tp = t; tp.rec = false;
        std::vector<float> op = run (tp, z);
        check (rmsDb (op, kSr, kSr * 2) < -120.0, "PLAY: the tone is locked out (record interlock)");
    }

    std::printf ("\nAttack and release\n");
    {
        Settings s;
        const int n = kSr * 2;
        std::vector<float> in (n);
        for (int i = 0; i < n; ++i)
        {
            const double a = (i >= kSr / 2 && i < kSr) ? dbfsToAmp (-18.0) : dbfsToAmp (-58.0);
            in[i] = (float) (a * std::sin (kTwoPi * 1000.0 * i / kSr));
        }
        TypeA360 u; apply (u, s); u.prepare (kSr);
        std::vector<float> o (n);
        double gStep = 0.0, gRel = 0.0;
        for (int i = 0; i < n; i += 441)
        {
            u.process (&in[i], &o[i], 441);
            if (i == kSr / 2 + 441 * 2) gStep = u.bandGainReduction (1);
            if (i == kSr + 441 * 20) gRel = u.bandGainReduction (1);
        }
        std::printf ("  band 2 attenuator 20 ms after a step to Dolby level %.3f, 200 ms after it ends %.3f\n", gStep, gRel);
        check (gStep < 0.2, "compressor clamps down within a few ms");
        check (gRel > 0.9, "and recovers within 200 ms");
        check (finite (o), "output finite");
    }

    std::printf ("\nStability sweep\n");
    {
        unsigned int seed = 777u;
        bool ok = true;
        for (int trial = 0; trial < 40 && ok; ++trial)
        {
            Settings s;
            seed = seed * 1664525u + 1013904223u; s.rec = (seed >> 16) & 1;
            seed = seed * 1664525u + 1013904223u; s.nr = (seed >> 16) & 1;
            seed = seed * 1664525u + 1013904223u; s.tone = ((seed >> 16) & 7) == 0;
            for (int b = 0; b < kNumBands; ++b)
            {
                seed = seed * 1664525u + 1013904223u; s.on[b] = (seed >> 16) & 3;
                seed = seed * 1664525u + 1013904223u; s.lev[b] = ((seed >> 8) & 0xFFFF) / 65535.0;
            }
            seed = seed * 1664525u + 1013904223u;
            const double hz = 41.0 * std::pow (2.0, ((seed >> 8) & 0xFFFF) / 65535.0 * 8.5);
            seed = seed * 1664525u + 1013904223u;
            const double dbfs = -80.0 + ((seed >> 8) & 0xFFFF) / 65535.0 * 86.0;
            std::vector<float> in = sine (hz, dbfs, kSr / 4);
            for (size_t i = 0; i < in.size(); ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                in[i] += (float) ((((seed >> 8) & 0xFFFF) / 32768.0 - 1.0) * (trial % 5 == 0 ? 1.0 : 0.0));
            }
            std::vector<float> o = run (s, in);
            if (! finite (o)) { ok = false; std::printf ("  failed: trial %d, %.0f Hz, %.0f dBFS\n", trial, hz, dbfs); }
        }
        check (ok, "40 random settings x levels x frequencies, with loud noise: finite and bounded");
    }

    std::printf ("\nAutomation and CPU\n");
    {
        TypeA360 u; u.prepare (kSr);
        std::vector<float> in = programme (kSr * 2, -18.0), o (in.size());
        for (size_t pos = 0; pos < in.size(); pos += 64)
        {
            const double t = (double) pos / in.size();
            u.setRecord (std::sin (kTwoPi * 3.0 * t) > 0.0);
            u.setNrIn (std::sin (kTwoPi * 5.0 * t) > -0.5);
            u.setDolbyTone (std::sin (kTwoPi * 2.0 * t) > 0.8);
            for (int b = 0; b < kNumBands; ++b)
            {
                u.setBandOn (b, std::sin (kTwoPi * (b + 2) * t) > -0.3);
                u.setBandLevel (b, 0.5 + 0.5 * std::sin (kTwoPi * (b + 1) * 1.7 * t));
            }
            u.setInputGainDb (20.0 * std::sin (kTwoPi * 1.3 * t));
            u.setOutputGainDb (-20.0 * std::sin (kTwoPi * 0.9 * t));
            const int n = (int) ((in.size() - pos) < 64 ? in.size() - pos : 64);
            u.process (&in[pos], &o[pos], n);
        }
        check (finite (o), "every control automated while playing");

        TypeA360 c; c.prepare (kSr);
        std::vector<float> big = programme (kSr * 5, -18.0), ob (big.size());
        const std::clock_t t0 = std::clock();
        c.process (&big[0], &ob[0], (int) big.size());
        const double secs = (double) (std::clock() - t0) / CLOCKS_PER_SEC / 5.0;
        std::printf ("  CPU: %.1f%% of one core per channel at 44.1 kHz\n", 100.0 * secs);
        Settings dec; dec.rec = false;
        TypeA360 d; apply (d, dec); d.prepare (kSr);
        const std::clock_t t1 = std::clock();
        d.process (&big[0], &ob[0], (int) big.size());
        const double secs2 = (double) (std::clock() - t1) / CLOCKS_PER_SEC / 5.0;
        std::printf ("  CPU (Play, decoding): %.1f%% of one core per channel\n", 100.0 * secs2);
        check (secs < 0.25 && secs2 < 0.35, "CPU reasonable");
    }

    std::printf ("\nDemo renders in %s\n", dir.c_str());
    {
        const std::vector<float> p = programme (kSr * 4, -26.0);
        Settings off; off.nr = false;
        writeWav (dir + "/00_dry_through_unit.wav", run (off, p), run (off, p));
        Settings enc;
        writeWav (dir + "/01_encode_all_bands.wav", run (enc, p), run (enc, p));
        Settings air; air.on[0] = false; air.on[1] = false;
        Settings stress = air; stress.stress = 18.0;
        writeWav (dir + "/06_bands_3_4_stress_18.wav", run (stress, p), run (stress, p));
        writeWav (dir + "/02_encode_bands_3_4_only.wav", run (air, p), run (air, p));
        Settings top; top.on[0] = false; top.on[1] = false; top.on[2] = false;
        writeWav (dir + "/03_encode_band_4_only.wav", run (top, p), run (top, p));
        Settings rt; rt.rec = false;
        const std::vector<float> e = run (enc, p);
        writeWav (dir + "/04_encode_then_decode.wav", run (rt, e), run (rt, e));
        Settings t; t.tone = true;
        std::vector<float> z (kSr * 2, 0.0f);
        writeWav (dir + "/05_dolby_tone.wav", run (t, z), run (t, z));
        std::printf ("  7 files written\n");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL DSP CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
