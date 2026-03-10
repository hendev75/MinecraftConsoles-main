#include "stdafx.h"
#ifdef _WINDOWS64

#include "ControllerTypeDetector.h"
#include <windows.h>
#include <tlhelp32.h>

// Sony vendor ID
static const DWORD SONY_VID = 0x054C;

// Known Sony controller product IDs
static const DWORD DS3_PID       = 0x0268;  // DualShock 3
static const DWORD DS4_V1_PID    = 0x05C4;  // DualShock 4 v1
static const DWORD DS4_V2_PID    = 0x09CC;  // DualShock 4 v2
static const DWORD DUALSENSE_PID = 0x0CE6;  // DualSense (PS5)

static bool IsSonyControllerPid(DWORD pid)
{
	return pid == DS3_PID || pid == DS4_V1_PID || pid == DS4_V2_PID || pid == DUALSENSE_PID;
}

// Case-insensitive search for VID_xxxx and PID_xxxx in a string.
// Handles upper, lower, and mixed case.
static bool ParseVidPid(const wchar_t *path, DWORD &vid, DWORD &pid)
{
	vid = 0;
	pid = 0;
	if (!path) return false;

	// Make a lowercase copy for case-insensitive matching
	size_t len = wcslen(path);
	if (len == 0 || len > 4096) return false;

	wchar_t *lower = new wchar_t[len + 1];
	for (size_t i = 0; i <= len; i++)
		lower[i] = towlower(path[i]);

	const wchar_t *pVid = wcsstr(lower, L"vid_");
	const wchar_t *pPid = wcsstr(lower, L"pid_");

	if (pVid && pPid)
	{
		// Use offsets from the original string to parse the hex values
		size_t vidOff = (pVid - lower) + 4;
		size_t pidOff = (pPid - lower) + 4;
		vid = wcstoul(path + vidOff, nullptr, 16);
		pid = wcstoul(path + pidOff, nullptr, 16);
	}

	delete[] lower;
	return vid != 0;
}

