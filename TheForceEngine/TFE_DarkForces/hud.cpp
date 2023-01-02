#include "hud.h"
#include "time.h"
#include "automap.h"
#include "config.h"
#include "gameMessage.h"
#include "mission.h"
#include "util.h"
#include "player.h"
#include "weapon.h"
#include <TFE_FileSystem/paths.h>
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>
#include <TFE_FrontEndUI/console.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Jedi/Renderer/screenDraw.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/screenDrawGPU.h>
#include <TFE_Jedi/Level/rfont.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/roffscreenBuffer.h>
#include <cstring>

#define TFE_CONVERT_CAPS 0
#if TFE_CONVERT_CAPS
#include <TFE_Asset/imageAsset.h>
#endif

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Constants
	///////////////////////////////////////////
	enum HudConstants
	{
		HUD_COLORS_START    = 24,
		HUD_COLORS_COUNT    = 8,
		HUD_MAX_PRIORITY    = 0,
		HUD_HIGH_PRIORITY   = 1,
		HUD_LOWEST_PRIORITY = 9,

		HUD_MSG_LONG_DUR  = 1165,	// 8 seconds
		HUD_MSG_SHORT_DUR =  436,	// 3 seconds
	};

	static const s32 c_hudVertAnimTable[] =
	{
		160, 170, 185, 200,
	};

	///////////////////////////////////////////
	// Internal State
	///////////////////////////////////////////
	static GameMessages s_hudMessages;
	static Font* s_hudFont;
	static char s_hudMessage[80];
	static s32 s_hudCurrentMsgId = 0;
	static s32 s_hudMsgPriority = HUD_LOWEST_PRIORITY;
	static Tick s_hudMsgExpireTick;

	static JBool s_screenDirtyLeft[4]  = { 0 };
	static JBool s_screenDirtyRight[4] = { 0 };
	static JBool s_basePaletteCopyMode = JFALSE;
	static JBool s_updateHudColors = JFALSE;

	static u8 s_tempBuffer[256 * 3];
	static u8 s_hudPalette[HUD_COLORS_COUNT * 3 + 10];
	static u8 s_hudWorkPalette[HUD_COLORS_COUNT * 3 + 10];
	static s32 s_hudColorCount = 8;
	static s32 s_hudPaletteIndex = 24;
	
	static TextureData* s_hudStatusL  = nullptr;
	static TextureData* s_hudStatusR  = nullptr;
	static TextureData* s_hudLightOn  = nullptr;
	static TextureData* s_hudLightOff = nullptr;
	static TextureData* s_hudCapLeft  = nullptr;
	static TextureData* s_hudCapRight = nullptr;
	static Font* s_hudMainAmmoFont    = nullptr;
	static Font* s_hudSuperAmmoFont   = nullptr;
	static Font* s_hudShieldFont      = nullptr;
	static Font* s_hudHealthFont      = nullptr;
	static OffScreenBuffer* s_cachedHudLeft = nullptr;
	static OffScreenBuffer* s_cachedHudRight = nullptr;

	static s32 s_rightHudVertTarget;
	static s32 s_rightHudVertAnim;
	static s32 s_rightHudShow;
	static s32 s_rightHudMove;
	static s32 s_leftHudVertTarget;
	static s32 s_leftHudVertAnim;
	static s32 s_leftHudShow;
	static s32 s_leftHudMove;
	static JBool s_forceHudPaletteUpdate = JFALSE;

	static s32 s_prevEnergy = 0;
	static s32 s_prevLifeCount = 0;
	static s32 s_prevShields = 0;
	static s32 s_prevHealth = 0;
	static s32 s_prevPrimaryAmmo = 0;
	static s32 s_prevSecondaryAmmo = 0;
	static JBool s_prevSuperchageHud = JFALSE;
	static JBool s_prevHeadlampActive = JFALSE;

	///////////////////////////////////////////
	// Shared State
	///////////////////////////////////////////
	fixed16_16 s_flashEffect = 0;
	fixed16_16 s_healthDamageFx = 0;
	fixed16_16 s_shieldDamageFx = 0;

	s32 s_secretsFound = 0;
	s32 s_secretsPercent = 0;
	JBool s_showData = JFALSE;

	///////////////////////////////////////////
	// Forward Declarations
	///////////////////////////////////////////
	GameMessage* hud_getMessage(GameMessages* messages, s32 msgId);
	TextureData* hud_loadTexture(const char* texFile);
	Font* hud_loadFont(const char* fontFile);
	void copyIntoPalette(u8* dst, u8* src, s32 count, s32 mode);
	void getCameraXZ(fixed16_16* x, fixed16_16* z);
	void displayHudMessage(Font* font, DrawRect* rect, s32 x, s32 y, char* msg, u8* framebuffer);
	void hud_drawString(OffScreenBuffer* elem, Font* font, s32 x0, s32 y0, const char* str);
#if TFE_CONVERT_CAPS
	void hud_convertCapsToBM();
