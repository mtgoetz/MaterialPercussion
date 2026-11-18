# Material Percussion

A physically-modelled percussion synth (VST3 + standalone) built with JUCE.

An **exciter** drives an **object**. The object is either a bank of up to 128 resonant
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
| Captured | modal     | Up to 128 modes measured from a recording | Stretch: squeeze / spread partials |

## Exciters

| Exciter   | How it works                                             | Hardness knob | Colour knob        |
|-----------|----------------------------------------------------------|---------------|--------------------|
| Mallet    | Raised-cosine impact, optional bounces and noise burst   | Hardness      | (unused)           |
| Bow       | Stick-slip friction against the object's own velocity    | Pressure      | Tone inside the loop |
| Scrape    | Noise blended with sparse ticks, scaled to the object    | Roughness     | Dark .. bright     |
| Particles | Random grain collisions (shaker / rain / sand)           | Density       | Grain hardness     |
| Feedback  | Object velocity amplified and fed back, level-controlled | Drive         | Picks which modes win |
| Sample    | The captured recording's attack (see Capture)            | Strike        | Tone of the attack |

The continuous exciters (Bow, Scrape, Particles, Feedback) run for **Length**, or for as
long as the key is held. They are the only thing a note-off affects.

## Capture

Drop a recording of a single hit (WAV, AIFF, FLAC or OGG) anywhere on the plugin, or pick
the **Captured** model and click **Load sound...**. A background thread analyses it:

1. It finds the onset and takes one long FFT. Its peaks are the candidate modes.
2. For each candidate it shifts that band down to 0 Hz and decimates it. Then it fits
   damped exponentials by linear prediction. This gives each mode's frequency, decay,
   amplitude and phase, and separates close beating pairs (bell and glass doublets) that
   the FFT can't resolve.
3. It keeps the strongest 128 modes. These become the **Captured** model.
4. It subtracts the modes from the recording. What's left is the stick click, scrape or
   air, and that becomes the attack the **Sample** exciter plays.

Loading sets the knobs to their as-recorded positions: Pitch is the recording's pitch;
Shape, Position and Damping are 50%; Decay is 1 s; the exciter is Sample with Strike and
Attack at 100%. With those settings a hit plays the recording back. From there each knob
bends it:

- **Stretch** moves the partials together or apart.
- **Position** tilts it darker or brighter.
- **Decay** scales the ring times.
- **Damping** makes the high modes ring longer or die faster.
- **Pitch** transposes it.

Every exciter, the nonlinear section and the body work on the captured object. Bowing a
recorded pan or putting a recorded bell on a wood body both work. The Sample exciter also
works on the other models: at low **Strike**, the recorded attack is what drives the
object. The capture is saved with the plugin state.

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
- `Source/dsp/Capture.*` - modal analysis of a recording (peak picking, per-band linear prediction, residual)
- `Source/dsp/Exciter.*` - mallet, bow, scrape, particles, feedback and sample exciters
- `Source/dsp/ModalBank.h` - resonator bank with contact point, energy and cascade
- `Source/dsp/Waveguide.*` - tube and wire delay-line loops (fitted loss filter, dispersion, tuning)
- `Source/dsp/Voice.*` - one sounding object: exciter -> object <-> body, rattle, pitch modulation
- `Source/PluginProcessor.*` - parameters, MIDI, voice allocation, loading/saving captures
- `Source/ui/*` - themes/look-and-feel, knobs, the clickable object view and the mode spectrum display
