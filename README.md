# Material Percussion

A physically-modelled percussion synth (VST3 + standalone) built with JUCE.

An **exciter** drives an **object**. The object is either a bank of up to 64 resonant
modes (modal synthesis) or a delay-line loop (digital waveguide). It can be joined to a
second **body** resonator and pushed into **nonlinear** behaviour. The material model
decides where the modes sit, how fast each one dies and how strongly the strike
position excites it.

## Objects

| Model    | Engine    | Physics                                   | Shape control                      |
|----------|-----------|-------------------------------------------|------------------------------------|
| Membrane | modal     | Circular membrane (Bessel zeros)          | Tuning: raw head -> timpani        |
| Wood     | modal     | Free-free Euler-Bernoulli bar             | Tuning: woodblock -> marimba bar   |
| Metal    | modal     | Rectangular plate                         | Aspect: square -> long plate       |
| Glass    | modal     | Ring/shell flexural modes, split doublets | Thickness                          |
| Bell     | modal     | Church-bell partial series, doublets      | Profile: minor -> major third      |
| String   | modal     | Stiff string                              | Stiffness (inharmonicity)          |
| Tube     | waveguide | Stopped + open bore loops, cross-faded    | Bore: stopped (odd harmonics) -> open |
| Wire     | waveguide | String loop with allpass dispersion       | Stiffness                          |

## Exciters

| Exciter   | How it works                                             | Hardness knob | Colour knob        |
|-----------|----------------------------------------------------------|---------------|--------------------|
| Mallet    | Raised-cosine impact, optional bounces and noise burst   | Hardness      | (unused)           |
| Bow       | Stick-slip friction against the object's own velocity    | Pressure      | Tone inside the loop |
| Scrape    | Noise blended with sparse ticks, scaled to the object    | Roughness     | Dark .. bright     |
| Particles | Random grain collisions (shaker / rain / sand)           | Density       | Grain hardness     |
| Feedback  | Object velocity amplified and fed back, level-controlled | Drive         | Picks which modes win |

The continuous exciters (all except Mallet) run for **Length**, or for as long as the key
is held. They are the only thing a note-off affects.

## Nonlinear and body

- **Pitch Drop**: the pitch jumps up on impact and settles.
- **Tension**: loud means stiff. The pitch rises with the object's energy and bends down
  as it decays. On modal objects, energy also cascades from each mode into higher ones,
  which gives a gong or cymbal bloom. The cascade only moves existing energy, so it can't
  run away.
- **Rattle / Gap**: a one-sided contact (a loose part) that the object slaps against once
  it moves further than the gap. The output is a buzz of clipped motion plus sizzle bursts.
- **Body**: a second modal resonator (any model, 32 modes) with its own tune and decay.
  - **Blend** moves the strike from the object to the body.
  - **Couple** is a spring between the two contact points: sympathetic ringing, beating
    and energy exchange. Waveguide objects drive their body one way only, which keeps the
    loop passive.

## Build (Windows)

Requires Visual Studio 2026 with the "Desktop development with C++" workload (it bundles
CMake and Ninja) and Git. JUCE is downloaded automatically on the first configure.

**In Visual Studio:** File > Open > Folder, pick this folder, choose the `x64 Release`
(or `x64 Debug`) preset in the toolbar, then Build > Build All. Output goes to
`out/build/<preset>/`.

**From a Developer PowerShell for VS:**

```
cmake --preset vs2026
cmake --build --preset vs2026-release
```

Outputs (for the `vs2026` preset):

- `build/MaterialPercussion_artefacts/Release/VST3/Material Percussion.vst3`
- `build/MaterialPercussion_artefacts/Release/Standalone/Material Percussion.exe`

To use in Ableton Live, copy the `.vst3` folder to `C:\Program Files\Common Files\VST3`
(admin rights needed), enable "Use VST3 Plug-in System Folders" in Live's Plug-Ins
preferences, and rescan. Load one instance per Drum Rack pad: notes are pitched
relative to C3 (MIDI 60), which is what Drum Rack sends by default.

## Layout

- `Source/dsp/ModeModels.*` - pure functions turning (model, shape, position, decay, damping) into a mode set
- `Source/dsp/VoiceSettings.h` - exciter types and the per-block settings shared by all voices
- `Source/dsp/Exciter.*` - mallet, bow, scrape, particles and feedback exciters
- `Source/dsp/ModalBank.h` - resonator bank with contact point, energy and cascade
- `Source/dsp/Waveguide.*` - tube and wire delay-line loops (fitted loss filter, dispersion, tuning)
- `Source/dsp/Voice.*` - one sounding object: exciter -> object <-> body, rattle, pitch modulation
- `Source/PluginProcessor.*` - parameters, MIDI, voice allocation
- `Source/ui/*` - themes/look-and-feel, knobs, the clickable object view and the mode spectrum display
