# Measured LUTs

[日本語](README.ja.md)

## quest3-match-v1.cube

A 3D LUT (33³, `.cube`) that brings the Steam Frame's colors closer to how they look on a Quest 3.
It was computed from measurements of both headsets taken with a SpyderX and ArgyllCMS (`spotread`).

### Usage

Installing FrameColor (this fork) puts this file in `~/Documents/FrameColor/luts/`, and a first install switches it on automatically.

To switch it on or off, open "Display Color Adjustment" in the SteamVR dashboard and use the Color LUT card at the top.

To add it by hand, copy this file to `~/Documents/FrameColor/luts/` on the Frame, or run: `~/.local/bin/framecolor-lut install quest3-match-v1.cube`

### Measurement conditions

| Item | Quest 3 (target) | Steam Frame (corrected) |
|---|---|---|
| Connection | PC SteamVR → Virtual Desktop (AV1, 200 Mbps, Streamer 1.34.22) | PC SteamVR → Steam Frame streaming |
| Refresh rate | 90 Hz | 90 Hz |
| Panel brightness | Default | Default (same as Quest) |
| Eye measured | Left | Left |
| Sensor | SpyderX + ArgyllCMS `spotread` | Same |
| Chart | 9³ grid + independent check colors | 9³ grid + independent check colors |

All FrameColor sliders were at their defaults (the state after Reset) during measurement.

### Results (measured, DeltaE76, difference from the Quest 3)

| State | Mean | 95th percentile | Max |
|---|---:|---:|---:|
| No correction | 18.4 | 35.1 | 52.2 |
| This LUT applied | 3.2 | 7.1 | 8.2 |

Values are for 36 check colors that were not used to compute the LUT. The uncorrected and corrected values come from separate measurement sessions.

### Notes

- **Units vary.** One Frame, one Quest 3, and one sensor were measured. Your Frame may not give the same result.
- Measured at a single point in the center of the left eye. Differences in the right eye, toward the lens edges, or with gaze position are not corrected.
- Changing the panel brightness far from the measured value changes how well it matches.
- Colors the Frame cannot show (out of gamut) and brightness differences remain.
- Treat this LUT as a starting point. Switch it off if it looks wrong to you.
