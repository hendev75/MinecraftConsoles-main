#include "stdafx.h"
#ifdef _WINDOWS64

#include "DS4HIDInput.h"
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <math.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

DS4HIDInput g_DS4Input;

// Sony vendor ID and known product IDs
static const USHORT SONY_VID       = 0x054C;
static const USHORT DS4_V1_PID     = 0x05C4;
static const USHORT DS4_V2_PID     = 0x09CC;
static const USHORT DUALSENSE_PID  = 0x0CE6;

DS4HIDInput::DS4HIDInput()
	: m_hDevice(INVALID_HANDLE_VALUE)
	, m_hReadEvent(NULL)
	, m_packetNumber(0)
	, m_isUSB(true)
	, m_isDualSense(false)
{
	memset(&m_state, 0, sizeof(m_state));
}

DS4HIDInput::~DS4HIDInput()
{
	Close();
}

bool DS4HIDInput::Open()
{
	if (m_hDevice != INVALID_HANDLE_VALUE)
		return true; // Already open

	GUID hidGuid;
	HidD_GetHidGuid(&hidGuid);

	HDEVINFO hDevInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (hDevInfo == INVALID_HANDLE_VALUE)
		return false;

	SP_DEVICE_INTERFACE_DATA ifData;
	ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &hidGuid, i, &ifData); i++)
	{
		// Get required buffer size
		DWORD requiredSize = 0;
		SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, nullptr, 0, &requiredSize, nullptr);
		if (requiredSize == 0) continue;

		BYTE *detailBuf = new BYTE[requiredSize];
		SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)detailBuf;
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (!SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, detail, requiredSize, nullptr, nullptr))
		{
			delete[] detailBuf;
			continue;
		}

		// First open WITHOUT overlapped flag just to check VID/PID and caps
		HANDLE hCheck = CreateFileW(detail->DevicePath,
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, 0, nullptr);

		if (hCheck == INVALID_HANDLE_VALUE)
		{
			delete[] detailBuf;
			continue;
		}

		HIDD_ATTRIBUTES attrs;
		attrs.Size = sizeof(attrs);
		if (!HidD_GetAttributes(hCheck, &attrs) || attrs.VendorID != SONY_VID)
		{
			CloseHandle(hCheck);
			delete[] detailBuf;
			continue;
		}

		bool isDS4 = (attrs.ProductID == DS4_V1_PID || attrs.ProductID == DS4_V2_PID);
		bool isDS = (attrs.ProductID == DUALSENSE_PID);

		if (!isDS4 && !isDS)
		{
			CloseHandle(hCheck);
			delete[] detailBuf;
			continue;
		}

		// Check HID usage page — we only want the gamepad interface
		// (DS4 exposes multiple HID collections: gamepad, motion, audio)
		PHIDP_PREPARSED_DATA preparsed = nullptr;
		bool isGamepad = false;
		USHORT inputReportLen = 0;

		if (HidD_GetPreparsedData(hCheck, &preparsed))
		{
			HIDP_CAPS caps;
			if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
			{
				// Usage Page 0x01 = Generic Desktop, Usage 0x05 = Game Pad, 0x04 = Joystick
				if (caps.UsagePage == 0x01 && (caps.Usage == 0x05 || caps.Usage == 0x04))
				{
					isGamepad = true;
					inputReportLen = caps.InputReportByteLength;
				}
				app.DebugPrintf("DS4HID: Found Sony device PID=%04X UsagePage=%04X Usage=%04X ReportLen=%d\n",
					attrs.ProductID, caps.UsagePage, caps.Usage, caps.InputReportByteLength);
			}
			HidD_FreePreparsedData(preparsed);
		}

		CloseHandle(hCheck);

		if (!isGamepad)
		{
			delete[] detailBuf;
			continue;
		}

		// Now re-open WITH FILE_FLAG_OVERLAPPED for async reads in Poll()
		HANDLE hDev = CreateFileW(detail->DevicePath,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

		if (hDev == INVALID_HANDLE_VALUE)
		{
			// Fall back to read-only with overlapped
			hDev = CreateFileW(detail->DevicePath,
				GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		}

		if (hDev == INVALID_HANDLE_VALUE)
		{
			app.DebugPrintf("DS4HID: Found gamepad but CreateFile failed (err=%u)\n", GetLastError());
			delete[] detailBuf;
			continue;
		}

		m_hDevice = hDev;
		m_isDualSense = isDS;
		m_isUSB = (inputReportLen <= 64);

		// Create persistent event for overlapped reads
		m_hReadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

		// Flush any stale reports queued by the driver
		HidD_FlushQueue(m_hDevice);

		app.DebugPrintf("DS4HID: Opened %s controller (%s, VID=%04X PID=%04X, ReportLen=%d)\n",
			isDS ? "DualSense" : "DS4",
			m_isUSB ? "USB" : "Bluetooth",
			attrs.VendorID, attrs.ProductID, inputReportLen);

		delete[] detailBuf;
		SetupDiDestroyDeviceInfoList(hDevInfo);
		return true;
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);
	return false;
}