#endif

	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void hud_sendTextMessage(s32 msgId)
	{
		GameMessage* msg = hud_getMessage(&s_hudMessages, msgId);
		// Only display the message if it is the same or lower priority than the current message.
		if (!msg || msg->priority > s_hudMsgPriority)
		{
			// Always write the message to the console
			if (msg && msg->text) { TFE_Console::addToHistory(msg->text); }
			return;
		}

		const char* msgText = msg->text;
		if (!msgText[0]) { return; }
		strCopyAndZero(s_hudMessage, msgText, 80);

		s_hudMsgExpireTick = s_curTick + ((msg->priority <= HUD_HIGH_PRIORITY) ? HUD_MSG_LONG_DUR : HUD_MSG_SHORT_DUR);
		s_hudCurrentMsgId  = msgId;
		s_hudMsgPriority   = msg->priority;

		// TFE specific
		TFE_Console::addToHistory(msgText);
	}

	void hud_sendTextMessage(const char* msg, s32 priority)
	{
		// Only display the message if it is the same or lower priority than the current message.
		if (!msg || priority > s_hudMsgPriority)
		{
			// Always write the message to the console
			if (msg) { TFE_Console::addToHistory(msg); }
			return;
		}
		strCopyAndZero(s_hudMessage, msg, 80);

		s_hudMsgExpireTick = s_curTick + ((priority <= HUD_HIGH_PRIORITY) ? HUD_MSG_LONG_DUR : HUD_MSG_SHORT_DUR);
		s_hudCurrentMsgId  = 0;
		s_hudMsgPriority   = priority;

		// TFE specific
		TFE_Console::addToHistory(msg);
	}

	void hud_clearMessage()
	{
		s_hudMessage[0] = 0;
		s_hudCurrentMsgId = 0;
		s_hudMsgPriority = HUD_LOWEST_PRIORITY;
		for (s32 i = 0; i < 4; i++)
		{
			s_screenDirtyLeft[i]  = JFALSE;
			s_screenDirtyRight[i] = JFALSE;
		}
	}

	void hud_addTexture(TextureInfoList& texList, TextureData* tex)
	{
		TextureInfo texInfo = {};
		texInfo.type = TEXINFO_DF_TEXTURE_DATA;
		texInfo.texData = tex;
		texList.push_back(texInfo);
	}

	void hud_addFont(TextureInfoList& texList, Font* font)
	{
		for (s32 i = font->minChar; i <= font->maxChar; i++)
		{
			hud_addTexture(texList, &font->glyphs[i - font->minChar]);
		}
	}

	bool hud_getTextures(TextureInfoList& texList, AssetPool pool)
	{
		// First load the fonts.
		hud_addFont(texList, s_hudFont);
		hud_addFont(texList, s_hudMainAmmoFont);
		hud_addFont(texList, s_hudSuperAmmoFont);
		hud_addFont(texList, s_hudShieldFont);
		hud_addFont(texList, s_hudHealthFont);
		return true;
	}

	void hud_loadGameMessages()
	{
		s_hudMessages.count = 0;
		s_hudMessages.msgList = nullptr;

		FilePath filePath;
		if (TFE_Paths::getFilePath("text.msg", &filePath))
		{
			parseMessageFile(&s_hudMessages, &filePath, 1);
		}
		s_hudMessage[0] = 0;
		s_hudFont = nullptr;
		if (TFE_Paths::getFilePath("glowing.fnt", &filePath))
		{
			s_hudFont = font_load(&filePath);
		}

		// TFE
		TFE_Jedi::renderer_addHudTextureCallback(hud_getTextures);
	}

	void hud_reset()
	{
		freeOffScreenBuffer(s_cachedHudLeft);
		freeOffScreenBuffer(s_cachedHudRight);
		s_cachedHudLeft = nullptr;
		s_cachedHudRight = nullptr;
	}
		
	void hud_loadGraphics()
	{
		s_hudStatusL       = hud_loadTexture("StatusLf.bm");
		s_hudStatusR       = hud_loadTexture("StatusRt.bm");
		s_hudLightOn       = hud_loadTexture("lighton.bm");
		s_hudLightOff      = hud_loadTexture("lightoff.bm");
		s_hudMainAmmoFont  = hud_loadFont("AmoNum.fnt");
		s_hudSuperAmmoFont = hud_loadFont("SuperWep.fnt");
		s_hudShieldFont    = hud_loadFont("ArmNum.fnt");
		s_hudHealthFont    = hud_loadFont("HelNum.fnt");

		// Create offscreen buffers for the HUD elements.
		s_cachedHudLeft  = createOffScreenBuffer(s_hudStatusL->width, s_hudStatusL->height, OBF_TRANS);
		s_cachedHudRight = createOffScreenBuffer(s_hudStatusR->width, s_hudStatusR->height, OBF_TRANS);
		// Clear the buffer images to transparent (0).
		offscreenBuffer_clearImage(s_cachedHudLeft,  0/*clear_color*/);
		offscreenBuffer_clearImage(s_cachedHudRight, 0/*clear_color*/);

		if (s_hudStatusL)
		{
			offscreenBuffer_drawTexture(s_cachedHudLeft, s_hudStatusL, 0, 0);
		}
		if (s_hudStatusR)
		{
			offscreenBuffer_drawTexture(s_cachedHudRight, s_hudStatusR, 0, 0);
		}

		#if TFE_CONVERT_CAPS
			hud_convertCapsToBM();
		#endif

		// TFE:
		// Load the caps
		s_hudCapLeft  = hud_loadTexture("HudStatusLeftAddon.bm");
		s_hudCapRight = hud_loadTexture("HudStatusRightAddon.bm");
	}
		
	void hud_updateBasePalette(u8* srcBuffer, s32 offset, s32 count)
	{
		u8* dstBuffer = s_tempBuffer + offset * 3;
		memcpy(dstBuffer, srcBuffer, count * 3);
		copyIntoPalette(s_basePalette + (offset * 3), srcBuffer, count, s_basePaletteCopyMode);

		s_hudPaletteIndex = offset;
		s_hudColorCount = count;
		s_updateHudColors = JTRUE;
	}
		
	void hud_initAnimation()
	{
		s_rightHudMove = 4;
		s_leftHudMove = 4;
		s_forceHudPaletteUpdate = JTRUE;
		s_rightHudShow = 4;
		s_leftHudShow = 4;
	}
		
	JBool hud_setupToggleAnim1(JBool enable)
	{
		if (enable)
		{
			s_leftHudVertTarget = 0;
			s_rightHudVertTarget = 0;

			s_leftHudVertAnim = 0;
			s_rightHudVertAnim = 0;

			s_leftHudShow = 4;
			s_rightHudShow = 4;

			s_leftHudMove = 4;
			s_rightHudMove = 4;
		}
		else
		{
			s_leftHudVertTarget = s_leftHudVertAnim == 0 ? 3 : 0;
			s_rightHudVertTarget = s_rightHudVertAnim == 0 ? 3 : 0;

			s_leftHudShow = 4;
			s_rightHudShow = 4;

			s_leftHudMove = 4;
			s_rightHudMove = 4;
		}
		return s_leftHudVertTarget == 0 ? JTRUE : JFALSE;
	}

	void hud_toggleDataDisplay()
	{
		s_showData = ~s_showData;
	}

	void hud_startup(JBool fromSave)
	{
		// Reset cached values.
		s_prevEnergy = 0;
		s_prevLifeCount = 0;
		s_prevShields = 0;
		s_prevHealth = 0;
		s_prevPrimaryAmmo = 0;
		s_prevSecondaryAmmo  = 0;
		s_prevSuperchageHud  = JFALSE;
		s_prevHeadlampActive = JFALSE;

		if (!fromSave)
		{
			s_secretsFound = 0;
			s_secretsPercent = 0;
		}

		hud_initAnimation();
		if (s_config.showUI)
		{
			hud_setupToggleAnim1(JTRUE);
		}
		offscreenBuffer_drawTexture(s_cachedHudRight, s_hudLightOff, 19, 0);
	}
		
	void hud_drawMessage(u8* framebuffer)
	{
		if (s_missionMode == MISSION_MODE_MAIN && s_hudMessage[0])
		{
			displayHudMessage(s_hudFont, (DrawRect*)vfb_getScreenRect(VFB_RECT_UI), 4, 10, s_hudMessage, framebuffer);
			if (s_curTick > s_hudMsgExpireTick)
			{
				s_hudMessage[0]   = 0;
				s_hudCurrentMsgId = 0;
				s_hudMsgPriority  = HUD_LOWEST_PRIORITY;
			}
			// s_screenDirtyLeft[s_curFrameBufferIdx] = JTRUE;
		}

		if (s_showData && s_playerEye)
		{
			fixed16_16 x, z;
			getCameraXZ(&x, &z);

			s32 xOffset = floor16(div16(intToFixed16(vfb_getWidescreenOffset()), vfb_getXScale()));

			char dataStr[64];
			sprintf(dataStr, "X:%04d Y:%.1f Z:%04d H:%.1f S:%d%%", floor16(x), -fixed16ToFloat(s_playerEye->posWS.y), floor16(z), fixed16ToFloat(s_playerEye->worldHeight), s_secretsPercent);
			displayHudMessage(s_hudFont, (DrawRect*)vfb_getScreenRect(VFB_RECT_UI), 164 + xOffset, 10, dataStr, framebuffer);
			// s_screenDirtyRight[s_curFrameBufferIdx] = JTRUE;
		}
	}

	void hud_drawStringGpu(Font* font, fixed16_16 x0, fixed16_16 y0, fixed16_16 xScale, fixed16_16 yScale, const char* str)
	{
		if (!font) { return; }

		fixed16_16 x = x0;
		fixed16_16 y = y0;
		for (char c = *str; c != 0; )
		{
			if (c == '\n')
			{
				y += mul16(intToFixed16(font->height + font->vertSpacing), yScale);
				x = x0;
			}
			else
			{
				if (c == ' ')
				{
					x += mul16(intToFixed16(font->width), xScale);
				}
				else if (c >= font->minChar && c <= font->maxChar)
				{
					s32 charIndex = c - font->minChar;
					s32 offset = charIndex * 28;

					TextureData* glyph = &font->glyphs[charIndex];
					screenGPU_blitTextureScaled(glyph, nullptr, x, y, xScale, yScale, 31);
					x += mul16(intToFixed16(glyph->width + font->horzSpacing), xScale);
				}
			}
			str++;
			c = *str;
		}
	}

	void hud_drawGpu()
	{
		if (s_rightHudMove)
		{
			s_rightHudMove--;
		}
		if (s_leftHudMove)
		{
			s_leftHudMove--;
		}

		// 1. Draw the base.
		TFE_Settings_Hud* hudSettings = TFE_Settings::getHudSettings();

		// HUD scaling.
		fixed16_16 xScale = vfb_getXScale();
		fixed16_16 yScale = vfb_getYScale();
		fixed16_16 hudScaleX = xScale;
		fixed16_16 hudScaleY = yScale;
		u32 dispWidth, dispHeight;
		vfb_getResolution(&dispWidth, &dispHeight);

		if (hudSettings->hudScale == TFE_HUDSCALE_SCALED)
		{
			hudScaleX = floatToFixed16(hudSettings->scale);
			hudScaleY = floatToFixed16(hudSettings->scale);
		}
		else if (hudSettings->scale != 0)
		{
			hudScaleX = floatToFixed16(fixed16ToFloat(xScale) * hudSettings->scale);
			hudScaleY = floatToFixed16(fixed16ToFloat(yScale) * hudSettings->scale);
		}

		fixed16_16 x0, x1;
		if (hudSettings->hudPos == TFE_HUDPOS_4_3)
		{
			x0 = mul16(intToFixed16(260), xScale) + intToFixed16(vfb_getWidescreenOffset());
			x1 = intToFixed16(vfb_getWidescreenOffset());
		}
		else
		{
			x0 = intToFixed16(dispWidth) - mul16(intToFixed16(s_cachedHudRight->width - 1), hudScaleX);
			x1 = 0;
		}
		x0 -= intToFixed16(hudSettings->pixelOffset[0]);
		x1 += intToFixed16(hudSettings->pixelOffset[1]);

		fixed16_16 y0 = yScale + mul16(intToFixed16(c_hudVertAnimTable[s_rightHudVertAnim]), yScale);
		fixed16_16 y1 = yScale + mul16(intToFixed16(c_hudVertAnimTable[s_leftHudVertAnim]),  yScale);
		y0 += intToFixed16(hudSettings->pixelOffset[2]);
		y1 += intToFixed16(hudSettings->pixelOffset[2]);

		screenGPU_blitTextureScaled(s_hudStatusR, nullptr, x0, y0, hudScaleX, hudScaleY, 31);
		screenGPU_blitTextureScaled(s_hudStatusL, nullptr, x1, y1, hudScaleX, hudScaleY, 31);
		if (hudSettings->hudPos == TFE_HUDPOS_4_3 || hudSettings->pixelOffset[0] > 0 || hudSettings->pixelOffset[1] > 0)
		{
			screenGPU_blitTextureScaled(s_hudCapLeft,  nullptr, x1 - mul16(intToFixed16(s_hudCapLeft->width - 1), hudScaleX), y1, hudScaleX, hudScaleY, 31);
			screenGPU_blitTextureScaled(s_hudCapRight, nullptr, x0 + mul16(intToFixed16(s_hudStatusR->width), hudScaleX), y1, hudScaleX, hudScaleY, 31);
		}

		// Energy
		fixed16_16 energy = TFE_Jedi::abs(s_energy * 100);
		s32 percent = round16(energy >> 1);
		{
			const s32 offset = 3 * 3 + 1;	// offset 3 color indices + 1 because we are only changing the green color.
			// I think this should be < 5, since there are 5 pips.
			// This actually overwrites memory in DOS, but here I just made the array larger.
			for (s32 clrIndex = 0; clrIndex <= 5; clrIndex++)
			{
				if (percent > 20)
				{
					s_hudWorkPalette[3 * clrIndex + offset] = 58;
					percent -= 20;
				}
				else
				{
					// Color value between [8, 58], where 8 @ 0% and 58 @ 20%
					s_hudWorkPalette[3 * clrIndex + offset] = 8 + ((percent * 5) >> 1);
					percent = 0;
				}
			}
		}
		s32 lifeCount = player_getLifeCount() % 10;
		{
			char lifeCountStr[8];
			sprintf(lifeCountStr, "%1d", lifeCount);

			fixed16_16 xPos = mul16(intToFixed16(52), hudScaleX) + x1;
			fixed16_16 yPos = mul16(intToFixed16(26), hudScaleY) + y1;
			hud_drawStringGpu(s_hudHealthFont, xPos, yPos, hudScaleX, hudScaleY, lifeCountStr);
			
			s_rightHudShow = 4;
		}
		// Headlamp
		{
			fixed16_16 xPos = mul16(intToFixed16(19), hudScaleX) + x0;
			fixed16_16 yPos = y0;
			if (s_headlampActive)
			{
				screenGPU_blitTextureScaled(s_hudLightOn, nullptr, xPos, yPos, hudScaleX, hudScaleY, 31);
			}
			else
			{
				screenGPU_blitTextureScaled(s_hudLightOff, nullptr, xPos, yPos, hudScaleX, hudScaleY, 31);
			}
			s_rightHudShow = 4;
		}
		// Shields
		{
			char shieldStr[8];
			if (s_playerInfo.shields == -1)
			{
				s_hudWorkPalette[0] = 55;
				s_hudWorkPalette[1] = 55;
				s_hudWorkPalette[3] = 55;
				s_hudWorkPalette[4] = 55;

				strcpy(shieldStr, "200");
			}
			else
			{
				s_hudWorkPalette[0] = 0;
				s32 upperShields;
				if (s_playerInfo.shields > 100)
				{
					upperShields = s_playerInfo.shields;
				}
				else
				{
					upperShields = 100;
				}
				upperShields -= 100;
				u8 upperColor = 8 + (upperShields >> 1);
				s_hudWorkPalette[1] = upperColor;

				s_hudWorkPalette[3] = 0;
				s32 lowerShields;
				if (s_playerInfo.shields < 100)
				{
					lowerShields = s_playerInfo.shields;
				}
				else
				{
					lowerShields = 100;
				}
				s_hudWorkPalette[4] = 8 + (lowerShields >> 1);
				sprintf(shieldStr, "%03d", s_playerInfo.shields);
			}

			fixed16_16 xPos = mul16(intToFixed16(15), hudScaleX) + x1;
			fixed16_16 yPos = mul16(intToFixed16(26), hudScaleY) + y1;
			hud_drawStringGpu(s_hudShieldFont, xPos, yPos, hudScaleX, hudScaleY, shieldStr);
		}
		// Health
		{
			s32 health = min(100, s_playerInfo.health);
			// 6: color 2, red channel, 7 = green channel
			s_hudWorkPalette[6] = 8 + (health >> 1);
			s_hudWorkPalette[7] = health * 63 / 400;

			char healthStr[32];
			sprintf(healthStr, "%03d", s_playerInfo.health);
			fixed16_16 xPos = mul16(intToFixed16(33), hudScaleX) + x1;
			fixed16_16 yPos = mul16(intToFixed16(26), hudScaleY) + y1;
			hud_drawStringGpu(s_hudHealthFont, xPos, yPos, hudScaleX, hudScaleY, healthStr);

			s_leftHudShow = 4;
		}

		if (s_forceHudPaletteUpdate || memcmp(s_hudPalette, s_hudWorkPalette, HUD_COLORS_COUNT * 3) != 0)
		{
			// Copy work palette into hud palette.
			memcpy(s_hudPalette, s_hudWorkPalette, HUD_COLORS_COUNT * 3);
			// Copy hud palette into the base palette, this will update next time render_setPalette() is called.
			hud_updateBasePalette(s_hudPalette, HUD_COLORS_START, HUD_COLORS_COUNT);
			// Copy the final 8 HUD colors to the loaded palette.
			memcpy(s_levelPalette + HUD_COLORS_START * 3, s_hudPalette, HUD_COLORS_COUNT * 3);

			s_forceHudPaletteUpdate = JFALSE;
		}

		s32 ammo0 = -1;
		s32 ammo1 = -1;
		PlayerWeapon* weapon = s_curPlayerWeapon;
		if (weapon)
		{
			if (weapon->ammo)
			{
				ammo0 = *weapon->ammo;
			}
			if (weapon->secondaryAmmo)
			{
				ammo1 = *weapon->secondaryAmmo;
			}
		}

		// Ammo readouts.
		{
			char str[32];
			if (ammo0 < 0)
			{
				strcpy(str, ":::");
			}
			else
			{
				sprintf(str, "%03d", ammo0);
			}
			fixed16_16 xPos = mul16(intToFixed16(12), hudScaleX) + x0;
			fixed16_16 yPos = mul16(intToFixed16(21), hudScaleY) + y0;
			hud_drawStringGpu(s_superChargeHud ? s_hudSuperAmmoFont : s_hudMainAmmoFont, xPos, yPos, hudScaleX, hudScaleY, str);

			// Draw a colored quad to hide single-pixel "leaking" from beneath.
			xPos = mul16(intToFixed16(25), hudScaleX) + x0;
			yPos = mul16(intToFixed16(12), hudScaleY) + y0;
			fixed16_16 quadWidth  = mul16(intToFixed16(11), hudScaleX);
			fixed16_16 quadHeight = mul16(intToFixed16(7),  hudScaleY);

			screenGPU_drawColoredQuad(xPos, yPos, xPos + quadWidth, yPos + quadHeight, 0);

			if (ammo1 < 0)
			{
				strcpy(str, "::");
			}
			else
			{
				sprintf(str, "%02d", ammo1);
			}
			hud_drawStringGpu(s_hudShieldFont, xPos, yPos, hudScaleX, hudScaleY, str);

			s_rightHudShow = 4;
		}

		if (s_rightHudShow)
		{
			if (s_rightHudVertAnim > s_rightHudVertTarget)
			{
				s_rightHudVertAnim--;
			}
			else if (s_rightHudVertAnim < s_rightHudVertTarget)
			{
				s_rightHudVertAnim++;
				s_rightHudMove = 4;
			}
			else if (s_rightHudShow)
			{
				s_rightHudShow--;
			}
		}
		if (s_leftHudShow)
		{
			if (s_leftHudVertAnim > s_leftHudVertTarget)
			{
				s_leftHudVertAnim--;
				s_leftHudMove = 4;
			}
			else if (s_leftHudVertAnim < s_leftHudVertTarget)
			{
				s_leftHudVertAnim++;
				s_leftHudMove = 4;
			}
			else if (s_leftHudShow)
			{
				s_leftHudShow--;
			}
		}
	}
		
	void hud_drawAndUpdate(u8* framebuffer)
	{
		ScreenRect* screenRect = vfb_getScreenRect(VFB_RECT_UI);

		// TFE Note: drawing the HUD when GPU rendering is enabled is a bit different, since we can just draw all of the items scaled.
		if (TFE_Jedi::getSubRenderer() == TSR_CLASSIC_GPU)
		{
			hud_drawGpu();
			return;
		}

		// Clear the 3D view while the HUD positions are being animated.
		if (s_rightHudMove || s_leftHudMove)
		{
			if (s_rightHudMove)
			{
				s_rightHudMove--;
			}
			if (s_leftHudMove)
			{
				s_leftHudMove--;
			}
			clear3DView(framebuffer);
		}

		// Go ahead and just update the HUD every frame, there are already checks to see if each item changed.
		//if (s_leftHudShow || s_rightHudShow)
		{
			fixed16_16 energy = TFE_Jedi::abs(s_energy * 100);
			s32 percent = round16(energy >> 1);
			if (s_prevEnergy != percent)  // True when energy ticks down.
			{
				s_prevEnergy = percent;
				const s32 offset = 3 * 3 + 1;	// offset 3 color indices + 1 because we are only changing the green color.
				// I think this should be < 5, since there are 5 pips.
				// This actually overwrites memory in DOS, but here I just made the array larger.
				for (s32 clrIndex = 0; clrIndex <= 5; clrIndex++)
				{
					if (percent > 20)
					{
						s_hudWorkPalette[3 * clrIndex + offset] = 58;
						percent -= 20;
					}
					else
					{
						// Color value between [8, 58], where 8 @ 0% and 58 @ 20%
						s_hudWorkPalette[3 * clrIndex + offset] = 8 + ((percent * 5) >> 1);
						percent = 0;
					}
				}
			}
			s32 lifeCount = player_getLifeCount() % 10;
			if (s_prevLifeCount != lifeCount)
			{
				s_prevLifeCount = lifeCount;

				char lifeCountStr[8];
				sprintf(lifeCountStr, "%1d", lifeCount);
				hud_drawString(s_cachedHudLeft, s_hudHealthFont, 52, 26, lifeCountStr);

				s_rightHudShow = 4;
			}
			if (s_prevHeadlampActive != s_headlampActive)
			{
				s_prevHeadlampActive = s_headlampActive;
				if (s_headlampActive)
				{
					offscreenBuffer_drawTexture(s_cachedHudRight, s_hudLightOn, 19, 0);
				}
				else
				{
					offscreenBuffer_drawTexture(s_cachedHudRight, s_hudLightOff, 19, 0);
				}
				s_rightHudShow = 4;
			}
			if (s_playerInfo.shields != s_prevShields)
			{
				char shieldStr[8];

				if (s_playerInfo.shields == -1)
				{
					s_hudWorkPalette[0] = 55;
					s_hudWorkPalette[1] = 55;
					s_hudWorkPalette[3] = 55;
					s_hudWorkPalette[4] = 55;

					strcpy(shieldStr, "200");
				}
				else
				{
					s_hudWorkPalette[0] = 0;
					s32 upperShields;
					if (s_playerInfo.shields > 100)
					{
						upperShields = s_playerInfo.shields;
					}
					else
					{
						upperShields = 100;
					}
					upperShields -= 100;
					u8 upperColor = 8 + (upperShields >> 1);
					s_hudWorkPalette[1] = upperColor;

					s_hudWorkPalette[3] = 0;
					s32 lowerShields;
					if (s_playerInfo.shields < 100)
					{
						lowerShields = s_playerInfo.shields;
					}
					else
					{
						lowerShields = 100;
					}
					s_hudWorkPalette[4] = 8 + (lowerShields >> 1);
					sprintf(shieldStr, "%03d", s_playerInfo.shields);
				}
				s_prevShields = s_playerInfo.shields;
				hud_drawString(s_cachedHudLeft, s_hudShieldFont, 15, 26, shieldStr);
			}
			if (s_playerInfo.health != s_prevHealth)
			{
				s32 health = min(100, s_playerInfo.health);
				// 6: color 2, red channel, 7 = green channel
				s_hudWorkPalette[6] = 8 + (health >> 1);
				s_hudWorkPalette[7] = health * 63 / 400;
				s_prevHealth = s_playerInfo.health;

				char healthStr[32];
				sprintf(healthStr, "%03d", s_playerInfo.health);
				hud_drawString(s_cachedHudLeft, s_hudHealthFont, 33, 26, healthStr);

				s_leftHudShow = 4;
			}

			if (s_forceHudPaletteUpdate || memcmp(s_hudPalette, s_hudWorkPalette, HUD_COLORS_COUNT * 3) != 0)
			{
				// Copy work palette into hud palette.
				memcpy(s_hudPalette, s_hudWorkPalette, HUD_COLORS_COUNT * 3);
				// Copy hud palette into the base palette, this will update next time render_setPalette() is called.
				hud_updateBasePalette(s_hudPalette, HUD_COLORS_START, HUD_COLORS_COUNT);
				// Copy the final 8 HUD colors to the loaded palette.
				memcpy(s_levelPalette + HUD_COLORS_START * 3, s_hudPalette, HUD_COLORS_COUNT * 3);

				s_forceHudPaletteUpdate = JFALSE;
			}

			s32 ammo0 = -1;
			s32 ammo1 = -1;
			PlayerWeapon* weapon = s_curPlayerWeapon;
			if (weapon)
			{
				if (weapon->ammo)
				{
					ammo0 = *weapon->ammo;
				}
				if (weapon->secondaryAmmo)
				{
					ammo1 = *weapon->secondaryAmmo;
				}
			}
						
			if (ammo0 != s_prevPrimaryAmmo || ammo1 != s_prevSecondaryAmmo || s_superChargeHud != s_prevSuperchageHud)
			{
				char str[32];
				if (ammo0 < 0)
				{
					strcpy(str, ":::");
				}
				else
				{
					sprintf(str, "%03d", ammo0);
				}
				hud_drawString(s_cachedHudRight, s_superChargeHud ? s_hudSuperAmmoFont : s_hudMainAmmoFont, 12, 21, str);

				if (ammo1 < 0)
				{
					strcpy(str, "::");
				}
				else
				{
					sprintf(str, "%02d", ammo1);
				}
				hud_drawString(s_cachedHudRight, s_hudShieldFont, 25, 12, str);

				s_rightHudShow = 4;
				s_prevPrimaryAmmo = ammo0;
				s_prevSecondaryAmmo = ammo1;
				s_prevSuperchageHud = s_superChargeHud;
			}

			if (s_rightHudShow || screenRect->bot >= 160)
			{
				if (s_rightHudVertAnim > s_rightHudVertTarget)
				{
					s_rightHudVertAnim--;
				}
				else if (s_rightHudVertAnim < s_rightHudVertTarget)
				{
					s_rightHudVertAnim++;
					s_rightHudMove = 4;
				}
				else if (s_rightHudShow)
				{
					s_rightHudShow--;
				}
			}
			if (s_leftHudShow || screenRect->bot >= 160)
			{
				if (s_leftHudVertAnim > s_leftHudVertTarget)
				{
					s_leftHudVertAnim--;
					s_leftHudMove = 4;
				}
				else if (s_leftHudVertAnim < s_leftHudVertTarget)
				{
					s_leftHudVertAnim++;
					s_leftHudMove = 4;
				}
				else if (s_leftHudShow)
				{
					s_leftHudShow--;
				}
			}
		}
		assert(s_rightHudVertAnim >= 0 && s_rightHudVertAnim < 4);
		u32 dispWidth, dispHeight;
		vfb_getResolution(&dispWidth, &dispHeight);
		if (dispWidth == 320 && dispHeight == 200)
		{
			// The original 320x200 code.
			hud_drawElementToScreen(s_cachedHudRight, screenRect, 260, c_hudVertAnimTable[s_rightHudVertAnim], framebuffer);
			hud_drawElementToScreen(s_cachedHudLeft,  screenRect,   0, c_hudVertAnimTable[s_leftHudVertAnim],  framebuffer);
		}
		else
		{
			TFE_Settings_Hud* hudSettings = TFE_Settings::getHudSettings();
			
			// HUD scaling.
			fixed16_16 xScale = vfb_getXScale();
			fixed16_16 yScale = vfb_getYScale();
			fixed16_16 hudScaleX = xScale;
			fixed16_16 hudScaleY = yScale;
			u32 dispWidth, dispHeight;
			vfb_getResolution(&dispWidth, &dispHeight);

			if (hudSettings->hudScale == TFE_HUDSCALE_SCALED)
			{
				hudScaleX = floatToFixed16(hudSettings->scale);
				hudScaleY = floatToFixed16(hudSettings->scale);
			}
			else if (hudSettings->scale != 0)
			{
				hudScaleX = floatToFixed16(fixed16ToFloat(xScale) * hudSettings->scale);
				hudScaleY = floatToFixed16(fixed16ToFloat(yScale) * hudSettings->scale);
			}

			s32 x0, x1;
			if (hudSettings->hudPos == TFE_HUDPOS_4_3)
			{
				x0 = floor16(mul16(intToFixed16(260), xScale)) + vfb_getWidescreenOffset();
				x1 = vfb_getWidescreenOffset();
			}
			else
			{
				x0 = dispWidth - floor16(mul16(intToFixed16(s_cachedHudRight->width - 1), hudScaleX));
				x1 = 0;
			}
			x0 -= hudSettings->pixelOffset[0];
			x1 += hudSettings->pixelOffset[1];

			s32 y0 = floor16(yScale + mul16(intToFixed16(c_hudVertAnimTable[s_rightHudVertAnim]), yScale));
			s32 y1 = floor16(yScale + mul16(intToFixed16(c_hudVertAnimTable[s_leftHudVertAnim]), yScale));
			y0 += hudSettings->pixelOffset[2];
			y1 += hudSettings->pixelOffset[2];

			hud_drawElementToScreenScaled(s_cachedHudRight, screenRect, x0, y0, hudScaleX, hudScaleY, framebuffer);
			hud_drawElementToScreenScaled(s_cachedHudLeft,  screenRect, x1, y1, hudScaleX, hudScaleY, framebuffer);

			if (hudSettings->hudPos == TFE_HUDPOS_4_3 || hudSettings->pixelOffset[0] > 0 || hudSettings->pixelOffset[1] > 0)
			{
				DrawRect rect = { screenRect->left, screenRect->top, screenRect->right, screenRect->bot };
				s32 y0Scaled = (dispHeight == 200) ? y0 : floor16(intToFixed16(y0) + mul16(ONE_16, hudScaleY));
				s32 y1Scaled = (dispHeight == 200) ? y1 : floor16(intToFixed16(y1) + mul16(ONE_16, hudScaleY));

				blitTextureToScreenScaled(s_hudCapLeft,  &rect, floor16(intToFixed16(x1) - mul16(intToFixed16(s_hudCapLeft->width - 1), hudScaleX)), y1Scaled, hudScaleX, hudScaleY, framebuffer);
				blitTextureToScreenScaled(s_hudCapRight, &rect, floor16(intToFixed16(x0) + mul16(intToFixed16(s_hudStatusR->width - 1), hudScaleX)), y0Scaled, hudScaleX, hudScaleY, framebuffer);
			}
		}
	}

	///////////////////////////////////////////
	// Internal Implementation
	///////////////////////////////////////////
	TextureData* hud_loadTexture(const char* texFile)
	{
		TextureData* hudTex = bitmap_load(texFile, 0, POOL_GAME);
		if (!hudTex)
		{
			TFE_System::logWrite(LOG_ERROR, "HUD", "Cannot load texture '%s'", texFile);
		}
		return hudTex;
	}

	Font* hud_loadFont(const char* fontFile)
	{
		FilePath filePath;
		if (TFE_Paths::getFilePath(fontFile, &filePath))
		{
			return font_load(&filePath);
		}
		TFE_System::logWrite(LOG_ERROR, "HUD", "Cannot load font '%s'", fontFile);
		return nullptr;
	}

	GameMessage* hud_getMessage(GameMessages* messages, s32 msgId)
	{
		s32 count = messages->count;
		GameMessage* msg = messages->msgList;

		for (s32 i = 0; i < count; i++, msg++)
		{
			if (msgId == msg->id)
			{
				return msg;
			}
		}
		return nullptr;
	}
		
	void copyIntoPalette(u8* dst, u8* src, s32 count, s32 mode)
	{
		memcpy(dst, src, count * 3);
	}

	void getCameraXZ(fixed16_16* x, fixed16_16* z)
	{
		if (s_drawAutomap)
		{
			*x = s_mapX0;
			*z = s_mapZ0;
		}
		else
		{
			*x = s_eyePos.x;
			*z = s_eyePos.z;
		}
	}

	void displayHudMessage(Font* font, DrawRect* rect, s32 x, s32 y, char* msg, u8* framebuffer)
	{
		if (!font || !rect || !framebuffer) { return; }
		u32 dispWidth, dispHeight;
		vfb_getResolution(&dispWidth, &dispHeight);

		if (dispHeight == 200)
		{
			s32 xi = x;
			s32 x0 = x;
			for (char c = *msg; c != 0;)
			{
				if (c == '\n')
				{
					xi = x0;
					y += font->height + font->vertSpacing;
				}
				else if (c == ' ')
				{
					xi += font->width;
				}
				else if (c >= font->minChar && c <= font->maxChar)
				{
					s32 charIndex = c - font->minChar;
					TextureData* glyph = &font->glyphs[charIndex];
					blitTextureToScreen(glyph, rect, xi, y, framebuffer);

					xi += font->horzSpacing + glyph->width;
				}
				msg++;
				c = *msg;
			}
		}
		else
		{
			fixed16_16 xScale = vfb_getXScale();
			fixed16_16 yScale = vfb_getYScale();

			fixed16_16 xf = mul16(intToFixed16(x), xScale);
			fixed16_16 yf = mul16(intToFixed16(y), yScale);
			fixed16_16 x0 = xf;

			fixed16_16 fWidth = mul16(intToFixed16(font->width), xScale);
			for (char c = *msg; c != 0;)
			{
				if (c == '\n')
				{
					xf = x0;
					yf += mul16(intToFixed16(font->height + font->vertSpacing), yScale);
				}
				else if (c == ' ')
				{
					xf += fWidth;
				}
				else if (c >= font->minChar && c <= font->maxChar)
				{
					s32 charIndex = c - font->minChar;
					TextureData* glyph = &font->glyphs[charIndex];
					blitTextureToScreenScaledText(glyph, rect, floor16(xf), floor16(yf), xScale, yScale, framebuffer);
					xf += mul16(intToFixed16(font->horzSpacing + glyph->width), xScale);
				}
				msg++;
				c = *msg;
			}
		}
	}

	void hud_drawString(OffScreenBuffer* elem, Font* font, s32 x0, s32 y0, const char* str)
	{
		if (!font) { return; }

		s32 x = x0;
		s32 y = y0;
		for (char c = *str; c != 0; )
		{
			if (c == '\n')
			{
				y += font->height + font->vertSpacing;
				x = x0;
			}
			else
			{
				if (c == ' ')
				{
					x += font->width;
				}
				else if (c >= font->minChar && c <= font->maxChar)
				{
					s32 charIndex = c - font->minChar;
					s32 offset = charIndex * 28;

					TextureData* glyph = &font->glyphs[charIndex];
					offscreenBuffer_drawTexture(elem, glyph, x, y);
					x += glyph->width + font->horzSpacing;
				}
			}
			str++;
			c = *str;
		}
	}

	void hud_drawElementToScreen(OffScreenBuffer* elem, ScreenRect* rect, s32 x0, s32 y0, u8* framebuffer)
	{
		s32 x1 = x0 + elem->width - 1;
		u8* image = elem->image;
		s32 y1 = y0 + elem->height - 1;
		if (x0 > rect->right || x1 < rect->left || y0 > rect->bot || y1 < rect->top)
		{
			return;
		}
		if (y0 < rect->top)
		{
			image += (rect->top - y0) * elem->width;
			y0 = rect->top;
		}
		if (y1 > rect->bot)
		{
			y1 = rect->bot;
		}
		if (x0 < rect->left)
		{
			image += rect->left - x0;
			x0 = rect->left;
		}
		if (x1 > rect->right)
		{
			x1 = rect->right;
		}
		const u32 stride = vfb_getStride();
		s32 yOffset = y0 * stride;
		if (elem->flags & OBF_TRANS)
		{
			for (s32 x = x0; x <= x1; x++)
			{
				u8* output = framebuffer + x + yOffset;
				u8* imageSrc = image + x - x0;
				for (s32 y = y0; y <= y1; y++, output += stride, imageSrc += elem->width)
				{
					u8 color = *imageSrc;
					if (color)
					{
						*output = color;
					}
				}
			}
		}
		else
		{
			for (s32 x = x0; x <= x1; x++)
			{
				u8* output = framebuffer + x + yOffset;
				u8* imageSrc = image + x - x0;
				for (s32 y = y0; y <= y1; y++, output += stride, imageSrc += elem->width)
				{
					*output = *imageSrc;
				}
			}
		}
	}

	void hud_drawElementToScreenScaled(OffScreenBuffer* elem, ScreenRect* rect, s32 x0, s32 y0, fixed16_16 xScale, fixed16_16 yScale, u8* framebuffer)
	{
		ScreenImage image =
		{
			elem->width,
			elem->height,
			elem->image,
			(elem->flags & OBF_TRANS) ? JTRUE : JFALSE,
			JFALSE
		};
		blitTextureToScreenScaled(&image, (DrawRect*)rect, x0, y0, xScale, yScale, framebuffer);
	}

	// This should not be enabled in released builds - it is only kept in case the images need to be regenerated.
