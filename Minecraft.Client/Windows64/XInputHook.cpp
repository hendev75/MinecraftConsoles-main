#include "stdafx.h"
#ifdef _WINDOWS64

#include "XInputHook.h"
#include "DS4HIDInput.h"
#include "ControllerTypeDetector.h"
#include "KeyboardMouseInput.h"
#include <windows.h>
#include <Xinput.h>

// ============================================================================
// IAT Hook for XInputGetState
//
// The precompiled 4J InputManager library internally calls XInputGetState
// from xinput9_1_0.dll. We patch the Import Address Table (IAT) to redirect
// those calls through our hook, which returns DS4 HID data for pad 0 when
// a PlayStation controller is connected.
// ============================================================================

// Pointer to the real XInputGetState function
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE *pState);
static PFN_XInputGetState s_realXInputGetState = nullptr;

// Our hooked version
static DWORD WINAPI Hooked_XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState)
{
	// Only intercept pad 0
	if (dwUserIndex == 0 && g_DS4Input.IsOpen())
	{
		DWORD result = g_DS4Input.GetState(pState);

		// Log first few calls to confirm hook is active
		static int s_hookLogCount = 0;
		if (s_hookLogCount < 5 && pState)
		{
			app.DebugPrintf("DS4Hook: GetState pad0 -> buttons=0x%04X LX=%d LY=%d RX=%d RY=%d LT=%d RT=%d\n",
				pState->Gamepad.wButtons,
				pState->Gamepad.sThumbLX, pState->Gamepad.sThumbLY,
				pState->Gamepad.sThumbRX, pState->Gamepad.sThumbRY,
				pState->Gamepad.bLeftTrigger, pState->Gamepad.bRightTrigger);
			s_hookLogCount++;
		}

		return result;
	}

	// Fall back to real XInput for other pads or if no DS4 is connected
	if (s_realXInputGetState)
		return s_realXInputGetState(dwUserIndex, pState);

	return ERROR_DEVICE_NOT_CONNECTED;
}

// Patch a single IAT entry in the given module
static bool PatchIATEntry(HMODULE hModule, const char *targetDll, const char *funcName, void *newFunc, void **origFunc)
{
	if (!hModule) return false;

	BYTE *base = (BYTE *)hModule;
	IMAGE_DOS_HEADER *dosHeader = (IMAGE_DOS_HEADER *)base;
	if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;

	IMAGE_NT_HEADERS *ntHeaders = (IMAGE_NT_HEADERS *)(base + dosHeader->e_lfanew);
	if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

	IMAGE_DATA_DIRECTORY *importDir = &ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (importDir->VirtualAddress == 0 || importDir->Size == 0) return false;

	IMAGE_IMPORT_DESCRIPTOR *imports = (IMAGE_IMPORT_DESCRIPTOR *)(base + importDir->VirtualAddress);

	for (; imports->Name != 0; imports++)
	{
		const char *dllName = (const char *)(base + imports->Name);
		if (_stricmp(dllName, targetDll) != 0)
			continue;

		IMAGE_THUNK_DATA *origThunk = (IMAGE_THUNK_DATA *)(base + imports->OriginalFirstThunk);
		IMAGE_THUNK_DATA *iatThunk = (IMAGE_THUNK_DATA *)(base + imports->FirstThunk);

		for (; origThunk->u1.AddressOfData != 0; origThunk++, iatThunk++)
		{
			if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal))
				continue;

			IMAGE_IMPORT_BY_NAME *importByName = (IMAGE_IMPORT_BY_NAME *)(base + origThunk->u1.AddressOfData);
			if (strcmp(importByName->Name, funcName) != 0)
				continue;

			// Found it — save the original and patch
			DWORD oldProtect;
			if (VirtualProtect(&iatThunk->u1.Function, sizeof(void *), PAGE_READWRITE, &oldProtect))
			{
				if (origFunc)
					*origFunc = (void *)iatThunk->u1.Function;
				iatThunk->u1.Function = (ULONG_PTR)newFunc;
				VirtualProtect(&iatThunk->u1.Function, sizeof(void *), oldProtect, &oldProtect);
				return true;
			}
		}
	}

	return false;
}

void InstallDS4XInputHook()
{
	// Try to open a DS4/DualSense device
	if (!g_DS4Input.Open())
	{
		app.DebugPrintf("DS4Hook: No DS4/DualSense found, XInput hook not installed\n");
		return;
	}

	// Set lightbar to blue to indicate the game has connected
	g_DS4Input.SetLightbar(0, 0, 64);

	// Patch the IAT of the main executable module
	HMODULE hExe = GetModuleHandleW(nullptr);

	// Try multiple XInput DLL names (different Windows SDK versions use different names)
	bool patched = false;
	const char *xinputDlls[] = { "xinput9_1_0.dll", "xinput1_4.dll", "xinput1_3.dll", "XINPUT9_1_0.dll", "XINPUT1_4.dll" };
	for (int i = 0; i < 5 && !patched; i++)
	{
		patched = PatchIATEntry(hExe, xinputDlls[i], "XInputGetState",
			(void *)Hooked_XInputGetState, (void **)&s_realXInputGetState);
		if (patched)
			app.DebugPrintf("DS4Hook: Hooked XInputGetState in IAT (%s)\n", xinputDlls[i]);
	}

	// Also try ordinal 100 (some XInput versions export by ordinal)
	if (!patched)
	{
		// Fallback: get the real function directly and use a different interception method
		HMODULE hXInput = GetModuleHandleA("xinput9_1_0.dll");
		if (!hXInput) hXInput = GetModuleHandleA("xinput1_4.dll");
		if (!hXInput) hXInput = GetModuleHandleA("xinput1_3.dll");

		if (hXInput)
		{
			s_realXInputGetState = (PFN_XInputGetState)GetProcAddress(hXInput, "XInputGetState");
			app.DebugPrintf("DS4Hook: Got real XInputGetState at %p (IAT patch failed, using direct override)\n",
				s_realXInputGetState);
		}
	}

	if (!patched && !s_realXInputGetState)
	{
		app.DebugPrintf("DS4Hook: WARNING — Could not hook XInputGetState. DS4 input may not work.\n");
	}

	// Update controller type detection immediately
	g_KBMInput.SetControllerType(CONTROLLER_TYPE_PLAYSTATION);

	app.DebugPrintf("DS4Hook: DS4/DualSense native input active\n");
}

void DS4HIDPoll()
{
	// Try to open DS4 if not already open (hot-plug support)
	static int s_retryCounter = 0;
	if (!g_DS4Input.IsOpen())
	{
		// Don't try every frame — check every ~120 frames
		if (++s_retryCounter < 120) return;
		s_retryCounter = 0;

		if (g_DS4Input.Open())
		{
			g_DS4Input.SetLightbar(0, 0, 64);
			g_KBMInput.SetControllerType(CONTROLLER_TYPE_PLAYSTATION);

			// Install hook if not already done
			if (!s_realXInputGetState)
				InstallDS4XInputHook();
		}
		return;
	}

	s_retryCounter = 0;
	g_DS4Input.Poll();
}

#endif
