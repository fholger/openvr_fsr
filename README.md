Modified OpenVR DLL with AMD FidelityFX SuperResolution / NVIDIA Image Scaling
---

This modified openvr_api.dll allows you to apply
either [AMD's FidelityFX SuperResolution](https://gpuopen.com/fidelityfx-superresolution/)
or [NVIDIA's Image Scaling](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)
to many SteamVR games, as long as they use D3D11.

### About AMD's FidelityFX Super Resolution

FidelityFX Super Resolution (FSR for short) is an upscaling technique developed by AMD,
but it works on pretty much any graphics card, including NVIDIA cards. The idea is that
the game internally renders to a lower resolution, thus saving GPU time and reaching higher
FPS, as long as it is not bottlenecked by the CPU. The resulting lower resolution render is
then upscaled to the target resolution by FSR, with the aim of restoring some of the lost
detail due to the lower resolution rendering. It does so in two steps - the first being
the actual upscaling to the target resolution, where particular attention is paid to edges
in the lower resolution picture. The second step is a sharpening step to counter some of the
blur introduced by the upscaling.

### About NVIDIA Image Scaling

NVIDIA Image Scaling (NIS for short) is NVIDIA's answer to FSR. Like FSR, it is an upscaling
algorithm intended to scale a lower-resolution rendered frame to a higher-resolution output.
The algorithm works differently, though, and so the output of NIS will differ from that of FSR.
It is hard to say which one is better. It may come down to personal preference and even the
particular game you are using it for. Feel free to experiment with both, that's why both are
available in this mod :)

### Notes about image quality

Note that, unlike DLSS, FSR/NIS is *not* an anti-aliasing solution. Any aliasing and shimmering
edges present in the original image will not be fixed in the output. As such, the final image
quality depends a lot on the particular game you are using it with. *AMD specifically advises
that FSR should be used in conjunction with the highest-quality anti-aliasing setting a game
has to offer.* In the case of VR games, that means enabling MSAA if it is available, or
else TAA. You may also want to experiment with turning off any sort of post-processing effects
in the games, as some of these should ideally run after FSR/NIS, but with this plugin will run
before it and so may negatively affect the image quality.

### Installation instructions

First, download the `openvr_fsr.zip` file from the [latest release](https://github.com/fholger/openvr_fsr/releases/latest) under "Assets".

Then find the location of the openvr_api.dll in the game's installation
folder: 
- It might be located right next to the main executable (e.g. Skyrim, FO4).
- For Unity games, look in: `<GameDir>\<Game>_Data\Plugins`
- For Unreal 4 games, look in: `<GameDir>\Engine\Binaries\ThirdParty\OpenVR\OpenVRvX_Y_Z`

Rename the existing `openvr_api.dll` to `openvr_api.orig.dll`, then extract both
the `openvr_api.dll` and the `openvr_mod.cfg` from the archive to this directory.
You should now edit the `openvr_mod.cfg` to your liking and adjust the `renderScale`
and `sharpness` parameters to your liking.

In case you want to uninstall the mod, simply remove the `openvr_api.dll` file again
and rename the original `openvr_api.orig.dll` back to `openvr_api.dll`.

In case you run into issues, the log file (`openvr_mod.log`) may provide clues to
what's going on.

### Configuration

The mod is configured by editing the values in its config file, `openvr_mod.cfg`. The
most important setting is `renderScale`, which determines the lowered render resolution that
the game will be using internally. If you have set a render resolution of e.g. 2244x2492 in
SteamVR, then that's the target resolution. The internal resolution will be scaled by the value
of `renderScale` in both dimensions. For example, if `renderScale` is set to 0.75, then the
actual render resolution will become 1683x1869. The render is then upscaled by FSR to the
original resolution of 2244x2492.

If you set a value higher than 1 for `renderScale`, then the game will render at the native
resolution, i.e. the one configured in SteamVR. But FSR will then take this render and upscale
it to a resolution multiplied by the value of `renderScale` in each dimension. For example, if
the resolution in SteamVR is 2242x2492 and you have configured a value of 1.3 for `renderScale`,
then the game will render at 2242x2492, but the image will be upscaled by FSR to 2915x3240.

The second relevant parameter is `sharpness`. Generally, the higher you set `sharpness`, the
sharper the final image will appear. You probably want to set this value higher if you lower
`renderScale`, but beware of over-sharpening. The default of 0.9 gives a fairly sharp result.
You can increase it up to 1.0 if you like an even sharper image. But if the image is too
sharp for your taste, consider experimenting with lower values.

To switch between FSR and NIS, set the parameter `useNIS` either to `false` (FSR, default)
or `true` (NIS).

### In-game hotkeys

By default, a few hotkeys are enabled which you can use to modify certain options of
the mod on the fly. While it is not technically possible to switch the mod on and off,
you can switch between FSR and NIS and also adjust the sharpness and sharpen radius
dynamically. Note that any changes you make via the hotkeys is *not* persisted in
the config file and will be reset to the values i the config on the next game launch.

By default, the following hotkeys are available. You can configure the keys in the
config file and also disable hotkeys altogether.

* F1 - toggles between FSR and NIS.
* F2 - toggles debug mode on or off.
* F3 - decreases sharpness by 0.05.
* F4 - increases sharpness by 0.05.
* F5 - decreases sharpening radius by 0.05.
* F6 - increases sharpening radius by 0.05.
* F7 - take a screen capture of the final output and save it as a .dds file next to
     the DLL location

### Performance considerations

While rendering at a lower resolution will save you performance (which is the entire point),
the upscaler does have a fixed cost in GPU time, and this time depends on your GPU and
the target resolution (*not* the render resolution). So the higher your target resolution, the
higher the cost of the upscaler. It means that, the higher your target resolution,
the lower you may have to set the render resolution (by lowering `renderScale`) before you see
an actual net benefit for your GPU times.

A part of the overhead of FSR/NIS can be mitigated by using a sort of "fixed foveated" optimization
where only the center of the image is upscaled by the more expensive FSR algorithm, while the
edges are upscaled by cheaper bilinear sampling. This can be controlled in the mod by the
`radius` setting, where anything within the radius from the center of the image is upscaled
with FSR, and anything outside is upscaled with bilinear filtering. Due to the natural loss
of clarity in the edges of current HMD lenses, even with a fairly small radius you will
probably have a hard time to tell the difference.

### Results

Example results:
- [Skyrim VR](https://imgsli.com/NjAxNTM/0/1)
- [Fallout 4 VR Native vs FSR modes](https://imgsli.com/NjAxNTE/0/1)
- [Fallout 4 VR Native vs FSR upsampling vs CAS sharpening](https://imgsli.com/NTk1OTI/2/1)

### Troubleshooting

- If you encounter issues like the view looking misaligned or mismatched between the eyes, or one eye is sharper
  than the other, try setting `radius` to `2` in the config and check if that fixes it. This will disable a
  performance optimization, but it doesn't always work with all games or headsets.
- If you encounter missing textures or banding, try setting `applyMIPBias` to `false` in the config.
- If your tracking stops working or is misbehaving with the mod applied, there is a chance that you copied the mod DLL
  to the wrong place. Please re-read the installation instructions and take special note of the plugin subfolders for
  Unity and Unreal engines.

### Important disclaimer

This is a best-effort experiment and hack to bring these upscaling techniques to VR games
which do not support them natively. Please understand that the approach taken here cannot
guarantee the optimal quality that FSR or NIS might, in theory, be capable of. AMD has specific
recommendations where and how FSR should be placed in the render pipeline. Due to the
nature of this generic hack, I cannot guarantee nor control that all of these recommendations
are actually met for any particular game. Please do not judge the quality of FSR solely by
this mod :)

I intend to keep working on the performance, quality and compatibility of this mod, so do check back occasionally.

### Known issues

- Half Life: Alyx and Star Wars: Squadrons do not work, because they don't like you replacing their openvr_dll.api.
- Please report any other game that isn't working, assuming that it is a SteamVR game and uses D3D11 for rendering.


OpenVR SDK
---

OpenVR is an API and runtime that allows access to VR hardware from multiple 
vendors without requiring that applications have specific knowledge of the 
hardware they are targeting. This repository is an SDK that contains the API 
and samples. The runtime is under SteamVR in Tools on Steam. 

### Documentation

Documentation for the API is available on the [GitHub Wiki](https://github.com/ValveSoftware/openvr/wiki/API-Documentation)

More information on OpenVR and SteamVR can be found on http://steamvr.com
