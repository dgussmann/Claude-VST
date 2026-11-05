# Wavesieve

A VST3 effect that turns wavetables into filters and waveshapers. It finds the wavetables already on your Mac: Serum, Vital, Surge XT and any folder you add.

- **Filter mode**: each frame's harmonics become a filter response. Cutoff is the frequency of harmonic 24. Phase modes: Minimum (no latency), Linear, Original and Raw.
- **Shaper mode**: the frame becomes the distortion curve, at 4x oversampling. Controls: Drive, Bias, Phase, Smooth, Overflow (Repeat, Hold, Mirror) and DC filter.

Loads `.wav` (including Serum's `clm` chunk), `.wt`, `.vitaltable` and `.vital`. Vital modifier components such as Frequency Filter are not re-created.
