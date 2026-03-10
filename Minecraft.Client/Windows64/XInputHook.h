#pragma once

#ifdef _WINDOWS64

// Installs an IAT hook that redirects all XInputGetState calls to first check
// for a DS4/DualSense via direct HID, falling back to real XInput if no
// PlayStation controller is connected.
void InstallDS4XInputHook();

// Called once per frame to poll the DS4 HID device (if open).
// Must be called before InputManager.Tick().
void DS4HIDPoll();

#endif