void DS4HIDInput::Close()
{
	if (m_hDevice != INVALID_HANDLE_VALUE)
	{
		CancelIo(m_hDevice);
		CloseHandle(m_hDevice);
		m_hDevice = INVALID_HANDLE_VALUE;
	}
	if (m_hReadEvent != NULL)
	{
		CloseHandle(m_hReadEvent);
		m_hReadEvent = NULL;
	}
	app.DebugPrintf("DS4HID: Device closed\n");
}

// Clamp a signed 16-bit-range value
static SHORT ClampShort(int val)
{
	if (val > 32767) return 32767;
	if (val < -32768) return -32768;
	return (SHORT)val;
}

// Convert DS4 0-255 stick axis to XInput -32768..32767 with deadzone
static SHORT DS4StickToXInput(BYTE raw, int deadzone)
{
	// DS4: 0=left/up, 128=center, 255=right/down
	int centered = (int)raw - 128;

	// Apply deadzone
	if (abs(centered) < deadzone)
		return 0;

	// Scale to -32768..32767
	if (centered > 0)
		return ClampShort((int)(((centered - deadzone) / (127.0f - deadzone)) * 32767.0f));
	else
		return ClampShort((int)(((centered + deadzone) / (127.0f - deadzone)) * -32768.0f));
}

void DS4HIDInput::ParseDS4Report(const BYTE *report, int reportLen)
{
	// DS4 report formats:
	//
	// Report ID 0x01 (USB or BT short mode, 10-64 bytes):
	//   [0]=ReportID, [1]=LX, [2]=LY, [3]=RX, [4]=RY,
	//   [5]=DPad+Face, [6]=Shoulders, [7]=PS+Touch, [8]=L2, [9]=R2
	//
	// Report ID 0x11 (BT extended mode, 78 bytes):
	//   [0]=0x11, [1]=protocol, [2]=tag, [3]=LX, [4]=LY, [5]=RX, [6]=RY,
	//   [7]=DPad+Face, [8]=Shoulders, [9]=PS+Touch, [10]=L2, [11]=R2

	const BYTE *d = nullptr;

	if (report[0] == 0x11 && reportLen >= 14)
	{
		// BT extended report: gamepad data starts at byte 3
		d = report + 3;
	}
	else if (report[0] == 0x01 && reportLen >= 10)
	{
		// USB report OR BT short report: gamepad data starts at byte 1
		d = report + 1;
	}
	else
		return; // Unknown report format

	BYTE lx = d[0]; // Left stick X
	BYTE ly = d[1]; // Left stick Y
	BYTE rx = d[2]; // Right stick X
	BYTE ry = d[3]; // Right stick Y

	BYTE buttons1 = d[4]; // D-pad + face buttons
	BYTE buttons2 = d[5]; // Shoulder/trigger/stick buttons
	BYTE buttons3 = d[6]; // PS + touchpad

	BYTE l2 = d[7]; // L2 analog
	BYTE r2 = d[8]; // R2 analog

	// Build XInput button mask
	WORD xButtons = 0;

	// D-Pad (low nibble of buttons1)
	BYTE dpad = buttons1 & 0x0F;
	switch (dpad)
	{
	case 0: xButtons |= XINPUT_GAMEPAD_DPAD_UP; break;
	case 1: xButtons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT; break;
	case 2: xButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; break;
	case 3: xButtons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT; break;
	case 4: xButtons |= XINPUT_GAMEPAD_DPAD_DOWN; break;
	case 5: xButtons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT; break;
	case 6: xButtons |= XINPUT_GAMEPAD_DPAD_LEFT; break;
	case 7: xButtons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT; break;
	case 8: break; // Released
	}

	// Face buttons (high nibble of buttons1)
	// DS4 Cross = XInput A, Circle = B, Square = X, Triangle = Y
	if (buttons1 & 0x20) xButtons |= XINPUT_GAMEPAD_A;         // Cross
	if (buttons1 & 0x40) xButtons |= XINPUT_GAMEPAD_B;         // Circle
	if (buttons1 & 0x10) xButtons |= XINPUT_GAMEPAD_X;         // Square
	if (buttons1 & 0x80) xButtons |= XINPUT_GAMEPAD_Y;         // Triangle

	// Shoulder and stick buttons
	if (buttons2 & 0x01) xButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;  // L1
	if (buttons2 & 0x02) xButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER; // R1
	// L2/R2 digital bits (0x04, 0x08) are handled via analog triggers below
	if (buttons2 & 0x10) xButtons |= XINPUT_GAMEPAD_BACK;           // Share -> Back
	if (buttons2 & 0x20) xButtons |= XINPUT_GAMEPAD_START;          // Options -> Start
	if (buttons2 & 0x40) xButtons |= XINPUT_GAMEPAD_LEFT_THUMB;     // L3
	if (buttons2 & 0x80) xButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;    // R3

	// PS button -> Guide (not actually XINPUT_GAMEPAD_ constant, but some games read bit 0x0400)
	// Touchpad click -> no standard XInput mapping (we skip it)

	// Fill XINPUT_STATE
	m_state.Gamepad.wButtons = xButtons;

	// Sticks — apply a small deadzone (10 out of 128)
	m_state.Gamepad.sThumbLX = DS4StickToXInput(lx, 10);
	m_state.Gamepad.sThumbLY = ClampShort(-((int)DS4StickToXInput(ly, 10))); // Invert Y (DS4: down=positive, XInput: up=positive)
	m_state.Gamepad.sThumbRX = DS4StickToXInput(rx, 10);
	m_state.Gamepad.sThumbRY = ClampShort(-((int)DS4StickToXInput(ry, 10))); // Invert Y

	// Triggers (already 0-255, same range as XInput)
	m_state.Gamepad.bLeftTrigger = l2;
	m_state.Gamepad.bRightTrigger = r2;

	m_packetNumber++;
	m_state.dwPacketNumber = m_packetNumber;
}

