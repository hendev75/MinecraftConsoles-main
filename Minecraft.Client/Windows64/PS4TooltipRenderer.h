#pragma once

#ifdef _WINDOWS64

#include "../Common/UI/UIEnums.h"
#include <string>
using std::wstring;

// Renders PlayStation-style button icons as an overlay when a DualShock/DualSense
// controller is detected.  Each visible tooltip gets a PS4 icon drawn at the
// bottom of the screen in place of the Flash SWF Xbox-style button prompts.
class PS4TooltipRenderer
{
public:
	PS4TooltipRenderer();
	~PS4TooltipRenderer();

	// Load the PS4 PNG icons from Common/Media/Graphics/PS4_icons/.
	// Call once after the texture system is ready.
	void Init();
	bool IsInitialized() const { return m_initialized; }

	// Draw PS4 tooltips for the currently active button prompts.
	// tooltipShow/tooltipText come from UIComponent_Tooltips m_tooltipValues.
	void Render(int screenWidth, int screenHeight,
		const bool tooltipShow[eToolTipNumButtons],
		const wstring tooltipText[eToolTipNumButtons],
		float opacity);

private:
	void RenderIcon(int textureId, int x, int y, int size);

	bool m_initialized;
	int m_iconTextureIds[eToolTipNumButtons]; // GL texture IDs for each button icon
};

extern PS4TooltipRenderer g_PS4Tooltips;

#endif
