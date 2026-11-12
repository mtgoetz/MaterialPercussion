# Material Percussion

A physically-modelled percussion synth (VST3 + standalone) built with JUCE.

Each hit excites a bank of up to 64 resonant **modes**. The material model decides
where those modes sit (frequency ratios from the physics of membranes, bars, plates,
shells, bells and strings), how fast each one dies (material damping), and how loudly
each is excited (the mode shape at the strike position, filtered by mallet hardness).

## Models

| Model    | Physics                                   | Shape control                     |
|----------|-------------------------------------------|-----------------------------------|
| Membrane | Circular membrane (Bessel zeros)          | Tuning: raw head -> timpani       |
| Wood     | Free-free Euler-Bernoulli bar             | Tuning: woodblock -> marimba bar  |
| Metal    | Rectangular plate                         | Aspect: square -> long plate      |
| Glass    | Ring/shell flexural modes, split doublets | Thickness                         |
| Bell     | Church-bell partial series, doublets      | Profile: minor -> major third     |
| String   | Stiff string                              | Stiffness (inharmonicity)         |

## Build (Windows)

Requires Visual Studio 2022 (or Build Tools) with the C++ workload, CMake >= 3.22 and Git.

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Outputs:

- `build/MaterialPercussion_artefacts/Release/VST3/Material Percussion.vst3`
- `build/MaterialPercussion_artefacts/Release/Standalone/Material Percussion.exe`

To use in Ableton Live, copy the `.vst3` folder to `C:\Program Files\Common Files\VST3`
(admin rights needed), enable "Use VST3 Plug-in System Folders" in Live's Plug-Ins
preferences, and rescan. Load one instance per Drum Rack pad: notes are pitched
relative to C3 (MIDI 60), which is what Drum Rack sends by default.

## Layout

- `Source/dsp/ModeModels.*` - pure functions turning (model, shape, position, decay, damping) into a mode set
- `Source/dsp/ModalVoice.*` - exciter + resonator bank for one voice
- `Source/PluginProcessor.*` - parameters, MIDI, voice allocation
- `Source/ui/*` - themes/look-and-feel, knobs, the clickable object view and the mode spectrum display