void DS4HIDInput::ParseDualSenseReport(const BYTE *report, int reportLen)
{
	// DualSense report formats:
	//
	// Report ID 0x01 (USB or BT short, 10-64 bytes):
	//   [0]=ReportID, [1]=LX, [2]=LY, [3]=RX, [4]=RY,
	//   [5]=R2, [6]=L2, [7]=counter, [8]=DPad+Face, [9]=Shoulders, [10]=PS+Touch+Mute
	//
	// Report ID 0x31 (BT extended, 78 bytes):
	//   [0]=0x31, [1]=tag, [2]=LX, [3]=LY, [4]=RX, [5]=RY,
	//   [6]=R2, [7]=L2, [8]=counter, [9]=DPad+Face, [10]=Shoulders, [11]=PS+Touch+Mute

	const BYTE *d = nullptr;

	if (report[0] == 0x31 && reportLen >= 12)
	{
		// BT extended report: gamepad data starts at byte 2
		d = report + 2;
	}
	else if (report[0] == 0x01 && reportLen >= 11)
	{
		// USB report OR BT short report: gamepad data starts at byte 1
		d = report + 1;
	}
	else
		return;


	BYTE lx = d[0];
	BYTE ly = d[1];
	BYTE rx = d[2];
	BYTE ry = d[3];
	BYTE r2 = d[4]; // Note: DualSense swaps trigger order vs DS4
	BYTE l2 = d[5];

	BYTE buttons1 = d[7]; // D-pad + face
	BYTE buttons2 = d[8]; // Shoulder/click
	BYTE buttons3 = d[9]; // PS/touchpad/mute

	WORD xButtons = 0;

	BYTE dpad = buttons1 & 0x0F;
	switch (dpad)
	{
	case 0: xButtons |= XINPUT_GAMEPAD_DPAD_UP; break;
	case 1: xButtons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT; break;
	case 2: xButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; break;
	case 3: xButtons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT; break;
	case 4: xButtons |= XINPUT_GAMEPAD_DPAD_DOWN; break;
	case 5: xButtons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT; break;
	case 6: xButtons |= XINPUT_GAMEPAD_DPAD_LEFT; break;
	case 7: xButtons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT; break;
	case 8: break;
	}

	if (buttons1 & 0x20) xButtons |= XINPUT_GAMEPAD_A;         // Cross
	if (buttons1 & 0x40) xButtons |= XINPUT_GAMEPAD_B;         // Circle
	if (buttons1 & 0x10) xButtons |= XINPUT_GAMEPAD_X;         // Square
	if (buttons1 & 0x80) xButtons |= XINPUT_GAMEPAD_Y;         // Triangle

	if (buttons2 & 0x01) xButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
	if (buttons2 & 0x02) xButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
	if (buttons2 & 0x10) xButtons |= XINPUT_GAMEPAD_BACK;      // Create -> Back
	if (buttons2 & 0x20) xButtons |= XINPUT_GAMEPAD_START;     // Menu -> Start
	if (buttons2 & 0x40) xButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
	if (buttons2 & 0x80) xButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

	m_state.Gamepad.wButtons = xButtons;
	m_state.Gamepad.sThumbLX = DS4StickToXInput(lx, 10);
	m_state.Gamepad.sThumbLY = ClampShort(-((int)DS4StickToXInput(ly, 10)));
	m_state.Gamepad.sThumbRX = DS4StickToXInput(rx, 10);
	m_state.Gamepad.sThumbRY = ClampShort(-((int)DS4StickToXInput(ry, 10)));
	m_state.Gamepad.bLeftTrigger = l2;
	m_state.Gamepad.bRightTrigger = r2;

	m_packetNumber++;
	m_state.dwPacketNumber = m_packetNumber;
}