// ============================================================================
// Method 1: Windows Registry scan.
// The PnP manager always writes device info to the registry under
// HKLM\SYSTEM\CurrentControlSet\Enum\USB and \HID.
// This works regardless of HidHide, HidGuardian, or ViGEmBus because
// the registry entries are maintained by the USB hub driver and PnP manager
// at a level below any HID filter drivers.
// ============================================================================
static bool ScanRegistryPath(const wchar_t *basePath)
{
	HKEY hBaseKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, basePath, 0, KEY_READ, &hBaseKey) != ERROR_SUCCESS)
		return false;

	wchar_t subKeyName[512];
	DWORD subKeyLen;
	bool found = false;

	for (DWORD i = 0; !found; i++)
	{
		subKeyLen = 512;
		if (RegEnumKeyExW(hBaseKey, i, subKeyName, &subKeyLen,
			nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
			break;

		DWORD vid = 0, pid = 0;
		if (ParseVidPid(subKeyName, vid, pid) && vid == SONY_VID && IsSonyControllerPid(pid))
		{
			// Found a Sony controller key — verify it has at least one device instance
			HKEY hDevKey;
			if (RegOpenKeyExW(hBaseKey, subKeyName, 0, KEY_READ, &hDevKey) == ERROR_SUCCESS)
			{
				wchar_t instName[256];
				DWORD instLen = 256;
				if (RegEnumKeyExW(hDevKey, 0, instName, &instLen,
					nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
				{
					found = true;
					app.DebugPrintf("PS4Detect: Found Sony controller in registry: %ls\\%ls\\%ls\n",
						basePath, subKeyName, instName);
				}
				RegCloseKey(hDevKey);
			}
		}
	}

	RegCloseKey(hBaseKey);
	return found;
}

static bool DetectViaRegistry()
{
	// Check USB devices (wired connections)
	if (ScanRegistryPath(L"SYSTEM\\CurrentControlSet\\Enum\\USB"))
		return true;

	// Check HID devices (covers both USB and Bluetooth HID, plus ViGEmBus virtual devices)
	if (ScanRegistryPath(L"SYSTEM\\CurrentControlSet\\Enum\\HID"))
		return true;

	return false;
}

// ============================================================================
// Method 2: RawInput device enumeration.
// Fast check that works when the HID device is visible (no HidHide).
// ============================================================================
static bool DetectViaRawInput()
{
	UINT numDevices = 0;
	if (GetRawInputDeviceList(nullptr, &numDevices, sizeof(RAWINPUTDEVICELIST)) != 0)
		return false;
	if (numDevices == 0)
		return false;

	RAWINPUTDEVICELIST *deviceList = new RAWINPUTDEVICELIST[numDevices];
	UINT retrieved = GetRawInputDeviceList(deviceList, &numDevices, sizeof(RAWINPUTDEVICELIST));
	if (retrieved == (UINT)-1)
	{
		delete[] deviceList;
		return false;
	}

	bool found = false;
	for (UINT i = 0; i < retrieved && !found; i++)
	{
		if (deviceList[i].dwType != RIM_TYPEHID)
			continue;

		UINT nameSize = 0;
		if (GetRawInputDeviceInfoW(deviceList[i].hDevice, RIDI_DEVICENAME, nullptr, &nameSize) != 0)
			continue;
		if (nameSize == 0 || nameSize > 1024)
			continue;

		wchar_t *name = new wchar_t[nameSize + 1];
		if (GetRawInputDeviceInfoW(deviceList[i].hDevice, RIDI_DEVICENAME, name, &nameSize) > 0)
		{
			name[nameSize] = L'\0';
			DWORD vid = 0, pid = 0;
			if (ParseVidPid(name, vid, pid) && vid == SONY_VID && IsSonyControllerPid(pid))
			{
				found = true;
				app.DebugPrintf("PS4Detect: Found Sony controller via RawInput: %ls\n", name);
			}
		}
		delete[] name;
	}

	delete[] deviceList;
	return found;
}

// ============================================================================
// Method 3: Process detection.
// Check if DS4Windows or other PlayStation controller bridge software is
// running. Uses partial name matching to catch forks and renamed executables.
// ============================================================================
static bool DetectViaProcess()
{
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnap == INVALID_HANDLE_VALUE)
		return false;

	PROCESSENTRY32W pe;
	pe.dwSize = sizeof(pe);

	// Convert process name to lowercase for partial matching
	wchar_t lowerName[MAX_PATH];

	bool found = false;
	if (Process32FirstW(hSnap, &pe))
	{
		do
		{
			// Lowercase the process name
			for (int c = 0; c < MAX_PATH; c++)
			{
				lowerName[c] = towlower(pe.szExeFile[c]);
				if (pe.szExeFile[c] == L'\0') break;
			}
			lowerName[MAX_PATH - 1] = L'\0';

			// Match known PlayStation controller bridge software
			// DS4Windows (all forks: Jays2Kings, Ryochan7, etc.)
			if (wcsstr(lowerName, L"ds4windows") != nullptr ||
				wcsstr(lowerName, L"ds4w") != nullptr ||
				// InputMapper
				wcsstr(lowerName, L"inputmapper") != nullptr ||
				// DualSenseX / DualSense tools
				wcsstr(lowerName, L"dualsense") != nullptr ||
				// Steam (Steam Input can bridge DS4)
				// Commented out: too broad, many Steam users use Xbox controllers
				// wcsstr(lowerName, L"steam.exe") != nullptr ||
				// BetterDS4
				wcsstr(lowerName, L"betterds4") != nullptr ||
				// reWASD PlayStation profiles
				wcsstr(lowerName, L"ds4tool") != nullptr ||
				wcsstr(lowerName, L"ds4drv") != nullptr ||
				// ScpToolkit (SCP Server for DS3/DS4)
				wcsstr(lowerName, L"scpserver") != nullptr ||
				wcsstr(lowerName, L"scptoolkit") != nullptr)
			{
				found = true;
				app.DebugPrintf("PS4Detect: Found PS controller software: %ls\n", pe.szExeFile);
				break;
			}
		} while (Process32NextW(hSnap, &pe));
	}

	CloseHandle(hSnap);
	return found;
}

// ============================================================================
// Main detection entry point.
// Tries multiple methods and caches the last result with debug output.
// ============================================================================
EControllerType DetectControllerType()
{
	static bool s_firstRun = true;

	// Method 1: Registry — most reliable, works through HidHide/ViGEmBus
	if (DetectViaRegistry())
	{
		if (s_firstRun) app.DebugPrintf("PS4Detect: Result = PLAYSTATION (via Registry)\n");
		s_firstRun = false;
		return CONTROLLER_TYPE_PLAYSTATION;
	}

	// Method 2: RawInput — fast, works when device is not hidden
	if (DetectViaRawInput())
	{
		if (s_firstRun) app.DebugPrintf("PS4Detect: Result = PLAYSTATION (via RawInput)\n");
		s_firstRun = false;
		return CONTROLLER_TYPE_PLAYSTATION;
	}

	// Method 3: Process detection — catches DS4Windows and similar software
	if (DetectViaProcess())
	{
		if (s_firstRun) app.DebugPrintf("PS4Detect: Result = PLAYSTATION (via Process)\n");
		s_firstRun = false;
		return CONTROLLER_TYPE_PLAYSTATION;
	}

	if (s_firstRun) app.DebugPrintf("PS4Detect: Result = XBOX (no PlayStation controller or software found)\n");
	s_firstRun = false;
	return CONTROLLER_TYPE_XBOX;
}

#endif
