# ShapeMod

A VST3 plugin that hosts another plugin and moves its knobs with a shape you draw, locked to your song's tempo.
Built for Ableton Live 12 on Apple Silicon and Intel Macs.

## What it does

- **Draw a shape**: add points, drag the diamonds to curve segments, or switch to Draw mode and paint steps on the grid.
- **Save shapes**: Shapes > Save shape as... stores them in `~/Library/Audio/Presets/ShapeMod/Shapes`. Subfolders there show up under **Libraries** in the Shapes menu.
- **Modulate another plugin**: Load plugin > pick any VST3. It opens inside ShapeMod. Click **Learn** on a target row
  and wiggle a knob in the plugin, or pick a parameter from the list. Up to 8 targets, each with its own range and invert.
- **Tempo sync from 1/8 bar to 4 bars**, plus dotted and triplet feels, phase offset, depth and smoothing.
- **Sync modes**: Song position (locked to the arrangement), Note retrigger (restarts on each MIDI note) or Free run.

## Two versions

| Plugin | Where it goes in Ableton | What to load inside it |
| --- | --- | --- |
| ShapeMod Instrument | Instrument slot of a MIDI track | A synth or sampler (MIDI is passed through) |
| ShapeMod FX | Any audio effect chain | An effect |

## Installing

Download `ShapeMod-mac` from the latest run on the **Actions** tab, unzip it, and follow `INSTALL.txt`.

## Building yourself

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --target ShapeModFX_VST3 ShapeModInstrument_VST3
```

Every push to `main` builds both plugins on GitHub's macOS runners (see `.github/workflows/build-mac.yml`).

Built with [JUCE 8](https://juce.com) under its free Starter licence.