#if TFE_CONVERT_CAPS
	u8 hud_findColorInPalette(u32 color, u32 colorCount, const u8* colors, const u8* palette)
	{
		// Transparent.
		if ((color >> 24) < 16)
		{
			return 0;
		}

		s32 srcR = color & 0xff;
		s32 srcG = (color >> 8) & 0xff;
		s32 srcB = (color >> 16) & 0xff;
		s32 colorDist = INT_MAX;
		s32 match = -1;
		for (u32 i = 0; i < colorCount; i++)
		{
			s32 dstR = CONV_6bitTo8bit(palette[colors[i] * 3 + 0]);
			s32 dstG = CONV_6bitTo8bit(palette[colors[i] * 3 + 1]);
			s32 dstB = CONV_6bitTo8bit(palette[colors[i] * 3 + 2]);
			s32 distApprox = TFE_Jedi::abs(srcR - dstR) + TFE_Jedi::abs(srcG - dstG) + TFE_Jedi::abs(srcB - dstB);
			if (distApprox < colorDist)
			{
				colorDist = distApprox;
				match = colors[i];
			}
		}
		assert(colorDist < 4);
		return match >= 0 ? match : 0;
	}

	void hud_convertPngToBM(const char* path, const char* name, u32 colorCount, const u8* colors, const u8* palette)
	{
		char srcPath[TFE_MAX_PATH];
		char dstPath[TFE_MAX_PATH];
		sprintf(srcPath, "%sTFE/AdjustableHud/%s.png", path, name);
		sprintf(dstPath, "%sTFE/AdjustableHud/%s.bm", path, name);

		Image* srcImage = TFE_Image::get(srcPath);
		if (!srcImage) { return; }

		u32 pixelCount = srcImage->width * srcImage->height;
		u8* output = (u8*)malloc(pixelCount);
		const u32* input = srcImage->data;
		// Convert to column format.
		for (u32 y = 0; y < srcImage->height; y++)
		{
			for (u32 x = 0; x < srcImage->width; x++)
			{
				output[x*srcImage->height + y] = hud_findColorInPalette(input[(srcImage->height - y - 1)*srcImage->width + x], colorCount, colors, palette);
			}
		}

		bitmap_writeTransparent(dstPath, u16(srcImage->width), u16(srcImage->height), output);
		free(output);
	}

	void hud_convertCapsToBM()
	{
		// First make sure the colors match, so grab all colors used by the original HUD backgrounds.
		u8 colorMap[256] = { 0 };
		u8 colors[256];
		u8 colorCount = 0;
		const u8* image = s_hudStatusL->image;
		for (s32 i = 0; i < s_hudStatusL->dataSize; i++)
		{
			if (image[i] == 0) { continue; }
			if (colorMap[image[i]] == 0)
			{
				colorMap[image[i]] = 1;
				colors[colorCount++] = image[i];
			}
		}

		image = s_hudStatusR->image;
		for (s32 i = 0; i < s_hudStatusR->dataSize; i++)
		{
			if (image[i] == 0) { continue; }
			if (colorMap[image[i]] == 0)
			{
				colorMap[image[i]] = 1;
				colors[colorCount++] = image[i];
			}
		}

		// Load default palette.
		FilePath filePath;
		u8 pal[768];
		if (TFE_Paths::getFilePath("SECBASE.PAL", &filePath))
		{
			FileStream::readContents(&filePath, pal, 768);
		}

		// Next load the true-color images.
		const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
		char programDirModDir[TFE_MAX_PATH];
		sprintf(programDirModDir, "%sMods/", programDir);
		TFE_Paths::fixupPathAsDirectory(programDirModDir);

		hud_convertPngToBM(programDirModDir, "HudStatusLeftAddon", colorCount, colors, pal);
		hud_convertPngToBM(programDirModDir, "HudStatusRightAddon", colorCount, colors, pal);
	}
#endif
}  // TFE_DarkForces