# TB Type A 360 (2026 Mac: CLAP, VST3, AU)

The A-type noise reduction unit **Model 360** with its **Cat. No. 22** module,
as an encoder for the **"Dolby A trick" / vocal stresser**, with the band and
STRESS mods. The same sound and panel as the Mountain Lion build
(`TB-Type-A-Model-360-Mountain-Lion`), sample for sample.

CLAP, VST3 and AU (AUv2), universal (Apple Silicon and Intel), macOS 11 and
later. Built and tested on GitHub Actions (clap-validator, pluginval, auval).
Ad-hoc signed, not notarised: see INSTALL.txt. Personal, non-commercial use.

## Front panel

Laid out like the real 360's 1U face, with the silver control plate widened to
take the band mod:

| Control | What it does |
|---|---|
| **Dolby tone** | 850 Hz calibration tone at Dolby level (with its warble). Record only, as on the unit; the NR signal is shorted while it runs. |
| Meter | Output 1 level against Dolby level (average-responding, VU-like). |
| **NR (in - out)** | The on/off. Lit = NR in. Out = the unit as a fixed-gain amplifier. |
| **STRESS** | 0 to +24 dB more of the whole compressed band signal (all bands). 0 = stock Cat. 22; around +18 dB gives the near-equal blend of the classic "vocal stresser" (Sound On Sound's recreation uses about 1.5 : 1 dry to processed). |
| **rec** | Always lit: the unit only records (encodes). |
| **BAND 1-4 level** | The band's summing resistor as a pot: 0 = off, **5 = the stock resistor** (red tick), 10 = +18 dB more of that band. 2.5 = -12 dB. |
| **BAND 1-4 on** | Lit = the band is in. |

Band frequencies (from the filter components): BAND 1 below 80 Hz, BAND 2
80 Hz - 3 kHz, BAND 3 above 3 kHz, BAND 4 above 9 kHz. Band 2 is made on the
card by subtracting bands 1 and 3 from the full signal, so the bands overlap:
switching band 1 (or bands 3 and 4) off leaves a little of band 2's edge, just
as lifting those resistors would on a real card.

| **input** trimpot | -40 to +20 dB into the circuit, as the 360's input trim by the module: lower = more of the effect. Its gain is made back up at the output automatically, so it changes the effect, not the level. |
| **output** trimpot | -20 to +20 dB, the level out. |

The bottom strip holds 8 **presets** (SAVE, then a slot) and the readout.

Levels: a **-18 dBFS** sine is **Dolby level** (+4 dBu at the unit's line input),
and Dolby level comes out at -18 dBFS.

The classic vocal stresser: BAND 1 and BAND 2 off, BAND 3 and 4 on, then
STRESS and the input trim to taste.

## The circuit as modelled

- Input transformer, input level, input amplifier with its 34.6 kHz Sallen-Key low-pass.
- Band filters from the drawing: band 1 Sallen-Key low-pass 80.8 Hz (Q 0.73);
  band 3 Sallen-Key high-pass 2.98 kHz (Q 0.70); band 4 9.58 kHz (Q 0.80);
  band 2 = wideband minus bands 1 and 3.
- Four feedback compressors (FET attenuators controlled from the band's own
  output through a full-wave rectifier and integrator), overshoot limiter diodes.
- Summing resistors R109 11K0, R209 11K3, R309 8K87, R409 9K53 (the band mod).
- Record (encode) only: output = input + NR signal.
- Output 2 amp, line amplifier clipping at about +21 dBm into 600 ohms on the
  18 V rail, 1:2 output transformer.
- Runs at 4x oversampling.

### Estimates

Values the drawings don't fix, as named constants in `Source/DSP/TypeA360.cpp`:

- Compressor gain law and threshold: the real gains are "adjusted on test", so
  they are fitted to the published A-type encode curve: about +10 dB at low
  level (bands 1-3), +15 dB above 9 kHz, +6 dB at -30 dB, +3.5 dB at -20 dB,
  +1 dB at Dolby level.
- Compressor time constants: attack 1 ms / release 28 ms (bands 3, 4), twice
  that for bands 1, 2 (from the integrator RCs), plus a fast speed-up path.
- Limiter level.
- Transformers T2 (A3D426) and T3 (A3D435): only part numbers are known;
  modelled with gentle low- and high-frequency roll-offs and a little core
  saturation at low frequencies and high levels.
- Dolby Tone warble period (about 0.63 s, from the unijunction timing).

## Install

Actions -> newest green run -> Artifacts -> `TB-Type-A-Model-360-mac`; unzip it
and follow `INSTALL.txt` (copy to `/Library/Audio/Plug-Ins/...`, then the
Terminal lines that clear the download quarantine).

## Build

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

On Linux this builds the CLAP and the tests (`typea_test`, `preset_test`,
`clap_host_test`). clap-wrapper v0.17.0 makes the VST3 and AU from the CLAP.
AU: type `aufx`, subtype `TBta`, manufacturer `TBel`.
