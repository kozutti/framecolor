# Color LUTs

[日本語](EXTERNAL_LUT.ja.md)

You can pick a 3D LUT in `.cube` format from the SteamVR dashboard and switch it on or off. Changes apply while SteamVR keeps running.

## Usage

Put `.cube` files in this folder. The installer or the dashboard creates it if it does not exist.

```text
~/Documents/FrameColor/luts/
```

In the Color LUT card at the top of the dashboard, the ON/OFF button switches the LUT, and `<` `>` choose a file. Choosing a file switches it on. The folder is checked every 2 seconds, so new files show up almost immediately. Changes take effect within about a second, without restarting SteamVR. The top-right corner shows the state: Applied, Off (original colors), Error, and so on.

Installing adds the bundled measured LUT `quest3-match-v1.cube` (brings the colors closer to a Quest 3) to this folder. It is not switched on automatically. See [luts/README.md](luts/README.md) for details.

Brightness, saturation, contrast, and gamma adjustments are applied on top of the selected LUT. To see the LUT alone, select the Standard preset. Panel brightness is a separate setting.

Supported `.cube` files have a size of 2–65, a domain of 0–1, and finite RGB values within 0–1. LUTs of a different size are interpolated to the internal size. GPUs without 16-bit linear filtering use 8-bit precision.

## Commands

```sh
~/.local/bin/framecolor-lut list
~/.local/bin/framecolor-lut select quest3-match-v1.cube
~/.local/bin/framecolor-lut on
~/.local/bin/framecolor-lut off
~/.local/bin/framecolor-lut status
~/.local/bin/framecolor-lut validate ~/my-measurement.cube
~/.local/bin/framecolor-lut install ~/my-measurement.cube
```

`disable` is an alias for `off`. `install` validates a LUT, copies it into the folder, selects it, and switches it on. `--restart` also restarts SteamVR, but selecting, switching, and installing do not need it.

`status` shows `lut.txt`, which the dashboard passes to the layer, and `lut-status.txt`, which the layer writes. If a LUT fails to load, the previous LUT stays in use and the reason is shown in the state.

## Migrating from the old format

If `~/.config/framecolor/external-lut.cube` exists at install time and there is no `lut.txt` yet, the installer copies that file into the folder and selects it. The original file is kept. Even without migration, the layer still loads the old file when `lut.txt` is missing.

Uninstalling removes `lut.txt` and `lut-status.txt`. The LUT folder, LUT files, and old-format LUT are not deleted.

## When measuring or verifying

Presets and sliders change the colors after the LUT. To check the LUT alone, select the Standard preset. Keep the panel brightness at the value used for measurement.

License notices of the original project and third-party code are kept unchanged.
