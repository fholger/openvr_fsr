Modified OpenVR DLL for Fallout 4 VR
---

This is a quick and dirty hack to "misreport" the Index controllers as Oculus
touch inputs to get usable controlls in FO4VR. Just grep the openvr_api.dll
from `Releases` and copy it into your Fallout 4 VR dir, overwriting the one
already there.

Note: you still need to modify the SteamVR controller bindings for Fallout 4 VR,
because the default legacy bindings are not suitable. Open the SteamVR dashboard,
go to Settings -> Controller Settings, select Fallout 4 VR in the list on the
right. See if you can find my customized bindings (by CABAListic).

If not, edit the default legacy bindings. Here's what you need to do:

1. remove the "Use as button" action from both thumbsticks.
2. modify the "A button" mapping for both controllers to map to the 
   "left/right A button" instead of grip.

Save the bindings. You should be good to go.


OpenVR SDK
---

OpenVR is an API and runtime that allows access to VR hardware from multiple 
vendors without requiring that applications have specific knowledge of the 
hardware they are targeting. This repository is an SDK that contains the API 
and samples. The runtime is under SteamVR in Tools on Steam. 

### Documentation

Documentation for the API is available on the [Github Wiki](https://github.com/ValveSoftware/openvr/wiki/API-Documentation)

More information on OpenVR and SteamVR can be found on http://steamvr.com
