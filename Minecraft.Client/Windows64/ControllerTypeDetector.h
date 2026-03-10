#pragma once

#ifdef _WINDOWS64

enum EControllerType
{
	CONTROLLER_TYPE_XBOX,
	CONTROLLER_TYPE_PLAYSTATION,
};

// Scans connected HID devices for PlayStation controllers (DS3, DS4, DualSense).
// Returns CONTROLLER_TYPE_PLAYSTATION if any Sony gamepad is found,
// otherwise CONTROLLER_TYPE_XBOX (the default XInput device).
EControllerType DetectControllerType();

#endif