void DS4HIDInput::Poll()
{
	if (m_hDevice == INVALID_HANDLE_VALUE || m_hReadEvent == NULL)
		return;

	// Read the latest report. Use a large buffer to handle both USB and BT sizes.
	BYTE reportBuf[256];
	memset(reportBuf, 0, sizeof(reportBuf));
	DWORD bytesRead = 0;

	// Use overlapped I/O with persistent event handle
	OVERLAPPED ov;
	memset(&ov, 0, sizeof(ov));
	ov.hEvent = m_hReadEvent;
	ResetEvent(m_hReadEvent);

	BOOL readOk = ReadFile(m_hDevice, reportBuf, sizeof(reportBuf), nullptr, &ov);
	if (!readOk && GetLastError() == ERROR_IO_PENDING)
	{
		// Wait up to 8ms for a report
		DWORD waitResult = WaitForSingleObject(m_hReadEvent, 8);
		if (waitResult == WAIT_OBJECT_0)
		{
			GetOverlappedResult(m_hDevice, &ov, &bytesRead, FALSE);
		}
		else
		{
			// Timeout — cancel the read and use previous state
			CancelIo(m_hDevice);
			GetOverlappedResult(m_hDevice, &ov, &bytesRead, TRUE);
			return;
		}
	}
	else if (readOk)
	{
		GetOverlappedResult(m_hDevice, &ov, &bytesRead, FALSE);
	}
	else
	{
		// Read failed — device may have been disconnected
		DWORD err = GetLastError();
		app.DebugPrintf("DS4HID: ReadFile failed (error %u)\n", err);
		if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_INVALID_HANDLE ||
			err == ERROR_GEN_FAILURE)
		{
			app.DebugPrintf("DS4HID: Device disconnected\n");
			Close();
		}
		return;
	}

	if (bytesRead > 0)
	{
		// Log first few reports for debugging
		static int s_logCount = 0;
		if (s_logCount < 10)
		{
			app.DebugPrintf("DS4HID: Read %u bytes, reportID=0x%02X, data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				bytesRead, reportBuf[0],
				bytesRead > 1 ? reportBuf[1] : 0, bytesRead > 2 ? reportBuf[2] : 0,
				bytesRead > 3 ? reportBuf[3] : 0, bytesRead > 4 ? reportBuf[4] : 0,
				bytesRead > 5 ? reportBuf[5] : 0, bytesRead > 6 ? reportBuf[6] : 0,
				bytesRead > 7 ? reportBuf[7] : 0, bytesRead > 8 ? reportBuf[8] : 0,
				bytesRead > 9 ? reportBuf[9] : 0, bytesRead > 10 ? reportBuf[10] : 0);
			s_logCount++;
		}

		if (m_isDualSense)
			ParseDualSenseReport(reportBuf, (int)bytesRead);
		else
			ParseDS4Report(reportBuf, (int)bytesRead);
	}
}

DWORD DS4HIDInput::GetState(XINPUT_STATE *pState)
{
	if (m_hDevice == INVALID_HANDLE_VALUE)
		return ERROR_DEVICE_NOT_CONNECTED;

	if (pState)
		*pState = m_state;

	return ERROR_SUCCESS;
}

void DS4HIDInput::SetLightbar(BYTE r, BYTE g, BYTE b)
{
	if (m_hDevice == INVALID_HANDLE_VALUE || m_isDualSense)
		return; // Only DS4 lightbar is supported for now

	// DS4 USB output report (report ID 0x05, 32 bytes)
	BYTE report[32];
	memset(report, 0, sizeof(report));
	report[0] = 0x05; // Report ID
	report[1] = 0xFF; // Flags: motor + lightbar
	// report[4] = rumble right (small motor)
	// report[5] = rumble left (big motor)
	report[6] = r;
	report[7] = g;
	report[8] = b;

	DWORD written = 0;
	WriteFile(m_hDevice, report, sizeof(report), &written, nullptr);
}

#endif
