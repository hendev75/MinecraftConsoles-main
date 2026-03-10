#include "stdafx.h"
#ifdef _WINDOWS64

#include "PS4TooltipRenderer.h"
#include "KeyboardMouseInput.h"
#include "../Minecraft.h"
#include "../Textures.h"
#include "../Font.h"
#include "../BufferedImage.h"
#include "../Gui.h"
#include "../Tesselator.h"

PS4TooltipRenderer g_PS4Tooltips;

// Map each tooltip button to its PS4 icon PNG filename.
static const wchar_t *s_ps4IconFiles[eToolTipNumButtons] =
{
	L"PlayStation_button_X.png",            // eToolTipButtonA  -> Cross
	L"PlayStation_button_C.png",            // eToolTipButtonB  -> Circle
	L"PlayStation_button_S.png",            // eToolTipButtonX  -> Square
	L"PlayStation_button_T.png",            // eToolTipButtonY  -> Triangle
	L"PlayStation_4_button_L2.png",         // eToolTipButtonLT -> L2
	L"PlayStation_4_button_R2.png",         // eToolTipButtonRT -> R2
	L"PlayStation_4_button_L1.png",         // eToolTipButtonLB -> L1
	L"PlayStation_4_button_R1.png",         // eToolTipButtonRB -> R1
	L"PlayStation_button_L3.png",           // eToolTipButtonLS -> L3
	L"PlayStation_button_R3.png",           // eToolTipButtonRS -> R3
	L"PlayStation_4_Options_button.png",    // eToolTipButtonBack -> Options
};

PS4TooltipRenderer::PS4TooltipRenderer()
	: m_initialized(false)
{
	for (int i = 0; i < eToolTipNumButtons; i++)
		m_iconTextureIds[i] = -1;
}

PS4TooltipRenderer::~PS4TooltipRenderer()
{
}

void PS4TooltipRenderer::Init()
{
	if (m_initialized) return;

	Minecraft *mc = Minecraft::GetInstance();
	if (!mc || !mc->textures) return;

	bool allLoaded = true;
	for (int i = 0; i < eToolTipNumButtons; i++)
	{
		// Build file path relative to the working directory
		wstring path = wstring(L"Common\\Media\\Graphics\\PS4_icons\\") + s_ps4IconFiles[i];

		// Read the PNG file into a memory buffer
		FILE *fp = nullptr;
		char narrowPath[512];
		wcstombs(narrowPath, path.c_str(), sizeof(narrowPath));
		narrowPath[sizeof(narrowPath) - 1] = '\0';
		fp = fopen(narrowPath, "rb");
		if (!fp)
		{
			app.DebugPrintf("PS4TooltipRenderer: Could not open %s\n", narrowPath);
			allLoaded = false;
			continue;
		}

		fseek(fp, 0, SEEK_END);
		DWORD fileSize = static_cast<DWORD>(ftell(fp));
		fseek(fp, 0, SEEK_SET);

		BYTE *fileData = new BYTE[fileSize];
		fread(fileData, 1, fileSize, fp);
		fclose(fp);

		BufferedImage *img = new BufferedImage(fileData, fileSize);
		delete[] fileData;

		if (img->getData() != nullptr)
		{
			int texId = mc->textures->getTexture(img, C4JRender::TEXTURE_FORMAT_RxGyBzAw, false);
			m_iconTextureIds[i] = texId;
		}
		else
		{
			app.DebugPrintf("PS4TooltipRenderer: Failed to decode icon %ls\n", s_ps4IconFiles[i]);
			allLoaded = false;
			delete img;
		}
	}

	m_initialized = true;
	app.DebugPrintf("PS4TooltipRenderer: Initialized (%s)\n", allLoaded ? "all icons loaded" : "some icons missing");
}

void PS4TooltipRenderer::RenderIcon(int textureId, int x, int y, int size)
{
	if (textureId < 0) return;

	Minecraft *mc = Minecraft::GetInstance();
	if (!mc || !mc->textures) return;

	mc->textures->bind(textureId);

	Tesselator *t = Tesselator::getInstance();
	t->begin();

	float fx = static_cast<float>(x);
	float fy = static_cast<float>(y);
	float fs = static_cast<float>(size);

	// Draw a full-texture quad (UV 0-1)
	t->vertexUV(fx,      fy + fs, 0.0f, 0.0f, 1.0f);
	t->vertexUV(fx + fs, fy + fs, 0.0f, 1.0f, 1.0f);
	t->vertexUV(fx + fs, fy,      0.0f, 1.0f, 0.0f);
	t->vertexUV(fx,      fy,      0.0f, 0.0f, 0.0f);
	t->end();
}

void PS4TooltipRenderer::Render(int screenWidth, int screenHeight,
	const bool tooltipShow[eToolTipNumButtons],
	const wstring tooltipText[eToolTipNumButtons],
	float opacity)
{
	if (!m_initialized) Init();
	if (!m_initialized) return;

	Minecraft *mc = Minecraft::GetInstance();
	if (!mc || !mc->font) return;

	// Count visible tooltips
	int visibleCount = 0;
	for (int i = 0; i < eToolTipNumButtons; i++)
	{
		if (tooltipShow[i]) visibleCount++;
	}
	if (visibleCount == 0) return;

	// Layout parameters
	const int iconSize = 20;    // Icon size in GUI-space pixels
	const int textGap = 2;     // Gap between icon and text
	const int buttonGap = 12;  // Gap between tooltip groups
	const int bottomMargin = 4; // Margin from bottom of screen

	// The screen dimensions here are in GUI-space (already scaled).
	// Calculate total width of all visible tooltips to center them.
	int totalWidth = 0;
	for (int i = 0; i < eToolTipNumButtons; i++)
	{
		if (!tooltipShow[i]) continue;
		int textWidth = 0;
		if (!tooltipText[i].empty())
			textWidth = mc->font->width(tooltipText[i]);
		totalWidth += iconSize + textGap + textWidth + buttonGap;
	}
	totalWidth -= buttonGap; // Remove trailing gap

	int startX = (screenWidth - totalWidth) / 2;
	int yPos = screenHeight - bottomMargin - iconSize;

	// Enable blending for icon transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Set opacity
	glColor4f(1.0f, 1.0f, 1.0f, opacity);

	int xPos = startX;
	for (int i = 0; i < eToolTipNumButtons; i++)
	{
		if (!tooltipShow[i]) continue;

		// Draw the PS4 button icon
		RenderIcon(m_iconTextureIds[i], xPos, yPos, iconSize);

		// Draw the label text next to the icon
		if (!tooltipText[i].empty())
		{
			int textX = xPos + iconSize + textGap;
			int textY = yPos + (iconSize - 8) / 2; // Vertically center text (8 = font height)
			// Use a slightly transparent white with shadow
			int alpha = static_cast<int>(opacity * 255.0f);
			int color = (alpha << 24) | 0xFFFFFF;
			mc->font->drawShadow(tooltipText[i], textX, textY, color);

			xPos += iconSize + textGap + mc->font->width(tooltipText[i]) + buttonGap;
		}
		else
		{
			xPos += iconSize + buttonGap;
		}
	}

	// Reset color
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

#endif
