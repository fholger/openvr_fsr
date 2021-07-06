Modified OpenVR DLL with AMD FidelityFX SuperResolution Upscaler
---

This modified openvr_api.dll allows you to apply [FidelityFX SuperResolution](https://gpuopen.com/fidelityfx-superresolution/)
upscaling to many SteamVR games, as long as they use D3D11.

To install, find the location of the openvr_api.dll in the game's installation
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

Example results:
- [Skyrim VR](https://imgsli.com/NjAxNTM/0/1)
- [Fallout 4 VR Native vs FSR modes](https://imgsli.com/NjAxNTE/0/1)
- [Fallout 4 VR Native vs FSR upsampling vs CAS sharpening](https://imgsli.com/NTk1OTI/2/1)

### Important disclaimer

This is a best-effort experiment and hack to bring this upscaling technique to VR games
which do not support it natively. Please understand that the approach taken here cannot
guarantee the optimal quality that FSR might, in theory, be capable of. AMD has specific
recommendations where and how FSR should be placed in the render pipeline. Due to the
nature of this generic hack, I cannot guarantee nor control that all of these recommendations
are actually met for any particular game. Please do not judge the quality of FSR solely by
this mod :)

I intend to keep working on the performance, quality and compatibility of this mod, so do check back occasionally.

### Known issues

- Half Life: Alyx and Star Wars: Squadrons do not work, because they don't like you replacing their openvr_dll.api.
- In rare cases, the rendering output may appear unnaturally dark when this mod is active. This is an issue with color space that I hope to be able to fix in a future release.
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
