#pragma once

#ifdef _WINDOWS64

#include <windows.h>
#include <Xinput.h>

// Direct HID reader for DualShock 4 (and DualSense) controllers.
// Opens the device via CreateFile on the HID device path and reads raw
// input reports, translating them to XINPUT_STATE so the rest of the game
// (including the precompiled 4J InputManager) can consume DS4 input
// through the normal XInput pathway.
class DS4HIDInput
{
public:
	DS4HIDInput();
	~DS4HIDInput();

	// Scan for and open a DS4/DualSense HID device. Safe to call repeatedly.
	// Returns true if a device is open and ready.
	bool Open();

	// Close the current device handle.
	void Close();

	// Returns true if a DS4/DualSense is open and readable.
	bool IsOpen() const { return m_hDevice != INVALID_HANDLE_VALUE; }

	// Read the latest HID report and update the internal XINPUT_STATE.
	// Call once per frame before XInputGetState is used.
	void Poll();

	// Get the translated XInput state. Valid after Poll().
	DWORD GetState(XINPUT_STATE *pState);

	// Set the lightbar colour (DS4 only). r, g, b in 0-255.
	void SetLightbar(BYTE r, BYTE g, BYTE b);

private:
	HANDLE m_hDevice;
	HANDLE m_hReadEvent;  // Persistent event for overlapped reads
	XINPUT_STATE m_state;
	DWORD m_packetNumber;
	bool m_isUSB;       // true = USB (64-byte reports), false = Bluetooth (78-byte)
	bool m_isDualSense; // true = DualSense, false = DS4

	void ParseDS4Report(const BYTE *report, int reportLen);
	void ParseDualSenseReport(const BYTE *report, int reportLen);
};

extern DS4HIDInput g_DS4Input;

#endif
