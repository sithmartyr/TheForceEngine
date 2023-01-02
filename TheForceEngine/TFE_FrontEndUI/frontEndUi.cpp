#include "frontEndUi.h"
#include "console.h"
#include "profilerView.h"
#include "modLoader.h"
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/midiDevice.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_DarkForces/config.h>
#include <TFE_Game/reticle.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/system.h>
#include <TFE_System/parser.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Archive/archive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Archive/zipArchive.h>
#include <TFE_Archive/gobMemoryArchive.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Ui/ui.h>
#include <TFE_Ui/markdown.h>
#include <TFE_Ui/imGUI/imgui.h>
// Game
#include <TFE_DarkForces/mission.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>

#include <climits>

using namespace TFE_Input;

namespace TFE_FrontEndUI
{
	struct UiImage
	{
		void* image;
		u32 width;
		u32 height;
	};

	enum SubUI
	{
		FEUI_NONE = 0,
		FEUI_MANUAL,
		FEUI_CREDITS,
		FEUI_CONFIG,
		FEUI_MODS,
		FEUI_COUNT
	};

	enum ConfigTab
	{
		CONFIG_ABOUT = 0,
		CONFIG_GAME,
		CONFIG_SAVE,
		CONFIG_LOAD,
		CONFIG_INPUT,
		CONFIG_GRAPHICS,
		CONFIG_HUD,
		CONFIG_SOUND,
		CONFIG_COUNT
	};

	const char* c_configLabels[] =
	{
		"About",
		"Game Settings",
		"Save",
		"Load",
		"Input",
		"Graphics",
		"Hud",
		"Sound",
	};

	static const Vec2i c_resolutionDim[] =
	{
		{320,200},
		{640,400},
		{640,480},
		{800,600},
		{960,720},
		{1024,768},
		{1280,960},
		{1440,1050},
		{1440,1080},
		{1600,1200},
		{1856,1392},
		{1920,1440},
		{2048,1536},
		{2880,2160}
	};

	static const char* c_resolutions[] =
	{
		"200p  (320x200)",
		"400p  (640x400)",
		"480p  (640x480)",
		"600p  (800x600)",
		"720p  (960x720)",
		"768p  (1024x768)",
		"960p  (1280x960)",
		"1050p (1440x1050)",
		"1080p (1440x1080)",
		"1200p (1600x1200)",
		"1392p (1856x1392)",
		"1440p (1920x1440)",
		"1536p (2048x1536)",
		"2160p (2880x2160)",
		"Match Window",
		"Custom",
	};

	static const char* c_resolutionsWide[] =
	{
		"200p",
		"400p",
		"480p",
		"600p",
		"720p",
		"768p",
		"960p",
		"1050p",
		"1080p",
		"1200p",
		"1392p",
		"1440p",
		"1536p",
		"2160p",
		"Match Window",
		"Custom",
	};

	static const char* c_renderer[] =
	{
		"Classic (Software)",
		"GPU / OpenGL",
	};

	typedef void(*MenuItemSelected)();

	static s32 s_resIndex = 0;
	static f32 s_uiScale = 1.0f;
	static char* s_aboutDisplayStr = nullptr;
	static char* s_manualDisplayStr = nullptr;
	static char* s_creditsDisplayStr = nullptr;

	static AppState s_appState;
	static AppState s_menuRetState;
	static ImFont* s_menuFont;
	static ImFont* s_versionFont;
	static ImFont* s_titleFont;
	static ImFont* s_dialogFont;
	static SubUI s_subUI;
	static ConfigTab s_configTab;
	static bool s_saveLoadSetupRequired = true;
	static bool s_bindingPopupOpen = false;

	static bool s_consoleActive = false;
	static bool s_relativeMode;
	static bool s_canSave = false;
	static bool s_drawNoGameDataMsg = false;

	static UiImage s_logoGpuImage;
	static UiImage s_titleGpuImage;
	static UiImage s_gradientImage;

	static UiImage s_buttonNormal[7];
	static UiImage s_buttonSelected[7];

	static MenuItemSelected s_menuItemselected[7];

	static const char* c_axisBinding[] =
	{
		"X Left Axis",
		"Y Left Axis",
		"X Right Axis",
		"Y Right Axis",
	};

	static const char* c_mouseMode[] =
	{
		"Menus Only",
		"Horizontal Turning",
		"Mouselook"
	};

	static InputConfig* s_inputConfig = nullptr;
	static bool s_controllerWinOpen = true;
	static bool s_mouseWinOpen = true;
	static bool s_inputMappingOpen = true;

	// Mod Support
	static char s_selectedModCmd[TFE_MAX_PATH] = "";

	void configAbout();
	void configGame();
	void configSave();
	void configLoad();
	void configInput();
	void configGraphics();
	void configHud();
	void configSound();
	void pickCurrentResolution();
	void manual();
	void credits();

	void configSaveLoadBegin(bool save);
	void renderBackground();

	void menuItem_Start();
	void menuItem_Manual();
	void menuItem_Credits();
	void menuItem_Settings();
	void menuItem_Mods();
	void menuItem_Editor();
	void menuItem_Exit();

	bool loadGpuImage(const char* localPath, UiImage* uiImage)
	{
		char imagePath[TFE_MAX_PATH];
		TFE_Paths::appendPath(TFE_PathType::PATH_PROGRAM, localPath, imagePath, TFE_MAX_PATH);
		Image* image = TFE_Image::get(imagePath);
		if (image)
		{
			TextureGpu* gpuImage = TFE_RenderBackend::createTexture(image->width, image->height, image->data, MAG_FILTER_LINEAR);
			uiImage->image = TFE_RenderBackend::getGpuPtr(gpuImage);
			uiImage->width = image->width;
			uiImage->height = image->height;
			return true;
		}
		return false;
	}

	void initConsole()
	{
		TFE_Console::init();
		TFE_ProfilerView::init();
	}

	void init()
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_FrontEndUI::init");
		s_appState = APP_STATE_MENU;
		s_menuRetState = APP_STATE_MENU;
		s_subUI = FEUI_NONE;
		s_relativeMode = false;
		s_drawNoGameDataMsg = false;

		s_uiScale = (f32)TFE_Ui::getUiScale() * 0.01f;

		ImGuiIO& io = ImGui::GetIO();
		s_menuFont    = io.Fonts->AddFontFromFileTTF("Fonts/DroidSansMono.ttf", floorf(32*s_uiScale + 0.5f));
		s_versionFont = io.Fonts->AddFontFromFileTTF("Fonts/DroidSansMono.ttf", floorf(16*s_uiScale + 0.5f));
		s_titleFont   = io.Fonts->AddFontFromFileTTF("Fonts/DroidSansMono.ttf", floorf(48*s_uiScale + 0.5f));
		s_dialogFont  = io.Fonts->AddFontFromFileTTF("Fonts/DroidSansMono.ttf", floorf(20*s_uiScale + 0.5f));

		if (!loadGpuImage("UI_Images/TFE_TitleLogo.png", &s_logoGpuImage))
		{
			TFE_System::logWrite(LOG_ERROR, "SystemUI", "Cannot load TFE logo: \"UI_Images/TFE_TitleLogo.png\"");
		}
		if (!loadGpuImage("UI_Images/TFE_TitleText.png", &s_titleGpuImage))
		{
			TFE_System::logWrite(LOG_ERROR, "SystemUI", "Cannot load TFE Title: \"UI_Images/TFE_TitleText.png\"");
		}
		if (!loadGpuImage("UI_Images/Gradient.png", &s_gradientImage))
		{
			TFE_System::logWrite(LOG_ERROR, "SystemUI", "Cannot load TFE Title: \"UI_Images/Gradient.png\"");
		}

		bool buttonsLoaded = true;
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_StartNormal.png", &s_buttonNormal[0]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_StartSelected.png", &s_buttonSelected[0]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_ManualNormal.png", &s_buttonNormal[1]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_ManualSelected.png", &s_buttonSelected[1]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_CreditsNormal.png", &s_buttonNormal[2]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_CreditsSelected.png", &s_buttonSelected[2]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_SettingsNormal.png", &s_buttonNormal[3]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_SettingsSelected.png", &s_buttonSelected[3]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_ModsNormal.png", &s_buttonNormal[4]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_ModsSelected.png", &s_buttonSelected[4]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_EditorNormal.png", &s_buttonNormal[5]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_EditorSelected.png", &s_buttonSelected[5]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_ExitNormal.png", &s_buttonNormal[6]);
		buttonsLoaded &= loadGpuImage("UI_Images/TFE_ExitSelected.png", &s_buttonSelected[6]);
		if (!buttonsLoaded)
		{
			TFE_System::logWrite(LOG_ERROR, "SystemUI", "Cannot load title screen button images.");
		}
				
		// Setup menu item callbacks
		s_menuItemselected[0] = menuItem_Start;
		s_menuItemselected[1] = menuItem_Manual;
		s_menuItemselected[2] = menuItem_Credits;
		s_menuItemselected[3] = menuItem_Settings;
		s_menuItemselected[4] = menuItem_Mods;
		s_menuItemselected[5] = menuItem_Editor;
		s_menuItemselected[6] = menuItem_Exit;
	}

	void shutdown()
	{
		delete[] s_aboutDisplayStr;
		delete[] s_manualDisplayStr;
		delete[] s_creditsDisplayStr;
		TFE_Console::destroy();
		TFE_ProfilerView::destroy();
	}

	AppState update()
	{
		AppState state = s_appState;
		if (state == APP_STATE_EXIT_TO_MENU)
		{
			s_appState = APP_STATE_MENU;
		}
		return state;
	}

	void setAppState(AppState state)
	{
		s_appState = state;
	}

	void enableConfigMenu()
	{
		s_appState = APP_STATE_MENU;
		s_subUI = FEUI_CONFIG;
		s_relativeMode = TFE_Input::relativeModeEnabled();
		TFE_Input::enableRelativeMode(false);
		pickCurrentResolution();
	}

	bool isConfigMenuOpen()
	{
		return (s_appState == APP_STATE_MENU) && (s_subUI == FEUI_CONFIG);
	}

	void setMenuReturnState(AppState state)
	{
		s_menuRetState = state;
	}

	AppState menuReturn()
	{
		s_subUI = FEUI_NONE;
		s_drawNoGameDataMsg = false;
		s_appState = s_menuRetState;
		TFE_Settings::writeToDisk();
		TFE_Input::enableRelativeMode(s_relativeMode);
		inputMapping_serialize();

		return s_appState;
	}

	bool shouldClearScreen()
	{
		return s_appState == APP_STATE_MENU && s_subUI == FEUI_NONE;
	}

	bool toggleConsole()
	{
		s_consoleActive = !s_consoleActive;
		// Start open
		if (s_consoleActive)
		{
			TFE_Console::startOpen();
		}
		else
		{
			TFE_Console::startClose();
		}
		return s_consoleActive;
	}

	bool isConsoleOpen()
	{
		return TFE_Console::isOpen();
	}

	bool isConsoleAnimating()
	{
		return TFE_Console::isAnimating();
	}

	void logToConsole(const char* str)
	{
		TFE_Console::addToHistory(str);
	}

	void toggleProfilerView()
	{
		bool isEnabled = TFE_ProfilerView::isEnabled();
		TFE_ProfilerView::enable(!isEnabled);
	}

	void showNoGameDataUI()
	{
		char settingsPath[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_USER_DOCUMENTS, "settings.ini", settingsPath);
		const TFE_Game* game = TFE_Settings::getGame();

		ImGui::PushFont(s_dialogFont);

		bool active = true;
		ImGui::Begin("##NoGameData", &active, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
		ImGui::Text("No valid \"%s\" game data found.", game->game);
		ImGui::Text("Please select Configure from the menu,");
		ImGui::Text("open up Game settings and setup the game path.");
		ImGui::Separator();
		ImGui::Text("An example path, if the game was purchased through Steam is:");
		ImGui::Text("\"D:/Program Files (x86)/Steam/steamapps/common/dark forces/Game/\" ");
		ImGui::Separator();
		if (ImGui::Button("Return to Menu"))
		{
			s_appState = APP_STATE_MENU;
		}
		ImGui::End();
		ImGui::PopFont();
	}

	void draw(bool drawFrontEnd, bool noGameData, bool setDefaults)
	{
		const u32 windowInvisFlags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;

		DisplayInfo display;
		TFE_RenderBackend::getDisplayInfo(&display);
		u32 w = display.width;
		u32 h = display.height;
		u32 menuWidth = 156;
		u32 menuHeight = 306;

		if (TFE_Console::isOpen())
		{
			TFE_Console::update();
		}
		if (TFE_ProfilerView::isEnabled())
		{
			TFE_ProfilerView::update();
		}
		if (noGameData)
		{
			s_subUI = FEUI_CONFIG;
			s_configTab = CONFIG_GAME;
			s_appState = APP_STATE_MENU;
			s_drawNoGameDataMsg = true;
			pickCurrentResolution();
		}
		else if (setDefaults)
		{
			s_subUI = FEUI_NONE;
			s_appState = APP_STATE_SET_DEFAULTS;
			pickCurrentResolution();
		}
		if (!drawFrontEnd) { return; }

		if (s_subUI == FEUI_NONE)
		{
			s_menuRetState = APP_STATE_MENU;
			s_relativeMode = false;

			const f32 windowPadding = 16.0f;	// required padding so that a window completely holds a control without clipping.

			const s32 logoScale = 2160;	// logo and title are authored for 2160p so it looks good at high resolutions, such as 1440p and 4k.
			const s32 posScale = 1080;	// positions are at 1080p so must be scaled for different resolutions.
			const s32 titlePosY = 100;

			const s32 logoHeight = s_logoGpuImage.height  * h / logoScale;
			const s32 logoWidth = s_logoGpuImage.width   * h / logoScale;
			const s32 titleHeight = s_titleGpuImage.height * h / logoScale;
			const s32 titleWidth = s_titleGpuImage.width  * h / logoScale;
			const s32 textHeight = s_buttonNormal[0].height * h / logoScale;
			const s32 textWidth = s_buttonNormal[0].width * h / logoScale;
			const s32 topOffset = titlePosY * h / posScale;

			// Title
			bool titleActive = true;
			ImGui::SetNextWindowSize(ImVec2(logoWidth + windowPadding, logoHeight + windowPadding));
			ImGui::SetNextWindowPos(ImVec2(f32((w - logoWidth - titleWidth) / 2), (f32)topOffset));
			ImGui::Begin("##Logo", &titleActive, windowInvisFlags);
			ImGui::Image(s_logoGpuImage.image, ImVec2((f32)logoWidth, (f32)logoHeight));
			ImGui::End();

			ImGui::SetNextWindowSize(ImVec2(titleWidth + windowPadding, titleWidth + windowPadding));
			ImGui::SetNextWindowPos(ImVec2(f32((w + logoWidth - titleWidth) / 2), (f32)topOffset + (logoHeight - titleHeight) / 2));
			ImGui::Begin("##Title", &titleActive, windowInvisFlags);
			ImGui::Image(s_titleGpuImage.image, ImVec2((f32)titleWidth, (f32)titleHeight));
			ImGui::End();

			ImGui::PushFont(s_versionFont);
			char versionText[256];
			sprintf(versionText, "Version %s", TFE_System::getVersionString());
			const f32 stringWidth = s_versionFont->CalcTextSizeA(s_versionFont->FontSize, 1024.0f, 0.0f, versionText).x;

			ImGui::SetNextWindowPos(ImVec2(f32(w) - stringWidth - s_versionFont->FontSize*2.0f, f32(h) - s_versionFont->FontSize*4.0f));
			ImGui::Begin("##Version", &titleActive, windowInvisFlags);
			ImGui::Text("%s", versionText);
			ImGui::End();
			ImGui::PopFont();

			if (s_appState == APP_STATE_SET_DEFAULTS)
			{
				DisplayInfo displayInfo;
				TFE_RenderBackend::getDisplayInfo(&displayInfo);

				bool active = true;
				ImGui::PushFont(s_dialogFont);
				ImGui::SetNextWindowPos(ImVec2(max(0.0f, (displayInfo.width - 1280.0f * s_uiScale) * 0.5f), max(0.0f, (displayInfo.height - 300.0f * s_uiScale) * 0.5f)));
				ImGui::SetNextWindowSize(ImVec2(1280.0f * s_uiScale, 300.0f * s_uiScale));
				ImGui::Begin("Select Default Settings", &active, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

				ImGui::LabelText("##ConfigLabel", "Please select the appropriate defaults.");
				ImGui::PopFont();

				ImGui::PushFont(s_versionFont);
				ImGui::LabelText("##ConfigLabel", "Note that individual settings, (crosshair pattern, pitch limits, etc.), can be changed using");
				ImGui::LabelText("##ConfigLabel", "  Settings/Configuration at any time.");
				ImGui::LabelText("##ConfigLabel", "Individual settings can be changed at anytime.");
				ImGui::PopFont();

				ImGui::Separator();
				ImGui::PushFont(s_dialogFont);

				ImGui::LabelText("##ConfigLabel", "Modern:  Play in high resolution, widescreen, and use modern controls.");
				ImGui::LabelText("##ConfigLabel", "Vanilla: Play using the original resolution and controls.");
				ImGui::Separator();

				s_inputConfig = inputMapping_get();
				TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();
				TFE_Settings_Graphics* graphicsSettings = TFE_Settings::getGraphicsSettings();
								
				ImGui::SetCursorPosX(1280.0f * s_uiScale * 0.5f - 128.0f*s_uiScale);
				if (ImGui::Button("Modern"))
				{
					// Controls
					s_inputConfig->mouseMode = MMODE_LOOK;
					// Game
					gameSettings->df_showSecretFoundMsg = true;
					gameSettings->df_bobaFettFacePlayer = true;
					// Graphics
					graphicsSettings->rendererIndex = RENDERER_HARDWARE;
					graphicsSettings->skyMode = SKYMODE_CYLINDER;
					graphicsSettings->widescreen = true;
					graphicsSettings->gameResolution.x = displayInfo.width;
					graphicsSettings->gameResolution.z = displayInfo.height;
					// Reticle.
					graphicsSettings->reticleEnable = true;
					graphicsSettings->reticleIndex = 4;
					graphicsSettings->reticleRed = 0.25f;
					graphicsSettings->reticleGreen = 1.0f;
					graphicsSettings->reticleBlue = 0.25f;
					graphicsSettings->reticleOpacity = 1.0f;
					graphicsSettings->reticleScale = 0.5f;

					reticle_setShape(graphicsSettings->reticleIndex);
					reticle_setScale(graphicsSettings->reticleScale);
					reticle_setColor(&graphicsSettings->reticleRed);
					// Now go to the menu.
					TFE_Settings::writeToDisk();
					inputMapping_serialize();
					s_appState = APP_STATE_MENU;
				}
				ImGui::SameLine();
				if (ImGui::Button("Vanilla"))
				{
					// Controls
					s_inputConfig->mouseMode = MMODE_TURN;
					// Game
					gameSettings->df_showSecretFoundMsg = false;
					gameSettings->df_bobaFettFacePlayer = false;
					// Graphics
					graphicsSettings->rendererIndex = RENDERER_SOFTWARE;
					graphicsSettings->widescreen = false;
					graphicsSettings->gameResolution.x = 320;
					graphicsSettings->gameResolution.z = 200;
					// Reticle.
					graphicsSettings->reticleEnable = false;
					// Now go to the menu.
					TFE_Settings::writeToDisk();
					inputMapping_serialize();
					s_appState = APP_STATE_MENU;
				}
				ImGui::End();
				ImGui::PopFont();
			}
			else
			{
				s32 menuHeight = (textHeight + 24) * 7 + 4;

				// Main Menu
				ImGui::PushFont(s_menuFont);

				bool active = true;
				ImGui::SetNextWindowPos(ImVec2(f32((w - textWidth - 24) / 2), f32(h - menuHeight - topOffset)));
				ImGui::SetNextWindowSize(ImVec2((f32)textWidth + 24, f32(menuHeight)));
				ImGui::Begin("##MainMenu", &active, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
					ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

				ImVec2 textSize = ImVec2(f32(textWidth), f32(textHeight));
				for (s32 i = 0; i < 7; i++)
				{
					// Disable the editor for now.
					// Remove this out once it is working again.
					if (s_menuItemselected[i] == menuItem_Editor)
					{
						ImGui::ImageAnimButton(s_buttonNormal[i].image, s_buttonSelected[i].image, textSize,
							ImVec2(0,0), ImVec2(1,1), ImVec2(0,0), ImVec2(1,1), -1, ImVec4(0,0,0,0), ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
					}
					else if (ImGui::ImageAnimButton(s_buttonNormal[i].image, s_buttonSelected[i].image, textSize))
					{
						s_menuItemselected[i]();
					}
				}

				ImGui::End();
				ImGui::PopFont();
			}
		}
		else if (s_subUI == FEUI_MANUAL)
		{
			bool active = true;
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(f32(w), f32(h)));

			const u32 window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

			ImGui::Begin("Manual", &active, window_flags);
			if (ImGui::Button("Return") || !active)
			{
				s_subUI = FEUI_NONE;
			}
			manual();
			ImGui::End();
		}
		else if (s_subUI == FEUI_CREDITS)
		{
			bool active = true;
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(f32(w), f32(h)));

			const u32 window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

			ImGui::Begin("Credits", &active, window_flags);
			if (ImGui::Button("Return") || !active)
			{
				s_subUI = FEUI_NONE;
			}
			credits();
			ImGui::End();
		}
		else if (s_subUI == FEUI_MODS)
		{
			bool active = true;
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(f32(w), f32(h)));

			u32 window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
			if (modLoader_getViewMode() != VIEW_IMAGES)
			{
				window_flags |= ImGuiWindowFlags_HorizontalScrollbar;
			}
			ImGui::Begin("Mods", &active, window_flags);
			if (ImGui::Button("Cancel") || !active)
			{
				s_subUI = FEUI_NONE;
				modLoader_cleanupResources();
			}
			else
			{
				modLoader_selectionUI();
			}
			ImGui::End();
		}
		else if (s_subUI == FEUI_CONFIG)
		{
			bool active = true;
			u32 window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

			TFE_Input::clearAccumulatedMouseMove();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(160.0f*s_uiScale, f32(h)));
			ImGui::Begin("##Sidebar", &active, window_flags);

			ImVec2 sideBarButtonSize(144*s_uiScale, 24*s_uiScale);
			ImGui::PushFont(s_dialogFont);
			if (ImGui::Button("About", sideBarButtonSize))
			{
				s_configTab = CONFIG_ABOUT;
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
			}
			if (ImGui::Button("Game", sideBarButtonSize))
			{
				s_configTab = CONFIG_GAME;
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
			}
			if (ImGui::Button("Save", sideBarButtonSize))
			{
				s_configTab = CONFIG_SAVE;
				configSaveLoadBegin(true);
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
			}
			if (ImGui::Button("Load", sideBarButtonSize))
			{
				s_configTab = CONFIG_LOAD;
				configSaveLoadBegin(false);
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
			}
			if (ImGui::Button("Input", sideBarButtonSize))
			{
				s_configTab = CONFIG_INPUT;
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
			}
			if (ImGui::Button("Graphics", sideBarButtonSize))
			{
				s_configTab = CONFIG_GRAPHICS;
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
			}
			if (ImGui::Button("Hud", sideBarButtonSize))
			{
				s_configTab = CONFIG_HUD;
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
			}
			if (ImGui::Button("Sound", sideBarButtonSize))
			{
				s_configTab = CONFIG_SOUND;
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
			}
			ImGui::Separator();
			if (ImGui::Button("Return", sideBarButtonSize))
			{
				s_subUI = FEUI_NONE;
				s_drawNoGameDataMsg = false;
				s_appState = s_menuRetState;
				TFE_Settings::writeToDisk();
				TFE_Input::enableRelativeMode(s_relativeMode);
				inputMapping_serialize();
			}
			if (s_menuRetState != APP_STATE_MENU && ImGui::Button("Exit to Menu", sideBarButtonSize))
			{
				MIDI_ShowDevChangeAlert = false;
				s_menuRetState = APP_STATE_MENU;

				s_subUI = FEUI_NONE;
				s_drawNoGameDataMsg = false;
				s_appState = APP_STATE_EXIT_TO_MENU;
				TFE_Settings::writeToDisk();
				inputMapping_serialize();
				s_selectedModCmd[0] = 0;
			}
			ImGui::PopFont();

			ImGui::End();

			// adjust the width based on tab.
			s32 tabWidth = w - s32(160*s_uiScale);
			if (s_configTab >= CONFIG_INPUT)
			{
				tabWidth = s32(414*s_uiScale);
			}

			// Avoid annoying scrollbars...
			if (s_configTab == CONFIG_SAVE || s_configTab == CONFIG_LOAD)
			{
				window_flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
			}

			ImGui::SetNextWindowPos(ImVec2(160.0f*s_uiScale, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(f32(tabWidth), f32(h)));
			ImGui::SetNextWindowBgAlpha(0.95f);
			ImGui::Begin("##Settings", &active, window_flags);

			ImGui::PushFont(s_dialogFont);
			ImGui::LabelText("##ConfigLabel", "%s", c_configLabels[s_configTab]);
			ImGui::PopFont();
			ImGui::Separator();
			switch (s_configTab)
			{
			case CONFIG_ABOUT:
				configAbout();
				break;
			case CONFIG_GAME:
				configGame();
				break;
			case CONFIG_SAVE:
				configSave();
				break;
			case CONFIG_LOAD:
				configLoad();
				break;
			case CONFIG_INPUT:
				configInput();
				break;
			case CONFIG_GRAPHICS:
				configGraphics();
				break;
			case CONFIG_HUD:
				configHud();
				break;
			case CONFIG_SOUND:
				configSound();
				break;
			};
			renderBackground();

			ImGui::End();
		}
	}

	void manual()
	{
		if (!s_manualDisplayStr)
		{
			// The document has not been loaded yet.
			char path[TFE_MAX_PATH];
			char fileName[TFE_MAX_PATH];
			strcpy(fileName, "Documentation/markdown/TheForceEngineManual.md");
			TFE_Paths::appendPath(PATH_PROGRAM, fileName, path);

			FileStream file;
			if (file.open(path, Stream::MODE_READ))
			{
				const size_t len = file.getSize();
				s_manualDisplayStr = new char[len + 1];
				file.readBuffer(s_manualDisplayStr, (u32)len);
				s_manualDisplayStr[len] = 0;

				file.close();
			}
		}

		if (s_manualDisplayStr) { TFE_Markdown::draw(s_manualDisplayStr); }
	}

	void credits()
	{
		if (!s_creditsDisplayStr)
		{
			// The document has not been loaded yet.
			char path[TFE_MAX_PATH];
			char fileName[TFE_MAX_PATH];
			strcpy(fileName, "Documentation/markdown/credits.md");
			TFE_Paths::appendPath(PATH_PROGRAM, fileName, path);

			FileStream file;
			if (file.open(path, Stream::MODE_READ))
			{
				const size_t len = file.getSize();
				s_creditsDisplayStr = new char[len + 1];
				file.readBuffer(s_creditsDisplayStr, (u32)len);
				s_creditsDisplayStr[len] = 0;

				file.close();
			}
		}

		if (s_creditsDisplayStr) { TFE_Markdown::draw(s_creditsDisplayStr); }
	}

	void configAbout()
	{
		if (!s_aboutDisplayStr)
		{
			// The document has not been loaded yet.
			char path[TFE_MAX_PATH];
			char fileName[TFE_MAX_PATH];
			strcpy(fileName, "Documentation/markdown/theforceengine.md");
			TFE_Paths::appendPath(PATH_PROGRAM, fileName, path);

			FileStream file;
			if (file.open(path, Stream::MODE_READ))
			{
				const size_t len = file.getSize();
				s_aboutDisplayStr = new char[len + 1];
				file.readBuffer(s_aboutDisplayStr, (u32)len);
				s_aboutDisplayStr[len] = 0;

				file.close();
			}
		}

		if (s_aboutDisplayStr) { TFE_Markdown::draw(s_aboutDisplayStr); }
	}

	void configGame()
	{
		TFE_GameHeader* darkForces = TFE_Settings::getGameHeader("Dark Forces");
		TFE_GameHeader* outlaws = TFE_Settings::getGameHeader("Outlaws");

		s32 browseWinOpen = -1;
		
		//////////////////////////////////////////////////////
		// Source Game Data
		//////////////////////////////////////////////////////
		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Game Source Data");
		ImGui::PopFont();

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 1.0f, 0.25f, 1.0f));
		ImGui::LabelText("##ConfigLabel", "The path should contain the source game exe or gob/lab files.");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		ImGui::Text("Dark Forces:"); ImGui::SameLine(100*s_uiScale);
		ImGui::InputText("##DarkForcesSource", darkForces->sourcePath, 1024); ImGui::SameLine();
		if (ImGui::Button("Browse##DarkForces"))
		{
			browseWinOpen = 0;
		}

		ImGui::Text("Outlaws:"); ImGui::SameLine(100*s_uiScale);
		ImGui::InputText("##OutlawsSource", outlaws->sourcePath, 1024); ImGui::SameLine();
		if (ImGui::Button("Browse##Outlaws"))
		{
			browseWinOpen = 1;
		}
		ImGui::Separator();

		//////////////////////////////////////////////////////
		// Current Game
		//////////////////////////////////////////////////////
		const char* c_games[] =
		{
			"Dark Forces",
			// "Outlaws",  -- TODO
		};
		static s32 s_gameIndex = 0;
		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Current Game");
		ImGui::PopFont();

		ImGui::SetNextItemWidth(256.0f);
		ImGui::Combo("##Current Game", &s_gameIndex, c_games, IM_ARRAYSIZE(c_games));

		//////////////////////////////////////////////////////
		// Game Settings
		//////////////////////////////////////////////////////
		ImGui::Separator();

		TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();

		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Dark Forces Settings");
		ImGui::PopFont();

		bool disableFightMusic = gameSettings->df_disableFightMusic;
		if (ImGui::Checkbox("Disable Fight Music", &disableFightMusic))
		{
			gameSettings->df_disableFightMusic = disableFightMusic;
		}

		bool enableAutoaim = gameSettings->df_enableAutoaim;
		if (ImGui::Checkbox("Enable Autoaim", &enableAutoaim))
		{
			gameSettings->df_enableAutoaim = enableAutoaim;
		}

		bool showSecretMsg = gameSettings->df_showSecretFoundMsg;
		if (ImGui::Checkbox("Show Secret Found Message", &showSecretMsg))
		{
			gameSettings->df_showSecretFoundMsg = showSecretMsg;
		}

		bool autorun = gameSettings->df_autorun;
		if (ImGui::Checkbox("Autorun", &autorun))
		{
			gameSettings->df_autorun = autorun;
		}

		bool bobaFettFacePlayer = gameSettings->df_bobaFettFacePlayer;
		if (ImGui::Checkbox("Boba Fett Face Player Fix", &bobaFettFacePlayer))
		{
			gameSettings->df_bobaFettFacePlayer = bobaFettFacePlayer;
		}

		if (s_drawNoGameDataMsg)
		{
			ImGui::Separator();
			ImGui::PushFont(s_dialogFont);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25, 0.25, 1.0f));
			ImGui::LabelText("##ConfigLabel", "Game Source Data cannot be found, please update the game source path above.");
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}

		// File dialogs...
		if (browseWinOpen >= 0)
		{
			char exePath[TFE_MAX_PATH];
			char filePath[TFE_MAX_PATH];
			const char* games[]=
			{
				"Select DARK.GOB",
				"Select OUTLAWS.LAB"
			};
			const std::vector<std::string> filters[]=
			{
				{ "GOB Archive", "*.gob", "Executable", "*.exe" },
				{ "LAB Archive", "*.lab", "Executable", "*.exe" },
			};

			FileResult res = TFE_Ui::openFileDialog(games[browseWinOpen], DEFAULT_PATH, filters[browseWinOpen]);
			if (!res.empty() && !res[0].empty())
			{
				strcpy(exePath, res[0].c_str());
				FileUtil::getFilePath(exePath, filePath);
				FileUtil::fixupPath(filePath);

				if (browseWinOpen == 0)
				{
					// Before accepting this path, verify that some of the required files are here...
					char testFile[TFE_MAX_PATH];
					sprintf(testFile, "%sDARK.GOB", filePath);
					if (FileUtil::exists(testFile))
					{
						strcpy(darkForces->sourcePath, filePath);
						TFE_Paths::setPath(PATH_SOURCE_DATA, darkForces->sourcePath);
					}
					else
					{
						ImGui::OpenPopup("Invalid Source Data");
					}
				}
				else if (browseWinOpen == 0)
				{
					// Before accepting this path, verify that some of the required files are here...
					char testFile[TFE_MAX_PATH];
					sprintf(testFile, "%sOutlaws.lab", filePath);
					if (FileUtil::exists(testFile))
					{
						strcpy(outlaws->sourcePath, filePath);
						// TFE_Paths::setPath(PATH_SOURCE_DATA, outlaws->sourcePath);
					}
					else
					{
						ImGui::OpenPopup("Invalid Source Data");
					}
				}
			}
		}

		if (ImGui::BeginPopupModal("Invalid Source Data", NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Invalid source data path!");
			ImGui::Text("Please select the source game executable or source asset (GOB or LAB).");

			if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
			ImGui::EndPopup();
		}
	}

	///////////////////////////////////////////////////////////////////////////////
	// Save / Load UI
	///////////////////////////////////////////////////////////////////////////////
	static std::vector<TFE_SaveSystem::SaveHeader> s_saveDir;
	static TextureGpu* s_saveImageView = nullptr;
	static s32 s_selectedSave = -1;
	static s32 s_selectedSaveSlot = -1;
	static bool s_hasQuicksave = false;

	static char s_newSaveName[256];
	static char s_fileName[TFE_MAX_PATH];
	static char s_saveGameConfirmMsg[TFE_MAX_PATH];
	static bool s_popupOpen = false;
	static bool s_popupSetFocus = false;

	void clearSaveImage()
	{
		u32 zero[TFE_SaveSystem::SAVE_IMAGE_WIDTH * TFE_SaveSystem::SAVE_IMAGE_HEIGHT];
		memset(zero, 0, sizeof(u32) * TFE_SaveSystem::SAVE_IMAGE_WIDTH * TFE_SaveSystem::SAVE_IMAGE_HEIGHT);
		s_saveImageView->update(zero, TFE_SaveSystem::SAVE_IMAGE_WIDTH * TFE_SaveSystem::SAVE_IMAGE_HEIGHT * 4);
	}

	void updateSaveImage(s32 index)
	{
		s_saveImageView->update(s_saveDir[index].imageData, TFE_SaveSystem::SAVE_IMAGE_WIDTH * TFE_SaveSystem::SAVE_IMAGE_HEIGHT * 4);
	}

	void openSaveNameEditPopup(const char* prevName)
	{
		if (prevName[0] == 0)
		{
			sprintf(s_saveGameConfirmMsg, "Create New Save?###SaveConfirm");
		}
		else
		{
			sprintf(s_saveGameConfirmMsg, "Overwrite Save '%s'?###SaveConfirm", prevName);
		}

		ImGui::OpenPopup(s_saveGameConfirmMsg);
		s_popupOpen = true;
		s_popupSetFocus = true;
	}

	void closeSaveNameEditPopup()
	{
		s_popupOpen = false;
		s_popupSetFocus = false;
		ImGui::CloseCurrentPopup();
	}

	void saveConfirmed()
	{
		s_saveLoadSetupRequired = true;
		TFE_SaveSystem::postSaveRequest(s_fileName, s_newSaveName, 3);
		closeSaveNameEditPopup();
	}

	void exitLoadSaveMenu()
	{
		s_subUI = FEUI_NONE;
		s_appState = s_menuRetState;
		s_drawNoGameDataMsg = false;
		TFE_Input::enableRelativeMode(s_relativeMode);
	}

	void configSaveLoadBegin(bool save)
	{
		s_selectedSave = max(0, s_selectedSaveSlot - (save ? 1 : 0));

		if (!s_saveImageView)
		{
			s_saveImageView = TFE_RenderBackend::createTexture(TFE_SaveSystem::SAVE_IMAGE_WIDTH, TFE_SaveSystem::SAVE_IMAGE_HEIGHT, 4);
		}
		TFE_SaveSystem::populateSaveDirectory(s_saveDir);
		s_hasQuicksave = (!s_saveDir.empty() && strcasecmp(s_saveDir[0].saveName, "Quicksave") == 0);

		if (!s_saveDir.empty() && (s_selectedSave > 0 || !save))
		{
			updateSaveImage(s_selectedSave);
		}
		else
		{
			clearSaveImage();
		}

		s_popupOpen = false;
		s_saveLoadSetupRequired = false;
		s_popupSetFocus = false;
	}
		
	void saveLoadDialog(bool save)
	{
		if (s_saveLoadSetupRequired)
		{
			configSaveLoadBegin(save);
		}

		// Create the current display info to adjust menu sizes.
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		f32 leftColumn = displayInfo.width < 1200 ? 196.0f*s_uiScale : 256.0f*s_uiScale;
		f32 rightColumn = leftColumn + ((f32)TFE_SaveSystem::SAVE_IMAGE_WIDTH + 32.0f)*s_uiScale;
		const s32 listOffset = save ? 1 : 0;

		// Left Column
		ImGui::SetNextWindowPos(ImVec2(leftColumn, floorf(displayInfo.height * 0.25f)));
		ImGui::BeginChild("##ImageAndInfo");
		{
			// Image
			ImVec2 size((f32)TFE_SaveSystem::SAVE_IMAGE_WIDTH, (f32)TFE_SaveSystem::SAVE_IMAGE_HEIGHT);
			size.x *= s_uiScale;
			size.y *= s_uiScale;

			ImGui::Image(TFE_RenderBackend::getGpuPtr(s_saveImageView), size,
				ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

			// Info
			size.y = 140 * s_uiScale;
			ImGui::BeginChild("##InfoWithBorder", size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			{
				if (!s_saveDir.empty() && (s_selectedSave > 0 || !save))
				{
					ImGui::PushFont(s_dialogFont);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

					char textBuffer[TFE_MAX_PATH];
					sprintf(textBuffer, "Game: Dark Forces\nTime: %s\nLevel: %s\nMods: %s\n", s_saveDir[s_selectedSave - listOffset].dateTime,
						s_saveDir[s_selectedSave - listOffset].levelName, s_saveDir[s_selectedSave - listOffset].modNames);
					ImGui::InputTextMultiline("##Info", textBuffer, strlen(textBuffer) + 1, size, ImGuiInputTextFlags_ReadOnly);

					ImGui::PopFont();
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::PushFont(s_dialogFont);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

					char textBuffer[TFE_MAX_PATH];
					sprintf(textBuffer, "Game: Dark Forces\nTime:\n\nLevel:\nMods:\n");
					ImGui::InputTextMultiline("##Info", textBuffer, strlen(textBuffer) + 1, size, ImGuiInputTextFlags_ReadOnly);

					ImGui::PopFont();
					ImGui::PopStyleColor();
				}
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();

		// Right Column
		f32 rwidth  = 1024.0f * s_uiScale;
		if (rightColumn + rwidth > displayInfo.width - 64.0f)
		{
			rwidth = displayInfo.width - 64.0f - rightColumn;
		}

		f32 rheight = displayInfo.height - 80.0f;
		ImGui::SetNextWindowPos(ImVec2(rightColumn, 64.0f*s_uiScale));
		ImGui::BeginChild("##FileList", ImVec2(rwidth, rheight), true);
		{
			const size_t count = s_saveDir.size() + size_t(listOffset);
			const TFE_SaveSystem::SaveHeader* header = s_saveDir.data();
			char newSave[] = "<Create Save>";
			ImVec2 buttonSize(rwidth - 2.0f, floorf(20 * s_uiScale + 0.5f));
			ImGui::PushFont(s_dialogFont);
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			s32 prevSelected = s_selectedSave;
			for (size_t i = 0; i < count; i++)
			{
				bool selected = i == s_selectedSave;
				const char* saveName;
				if (save && i == 0)
				{
					saveName = newSave;
				}
				else
				{
					saveName = header[i - listOffset].saveName;
				}

				if (ImGui::Selectable(saveName, selected, ImGuiSelectableFlags_None, buttonSize))
				{
					if (!s_popupOpen)
					{
						s_selectedSave = s32(i);
						s_selectedSaveSlot = s_selectedSave - listOffset;
						bool shouldExit = false;
						if (save)
						{
							if (s_selectedSave == 0)
							{
								s_selectedSaveSlot = (s32)s_saveDir.size();
								// If no quicksave exists, skip over it when generating the name.
								const s32 saveIndex = s_selectedSaveSlot + (s_hasQuicksave ? 0 : 1);
								TFE_SaveSystem::getSaveFilenameFromIndex(saveIndex, s_fileName);

								s_newSaveName[0] = 0;
								openSaveNameEditPopup(s_newSaveName);
							}
							else
							{
								strcpy(s_fileName, s_saveDir[s_selectedSaveSlot].fileName);
								strcpy(s_newSaveName, s_saveDir[s_selectedSaveSlot].saveName);

								// You cannot change the quick save name.
								if (s_selectedSaveSlot != 0)
								{
									openSaveNameEditPopup(s_newSaveName);
								}
								else
								{
									// Just save inline.
									shouldExit = true;
									s_saveLoadSetupRequired = true;
									TFE_SaveSystem::postSaveRequest(s_fileName, s_newSaveName, 3);
								}
							}
						}
						else
						{
							TFE_SaveSystem::postLoadRequest(s_saveDir[s_selectedSaveSlot].fileName);
							shouldExit = true;
						}

						if (shouldExit)
						{
							exitLoadSaveMenu();
						}
					}
				}
				else if ((ImGui::IsItemHovered() || ImGui::IsItemClicked()) && !s_popupOpen)
				{
					s_selectedSave = s32(i);
				}
			}
			if (prevSelected != s_selectedSave)
			{
				s32 index = s_selectedSave - listOffset;
				if (index >= 0)
				{
					updateSaveImage(s_selectedSave - listOffset);
				}
				else
				{
					clearSaveImage();
				}
			}
			prevSelected = s_selectedSave;

			if (ImGui::BeginPopupModal(s_saveGameConfirmMsg, NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				bool shouldExit = false;
				if (s_popupSetFocus)
				{
					ImGui::SetKeyboardFocusHere();
					s_popupSetFocus = false;
				}
				ImGui::SetNextItemWidth(768 * s_uiScale);
				if (ImGui::InputText("###SaveNameText", s_newSaveName, TFE_SaveSystem::SAVE_MAX_NAME_LEN, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					shouldExit = true;
					saveConfirmed();
				}
				if (ImGui::Button("OK", ImVec2(120, 0)))
				{
					shouldExit = true;
					saveConfirmed();
				}
				ImGui::SameLine(0.0f, 32.0f);
				if (ImGui::Button("Cancel", ImVec2(120, 0)))
				{
					shouldExit = false;
					closeSaveNameEditPopup();
				}
				ImGui::EndPopup();

				if (shouldExit)
				{
					exitLoadSaveMenu();
				}
			}

			ImGui::PopFont();
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
		s_selectedSaveSlot = s_selectedSave - listOffset;
	}

	void configSave()
	{
		if (!s_canSave)
		{
			ImGui::LabelText("###Msg", "Cannot save the game at this time.");
			if (ImGui::Button("Return", ImVec2(120.0f*s_uiScale, 0.0f)))
			{
				exitLoadSaveMenu();
			}
		}
		else
		{
			saveLoadDialog(true);
		}
	}

	void configLoad()
	{
		saveLoadDialog(false);
	}
		
	void getBindingString(InputBinding* binding, char* inputName)
	{
		if (binding->type == ITYPE_KEYBOARD)
		{
			if (binding->keyMod)
			{
				sprintf_s(inputName, 64, "%s + %s", TFE_Input::getKeyboardModifierName(binding->keyMod), TFE_Input::getKeyboardName(binding->keyCode));
			}
			else
			{
				strcpy_s(inputName, 64, TFE_Input::getKeyboardName(binding->keyCode));
			}
		}
		else if (binding->type == ITYPE_MOUSE)
		{
			strcpy_s(inputName, 64, TFE_Input::getMouseButtonName(binding->mouseBtn));
		}
		else if (binding->type == ITYPE_MOUSEWHEEL)
		{
			strcpy_s(inputName, 64, TFE_Input::getMouseWheelName(binding->mouseWheel));
		}
		else if (binding->type == ITYPE_CONTROLLER)
		{
			strcpy_s(inputName, 64, TFE_Input::getControllButtonName(binding->ctrlBtn));
		}
		else if (binding->type == ITYPE_CONTROLLER_AXIS)
		{
			strcpy_s(inputName, 64, TFE_Input::getControllerAxisName(binding->axis));
		}
	}

	static s32 s_keyIndex = -1;
	static s32 s_keySlot = -1;
	static KeyModifier s_keyMod = KEYMOD_NONE;

	void inputMapping(const char* name, InputAction action)
	{
		u32 indices[2];
		u32 count = inputMapping_getBindingsForAction(action, indices, 2);

		char inputName1[256] = "##Input1";
		char inputName2[256] = "##Input2";

		ImGui::LabelText("##ConfigLabel", "%s", name); ImGui::SameLine(132*s_uiScale);
		if (count >= 1)
		{
			InputBinding* binding = inputMapping_getBindingByIndex(indices[0]);
			getBindingString(binding, inputName1);
			strcat(inputName1, "##Input1");
			strcat(inputName1, name);
		}
		else
		{
			strcat(inputName1, name);
		}

		if (count >= 2)
		{
			InputBinding* binding = inputMapping_getBindingByIndex(indices[1]);
			getBindingString(binding, inputName2);
			strcat(inputName2, "##Input2");
			strcat(inputName2, name);
		}
		else
		{
			strcat(inputName2, name);
		}

		if (ImGui::Button(inputName1, ImVec2(120.0f*s_uiScale, 0.0f)))
		{
			// Press Key Popup.
			s_keyIndex = s32(action);
			s_keySlot = 0;
			s_keyMod = TFE_Input::getKeyModifierDown();
			ImGui::OpenPopup("##ChangeBinding");
		}
		ImGui::SameLine();
		if (ImGui::Button(inputName2, ImVec2(120.0f*s_uiScale, 0.0f)))
		{
			// Press Key Popup.
			s_keyIndex = s32(action);
			s_keySlot = 1;
			s_keyMod = TFE_Input::getKeyModifierDown();
			ImGui::OpenPopup("##ChangeBinding");
		}
	}

	bool uiControlsEnabled()
	{
		return (!s_bindingPopupOpen || s_configTab != CONFIG_INPUT) || s_subUI == FEUI_NONE;
	}

	void configInput()
	{
		const u32 window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
		f32 scroll = ImGui::GetScrollY();
		f32 yNext = 45.0f;

		s_inputConfig = inputMapping_get();

		ImGui::SetNextWindowPos(ImVec2(165.0f*s_uiScale, yNext - scroll));
		if (ImGui::Button("Reset To Defaults"))
		{
			inputMapping_resetToDefaults();
		}
		yNext += 32.0f*s_uiScale;

		ImGui::SetNextWindowPos(ImVec2(165.0f*s_uiScale, yNext - scroll));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.0f, 0.0f });
		if (ImGui::BeginChild("Controller Options", ImVec2(390.0f*s_uiScale, (s_controllerWinOpen ? 480.0f : 29.0f)*s_uiScale), true, window_flags))
		{
			if (ImGui::Button("Controller Options", ImVec2(370.0f*s_uiScale, 0.0f)))
			{
				s_controllerWinOpen = !s_controllerWinOpen;
			}
			ImGui::PopStyleVar();

			if (s_controllerWinOpen)
			{
				ImGui::Spacing();
				bool controllerEnable = (s_inputConfig->controllerFlags & CFLAG_ENABLE) != 0;
				if (ImGui::Checkbox("Enable", &controllerEnable))
				{
					if (controllerEnable) { s_inputConfig->controllerFlags |=  CFLAG_ENABLE; }
					                 else { s_inputConfig->controllerFlags &= ~CFLAG_ENABLE; }
				}

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "Controller Axis");
				ImGui::PopFont();

				ImGui::LabelText("##ConfigLabel", "Horizontal Turn"); ImGui::SameLine(175*s_uiScale);
				ImGui::SetNextItemWidth(196*s_uiScale);
				ImGui::Combo("##CtrlLookHorz", (s32*)&s_inputConfig->axis[AA_LOOK_HORZ], c_axisBinding, IM_ARRAYSIZE(c_axisBinding));

				ImGui::LabelText("##ConfigLabel", "Vertical Look"); ImGui::SameLine(175*s_uiScale);
				ImGui::SetNextItemWidth(196*s_uiScale);
				ImGui::Combo("##CtrlLookVert", (s32*)&s_inputConfig->axis[AA_LOOK_VERT], c_axisBinding, IM_ARRAYSIZE(c_axisBinding));

				ImGui::LabelText("##ConfigLabel", "Move"); ImGui::SameLine(175*s_uiScale);
				ImGui::SetNextItemWidth(196*s_uiScale);
				ImGui::Combo("##CtrlStrafe", (s32*)&s_inputConfig->axis[AA_STRAFE], c_axisBinding, IM_ARRAYSIZE(c_axisBinding));

				ImGui::LabelText("##ConfigLabel", "Strafe"); ImGui::SameLine(175*s_uiScale);
				ImGui::SetNextItemWidth(196*s_uiScale);
				ImGui::Combo("##CtrlMove", (s32*)&s_inputConfig->axis[AA_MOVE], c_axisBinding, IM_ARRAYSIZE(c_axisBinding));

				ImGui::Separator();

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "Sensitivity");
				ImGui::PopFont();

				ImGui::LabelText("##ConfigLabel", "Left Stick");
				ImGui::SetNextItemWidth(196*s_uiScale);
				ImGui::SliderFloat("##LeftSensitivity", &s_inputConfig->ctrlSensitivity[0], 0.0f, 4.0f); ImGui::SameLine(220*s_uiScale);
				ImGui::SetNextItemWidth(64*s_uiScale);
				ImGui::InputFloat("##LeftSensitivityInput", &s_inputConfig->ctrlSensitivity[0]);

				ImGui::LabelText("##ConfigLabel", "Right Stick");
				ImGui::SetNextItemWidth(196*s_uiScale);
				ImGui::SliderFloat("##RightSensitivity", &s_inputConfig->ctrlSensitivity[1], 0.0f, 4.0f); ImGui::SameLine(220*s_uiScale);
				ImGui::SetNextItemWidth(64*s_uiScale);
				ImGui::InputFloat("##RightSensitivityInput", &s_inputConfig->ctrlSensitivity[1]);

				ImGui::LabelText("##ConfigLabel", "Left Deadzone");
				ImGui::SetNextItemWidth(196 * s_uiScale);
				ImGui::SliderFloat("##LeftDeadzone", &s_inputConfig->ctrlDeadzone[0], 0.0f, 0.5f); ImGui::SameLine(220 * s_uiScale);
				ImGui::SetNextItemWidth(64 * s_uiScale);
				ImGui::InputFloat("##LeftDeadzoneInput", &s_inputConfig->ctrlDeadzone[0]);

				ImGui::LabelText("##ConfigLabel", "Right Deadzone");
				ImGui::SetNextItemWidth(196 * s_uiScale);
				ImGui::SliderFloat("##RightDeadzone", &s_inputConfig->ctrlDeadzone[1], 0.0f, 0.5f); ImGui::SameLine(220 * s_uiScale);
				ImGui::SetNextItemWidth(64 * s_uiScale);
				ImGui::InputFloat("##RightDeadzoneInput", &s_inputConfig->ctrlDeadzone[1]);

				ImGui::Separator();

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "Invert Axis");
				ImGui::PopFont();

				bool invertLeftHorz = (s_inputConfig->controllerFlags & CFLAG_INVERT_LEFT_HORZ) != 0;
				bool invertLeftVert = (s_inputConfig->controllerFlags & CFLAG_INVERT_LEFT_VERT) != 0;
				ImGui::LabelText("##ConfigLabel", "Left Stick Invert: "); ImGui::SameLine(175*s_uiScale);
				ImGui::Checkbox("Horizontal##Left", &invertLeftHorz); ImGui::SameLine();
				ImGui::Checkbox("Vertical##Left", &invertLeftVert);
				if (invertLeftHorz) { s_inputConfig->controllerFlags |=  CFLAG_INVERT_LEFT_HORZ; }
				else                { s_inputConfig->controllerFlags &= ~CFLAG_INVERT_LEFT_HORZ; }
				if (invertLeftVert) { s_inputConfig->controllerFlags |=  CFLAG_INVERT_LEFT_VERT; }
				               else { s_inputConfig->controllerFlags &= ~CFLAG_INVERT_LEFT_VERT; }
				
				bool invertRightHorz = (s_inputConfig->controllerFlags & CFLAG_INVERT_RIGHT_HORZ) != 0;
				bool invertRightVert = (s_inputConfig->controllerFlags & CFLAG_INVERT_RIGHT_VERT) != 0;
				ImGui::LabelText("##ConfigLabel", "Right Stick Invert: "); ImGui::SameLine(175*s_uiScale);
				ImGui::Checkbox("Horizontal##Right", &invertRightHorz); ImGui::SameLine();
				ImGui::Checkbox("Vertical##Right", &invertRightVert);
				if (invertRightHorz) { s_inputConfig->controllerFlags |=  CFLAG_INVERT_RIGHT_HORZ; }
				                else { s_inputConfig->controllerFlags &= ~CFLAG_INVERT_RIGHT_HORZ; }
				if (invertRightVert) { s_inputConfig->controllerFlags |=  CFLAG_INVERT_RIGHT_VERT; }
				                else { s_inputConfig->controllerFlags &= ~CFLAG_INVERT_RIGHT_VERT; }

				yNext += 480.0f*s_uiScale;
			}
			else
			{
				yNext += 29.0f*s_uiScale;
			}
		}
		else
		{
			yNext += (s_controllerWinOpen ? 480.0f : 29.0f)*s_uiScale;
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
				
		ImGui::SetNextWindowPos(ImVec2(165.0f*s_uiScale, yNext - scroll));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.0f, 0.0f });
		if (ImGui::BeginChild("##Mouse Options", ImVec2(390.0f*s_uiScale, (s_mouseWinOpen ? 250.0f : 29.0f)*s_uiScale), true, window_flags))
		{
			if (ImGui::Button("Mouse Options", ImVec2(370.0f*s_uiScale, 0.0f)))
			{
				s_mouseWinOpen = !s_mouseWinOpen;
			}
			ImGui::PopStyleVar();

			if (s_mouseWinOpen)
			{
				ImGui::Spacing();

				ImGui::LabelText("##ConfigLabel", "Mouse Mode"); ImGui::SameLine(100*s_uiScale);
				ImGui::SetNextItemWidth(160*s_uiScale);
				ImGui::Combo("##MouseMode", (s32*)&s_inputConfig->mouseMode, c_mouseMode, IM_ARRAYSIZE(c_mouseMode));

				ImGui::Separator();

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "Sensitivity");
				ImGui::PopFont();

				ImGui::LabelText("##ConfigLabel", "Horizontal");
				ImGui::SetNextItemWidth(196*s_uiScale);
				ImGui::SliderFloat("##HorzSensitivity", &s_inputConfig->mouseSensitivity[0], 0.0f, 4.0f); ImGui::SameLine(220*s_uiScale);
				ImGui::SetNextItemWidth(64*s_uiScale);
				ImGui::InputFloat("##HorzSensitivityInput", &s_inputConfig->mouseSensitivity[0]);

				ImGui::LabelText("##ConfigLabel", "Vertical");
				ImGui::SetNextItemWidth(196*s_uiScale);
				ImGui::SliderFloat("##VertSensitivity", &s_inputConfig->mouseSensitivity[1], 0.0f, 4.0f); ImGui::SameLine(220*s_uiScale);
				ImGui::SetNextItemWidth(64*s_uiScale);
				ImGui::InputFloat("##VertSensitivityInput", &s_inputConfig->mouseSensitivity[1]);

				ImGui::Separator();

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "Mouse Invert");
				ImGui::PopFont();

				bool invertHorz = (s_inputConfig->mouseFlags & MFLAG_INVERT_HORZ) != 0;
				bool invertVert = (s_inputConfig->mouseFlags & MFLAG_INVERT_VERT) != 0;
				ImGui::Checkbox("Horizontal##Mouse", &invertHorz); ImGui::SameLine();
				ImGui::Checkbox("Vertical##Mouse", &invertVert);
				if (invertHorz) { s_inputConfig->mouseFlags |=  MFLAG_INVERT_HORZ; }
				           else { s_inputConfig->mouseFlags &= ~MFLAG_INVERT_HORZ; }
				if (invertVert) { s_inputConfig->mouseFlags |=  MFLAG_INVERT_VERT; }
				           else { s_inputConfig->mouseFlags &= ~MFLAG_INVERT_VERT; }

				yNext += 250.0f*s_uiScale;
			}
			else
			{
				yNext += 29.0f*s_uiScale;
			}
		}
		else
		{
			yNext += (s_controllerWinOpen ? 250.0f : 29.0f)*s_uiScale;
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
		ImGui::SetNextWindowPos(ImVec2(165.0f*s_uiScale, yNext - scroll));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.0f, 0.0f });
		f32 inputMappingHeight = 1516.0f*s_uiScale;
		if (ImGui::BeginChild("##Input Mapping", ImVec2(390.0f*s_uiScale, s_inputMappingOpen ? inputMappingHeight : 29.0f*s_uiScale), true, window_flags))
		{
			if (ImGui::Button("Input Mapping", ImVec2(370.0f*s_uiScale, 0.0f)))
			{
				s_inputMappingOpen = !s_inputMappingOpen;
			}
			ImGui::PopStyleVar();

			if (s_inputMappingOpen)
			{
				ImGui::Spacing();

				ImGui::LabelText("##ConfigLabel", "Set: Left click and press key/button");
				ImGui::LabelText("##ConfigLabel", " Hold key modifier before the click");
				ImGui::LabelText("##ConfigLabel", " Key modifiers: Shift, Ctrl, Alt");
				ImGui::LabelText("##ConfigLabel", "Clear: Repeat binding");

				ImGui::Separator();

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "System");
				ImGui::PopFont();

				inputMapping("Console Toggle", IAS_CONSOLE);
				inputMapping("System Menu", IAS_SYSTEM_MENU);
				inputMapping("Quick Save",  IAS_QUICK_SAVE);
				inputMapping("Quick Load",  IAS_QUICK_LOAD);

				ImGui::Separator();

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "Dark Forces - General");
				ImGui::PopFont();

				inputMapping("Menu Toggle",       IADF_MENU_TOGGLE);
				inputMapping("PDA Toggle",        IADF_PDA_TOGGLE);
				inputMapping("Night Vision",      IADF_NIGHT_VISION_TOG);
				inputMapping("Cleats",            IADF_CLEATS_TOGGLE);
				inputMapping("Gas Mask",          IADF_GAS_MASK_TOGGLE);
				inputMapping("Head Lamp",         IADF_HEAD_LAMP_TOGGLE);
				inputMapping("Headwave",          IADF_HEADWAVE_TOGGLE);
				inputMapping("HUD Toggle",        IADF_HUD_TOGGLE);
				inputMapping("Holster Weapon",    IADF_HOLSTER_WEAPON);
				inputMapping("Automount Toggle",  IADF_AUTOMOUNT_TOGGLE);
				inputMapping("Cycle Prev Weapon", IADF_CYCLEWPN_PREV);
				inputMapping("Cycle Next Weapon", IADF_CYCLEWPN_NEXT);
				inputMapping("Prev Weapon",       IADF_WPN_PREV);
				inputMapping("Pause",             IADF_PAUSE);
				inputMapping("Automap",           IADF_AUTOMAP);
								
				ImGui::Separator();

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "Dark Forces - Automap");
				ImGui::PopFont();

				inputMapping("Zoom In",      IADF_MAP_ZOOM_IN);
				inputMapping("Zoom Out",     IADF_MAP_ZOOM_OUT);
				inputMapping("Enable Scroll",IADF_MAP_ENABLE_SCROLL);
				inputMapping("Scroll Up",    IADF_MAP_SCROLL_UP);
				inputMapping("Scroll Down",  IADF_MAP_SCROLL_DN);
				inputMapping("Scroll Left",  IADF_MAP_SCROLL_LT);
				inputMapping("Scroll Right", IADF_MAP_SCROLL_RT);
				inputMapping("Layer Up",     IADF_MAP_LAYER_UP);
				inputMapping("Layer Down",   IADF_MAP_LAYER_DN);

				ImGui::Separator();

				ImGui::PushFont(s_dialogFont);
				ImGui::LabelText("##ConfigLabel", "Dark Forces - Player");
				ImGui::PopFont();

				inputMapping("Forward",           IADF_FORWARD);
				inputMapping("Backward",          IADF_BACKWARD);
				inputMapping("Strafe Left",       IADF_STRAFE_LT);
				inputMapping("Strafe Right",      IADF_STRAFE_RT);
				inputMapping("Turn Left",         IADF_TURN_LT);
				inputMapping("Turn Right",        IADF_TURN_RT);
				inputMapping("Look Up",           IADF_LOOK_UP);
				inputMapping("Look Down",         IADF_LOOK_DN);
				inputMapping("Center View",       IADF_CENTER_VIEW);
				inputMapping("Run",               IADF_RUN);
				inputMapping("Walk Slowly",       IADF_SLOW);
				inputMapping("Crouch",            IADF_CROUCH);
				inputMapping("Jump",              IADF_JUMP);
				inputMapping("Use",               IADF_USE);
				inputMapping("Primary Fire",      IADF_PRIMARY_FIRE);
				inputMapping("Secondary Fire",    IADF_SECONDARY_FIRE);
				inputMapping("Fists",             IADF_WEAPON_1);
				inputMapping("Bryar Pistol",      IADF_WEAPON_2);
				inputMapping("E-11 Blaster",      IADF_WEAPON_3);
				inputMapping("Thermal Detonator", IADF_WEAPON_4);
				inputMapping("Repeater Gun",      IADF_WEAPON_5);
				inputMapping("Fusion Cutter",     IADF_WEAPON_6);
				inputMapping("I.M. Mines",        IADF_WEAPON_7);
				inputMapping("Mortar Gun",        IADF_WEAPON_8);
				inputMapping("Concussion Rifle",  IADF_WEAPON_9);
				inputMapping("Assault Cannon",    IADF_WEAPON_10);
								
				if (ImGui::BeginPopupModal("##ChangeBinding", NULL, ImGuiWindowFlags_AlwaysAutoResize))
				{
					s_bindingPopupOpen = true;

					ImGui::Text("Press a key, controller button or mouse button.");
					ImGui::Text("(To clear, repeat the current binding)");

					KeyboardCode key = TFE_Input::getKeyPressed();
					Button ctrlBtn = TFE_Input::getControllerButtonPressed();
					MouseButton mouseBtn = TFE_Input::getMouseButtonPressed();
					Axis ctrlAnalog = TFE_Input::getControllerAnalogDown();

					s32 wdx, wdy;
					TFE_Input::getMouseWheel(&wdx, &wdy);

					u32 indices[2];
					s32 count = (s32)inputMapping_getBindingsForAction(InputAction(s_keyIndex), indices, 2);

					InputBinding* binding;
					InputBinding newBinding;
					bool existingBinding = true;
					if (s_keySlot >= count)
					{
						binding = &newBinding;
						existingBinding = false;
					}
					else
					{
						binding = inputMapping_getBindingByIndex(indices[s_keySlot]);
					}

					bool setBinding = false;
					if (key != KEY_UNKNOWN)
					{
						setBinding = true;

						if (existingBinding && binding->action == InputAction(s_keyIndex) && binding->keyCode == key
							&& binding->keyMod == s_keyMod && binding->type == ITYPE_KEYBOARD)
						{
							inputMapping_removeBinding(indices[s_keySlot]);
						}
						else
						{
							memset(binding, 0, sizeof(InputBinding));
							binding->action = InputAction(s_keyIndex);
							binding->keyCode = key;
							binding->keyMod = s_keyMod;
							binding->type = ITYPE_KEYBOARD;
						}
					}
					else if (ctrlBtn != CONTROLLER_BUTTON_UNKNOWN)
					{
						setBinding = true;
						if (existingBinding && binding->action == InputAction(s_keyIndex) && binding->ctrlBtn == ctrlBtn
							&& binding->type == ITYPE_CONTROLLER)
						{
							inputMapping_removeBinding(indices[s_keySlot]);
						}
						else
						{
							memset(binding, 0, sizeof(InputBinding));
							binding->action = InputAction(s_keyIndex);
							binding->ctrlBtn = ctrlBtn;
							binding->type = ITYPE_CONTROLLER;
						}
					}
					else if (mouseBtn != MBUTTON_UNKNOWN)
					{
						setBinding = true;

						if (existingBinding && binding->action == InputAction(s_keyIndex) && binding->mouseBtn == mouseBtn
							&& binding->type == ITYPE_MOUSE)
						{
							inputMapping_removeBinding(indices[s_keySlot]);
						}
						else
						{
							memset(binding, 0, sizeof(InputBinding));
							binding->action = InputAction(s_keyIndex);
							binding->mouseBtn = mouseBtn;
							binding->type = ITYPE_MOUSE;
						}
					}
					else if (ctrlAnalog != AXIS_UNKNOWN)
					{
						setBinding = true;

						if (existingBinding && binding->action == InputAction(s_keyIndex) && binding->axis == ctrlAnalog
							&& binding->type == ITYPE_CONTROLLER_AXIS)
						{
							inputMapping_removeBinding(indices[s_keySlot]);
						}
						else
						{
							memset(binding, 0, sizeof(InputBinding));
							binding->action = InputAction(s_keyIndex);
							binding->axis = ctrlAnalog;
							binding->type = ITYPE_CONTROLLER_AXIS;
						}
					}
					else if (wdx || wdy)
					{
						setBinding = true;
						MouseWheel mw;
						if (wdx < 0)
						{
							mw = MOUSEWHEEL_LEFT;
						}
						else if (wdx > 0)
						{
							mw = MOUSEWHEEL_RIGHT;
						}
						else if (wdy > 0)
						{
							mw = MOUSEWHEEL_UP;
						}
						else if (wdy < 0)
						{
							mw = MOUSEWHEEL_DOWN;
						}

						if (existingBinding && binding->action == InputAction(s_keyIndex) && binding->mouseWheel == mw
							&& binding->type == ITYPE_MOUSEWHEEL)
						{
							inputMapping_removeBinding(indices[s_keySlot]);
						}
						else
						{
							memset(binding, 0, sizeof(InputBinding));
							binding->action = InputAction(s_keyIndex);
							binding->type = ITYPE_MOUSEWHEEL;
							binding->mouseWheel = mw;
						}
					}

					if (setBinding)
					{
						if (s_keySlot >= count)
						{
							inputMapping_addBinding(binding);
						}

						s_keyIndex = -1;
						s_keySlot = -1;
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}
				else
				{
					s_bindingPopupOpen = false;
				}
			}
		}
		else
		{
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::SetCursorPosY(yNext + (s_inputMappingOpen ? inputMappingHeight : 29.0f*s_uiScale));
	}
		
	void configGraphics()
	{
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		TFE_Settings_Window* window = TFE_Settings::getWindowSettings();
		TFE_Settings_Game* game = TFE_Settings::getGameSettings();

		//////////////////////////////////////////////////////
		// Window Settings.
		//////////////////////////////////////////////////////
		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Window");
		ImGui::PopFont();

		bool fullscreen = window->fullscreen;
		bool windowed = !fullscreen;
		bool vsync = TFE_System::getVSync();
		if (ImGui::Checkbox("Fullscreen", &fullscreen))
		{
			windowed = !fullscreen;
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Vsync", &vsync))
		{
			TFE_System::setVsync(vsync);
		}
		if (ImGui::Checkbox("Windowed", &windowed))
		{
			fullscreen = !windowed;
		}
		if (fullscreen != window->fullscreen)
		{
			TFE_RenderBackend::enableFullscreen(fullscreen);
		}
		window->fullscreen = fullscreen;
		ImGui::Separator();

		//////////////////////////////////////////////////////
		// Resolution
		//////////////////////////////////////////////////////
		bool widescreen = graphics->widescreen;

		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Virtual Resolution");
		ImGui::PopFont();

		ImGui::LabelText("##ConfigLabel", "Standard Resolution:"); ImGui::SameLine(150 * s_uiScale);
		ImGui::SetNextItemWidth(196);
		ImGui::Combo("##Resolution", &s_resIndex, widescreen ? c_resolutionsWide : c_resolutions, IM_ARRAYSIZE(c_resolutions));
		if (s_resIndex == TFE_ARRAYSIZE(c_resolutionDim))
		{
			DisplayInfo displayInfo;
			TFE_RenderBackend::getDisplayInfo(&displayInfo);
			graphics->gameResolution.x = displayInfo.width;
			graphics->gameResolution.z = displayInfo.height;
			graphics->widescreen = true;
			widescreen = true;
		}
		else if (s_resIndex == TFE_ARRAYSIZE(c_resolutionDim) + 1)
		{
			ImGui::LabelText("##ConfigLabel", "Custom:"); ImGui::SameLine(100 * s_uiScale);
			ImGui::SetNextItemWidth(196);
			ImGui::InputInt2("##CustomInput", graphics->gameResolution.m);

			graphics->gameResolution.x = max(graphics->gameResolution.x, 10);
			graphics->gameResolution.z = max(graphics->gameResolution.z, 10);
		}
		else
		{
			graphics->gameResolution = c_resolutionDim[s_resIndex];
		}

		ImGui::Checkbox("Widescreen", &widescreen);
		if (widescreen != graphics->widescreen)
		{
			graphics->widescreen = widescreen;
			if (s_resIndex == TFE_ARRAYSIZE(c_resolutionDim) && !widescreen)
			{
				// Find the closest match.
				s32 height = graphics->gameResolution.z;
				s32 diff = INT_MAX;
				s32 index = -1;
				for (s32 i = 0; i < TFE_ARRAYSIZE(c_resolutionDim); i++)
				{
					s32 curDiff = TFE_Jedi::abs(height - c_resolutionDim[i].z);
					if (c_resolutionDim[i].z <= height && curDiff < diff)
					{
						index = i;
						diff = curDiff;
					}
				}
				if (index >= 0)
				{
					graphics->gameResolution = c_resolutionDim[index];
					s_resIndex = index;
				}
			}
		}
		ImGui::Separator();

		//////////////////////////////////////////////////////
		// Renderer
		//////////////////////////////////////////////////////
		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Rendering");
		ImGui::PopFont();

		ImGui::LabelText("##ConfigLabel", "Renderer:"); ImGui::SameLine(75 * s_uiScale);
		ImGui::SetNextItemWidth(196 * s_uiScale);
		ImGui::Combo("##Renderer", &graphics->rendererIndex, c_renderer, IM_ARRAYSIZE(c_renderer));
		if (graphics->rendererIndex == 0)
		{
			// Software
			graphics->asyncFramebuffer = true;
			graphics->gpuColorConvert = true;
			ImGui::Checkbox("Extend Adjoin/Portal Limits", &graphics->extendAjoinLimits);
		}
		else if (graphics->rendererIndex == 1)
		{
			const f32 comboOffset = floorf(170 * s_uiScale);

			// Hardware

			// Pitch Limit
			s32 s_pitchLimit = game->df_pitchLimit;
			ImGui::LabelText("##ConfigLabel", "Pitch Limit"); ImGui::SameLine(comboOffset);
			ImGui::SetNextItemWidth(196 * s_uiScale);
			if (ImGui::Combo("##PitchLimit", &s_pitchLimit, c_tfePitchLimit, IM_ARRAYSIZE(c_tfePitchLimit)))
			{
				game->df_pitchLimit = PitchLimit(s_pitchLimit);
			}

			// Sky rendering mode.
			s32 skyMode = graphics->skyMode;
			ImGui::LabelText("##ConfigLabel", "Sky Render Mode"); ImGui::SameLine(comboOffset);
			ImGui::SetNextItemWidth(196 * s_uiScale);
			if (ImGui::Combo("##SkyMode", &skyMode, c_tfeSkyModeStrings, IM_ARRAYSIZE(c_tfeSkyModeStrings)))
			{
				graphics->skyMode = SkyMode(skyMode);
			}
		}
		ImGui::Separator();

		//////////////////////////////////////////////////////
		// Color Correction
		//////////////////////////////////////////////////////
		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Color Correction");
		ImGui::PopFont();

		ImGui::Checkbox("Enable", &graphics->colorCorrection);
		if (graphics->colorCorrection)
		{
			ImGui::SetNextItemWidth(196 * s_uiScale);
			ImGui::SliderFloat("Brightness", &graphics->brightness, 0.0f, 2.0f);

			ImGui::SetNextItemWidth(196 * s_uiScale);
			ImGui::SliderFloat("Contrast", &graphics->contrast, 0.0f, 2.0f);

			ImGui::SetNextItemWidth(196 * s_uiScale);
			ImGui::SliderFloat("Saturation", &graphics->saturation, 0.0f, 2.0f);

			ImGui::SetNextItemWidth(196 * s_uiScale);
			ImGui::SliderFloat("Gamma", &graphics->gamma, 0.0f, 2.0f);
		}
		else
		{
			// If color correction is disabled, reset values.
			graphics->brightness = 1.0f;
			graphics->contrast = 1.0f;
			graphics->saturation = 1.0f;
			graphics->gamma = 1.0f;
		}

		const ColorCorrection colorCorrection = { graphics->brightness, graphics->contrast, graphics->saturation, graphics->gamma };
		TFE_RenderBackend::setColorCorrection(graphics->colorCorrection, &colorCorrection);

		ImGui::Separator();

		//////////////////////////////////////////////////////
		// Reticle
		//////////////////////////////////////////////////////
		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Crosshair");
		ImGui::PopFont();

		if (ImGui::Checkbox("Enable###ReticleEnable", &graphics->reticleEnable))
		{
			reticle_enable(graphics->reticleEnable);
		}
		if (graphics->reticleEnable)
		{
			ImGui::SliderInt("Shape###ReticleShape", &graphics->reticleIndex, 0, reticle_getShapeCount() - 1);
			ImGui::SliderFloat4("Color###ReticleColor", &graphics->reticleRed, 0.0f, 1.0f);

			s32 scale = s32(graphics->reticleScale * 4.0f + 0.5f);
			ImGui::SliderInt("Scale###ReticleScale", &scale, 1, 8);
			graphics->reticleScale = f32(scale) * 0.25f;

			reticle_setShape(graphics->reticleIndex);
			reticle_setScale(graphics->reticleScale);
			reticle_setColor(&graphics->reticleRed);
		}

		ImGui::Separator();

		//////////////////////////////////////////////////////
		// Post-FX
		//////////////////////////////////////////////////////
		ImGui::PushFont(s_dialogFont);
		ImGui::LabelText("##ConfigLabel", "Post-FX");
		ImGui::PopFont();

		static bool s_bloom = false;
		static f32  s_bloomSoftness = 0.5f;
		static f32  s_bloomIntensity = 0.5f;

		//ImGui::Checkbox("Bloom", &s_bloom);
		ImGui::TextColored({ 1.0f, 1.0f, 1.0f, 0.33f }, "Bloom [TODO]");
		if (s_bloom)
		{
			ImGui::SetNextItemWidth(196*s_uiScale);
			ImGui::SliderFloat("Softness", &s_bloomSoftness, 0.0f, 1.0f);
			ImGui::SetNextItemWidth(196*s_uiScale);
			ImGui::SliderFloat("Intensity", &s_bloomIntensity, 0.0f, 1.0f);
		}
	}

	void configHud()
	{
		TFE_Settings_Hud* hud = TFE_Settings::getHudSettings();
		TFE_Settings_Window* window = TFE_Settings::getWindowSettings();

		ImGui::LabelText("##ConfigLabel", "Hud Scale Type:"); ImGui::SameLine(150*s_uiScale);
		ImGui::SetNextItemWidth(196*s_uiScale);
		ImGui::Combo("##HudScaleType", (s32*)&hud->hudScale, c_tfeHudScaleStrings, IM_ARRAYSIZE(c_tfeHudScaleStrings));

		ImGui::LabelText("##ConfigLabel", "Hud Position Type:"); ImGui::SameLine(150*s_uiScale);
		ImGui::SetNextItemWidth(196*s_uiScale);
		ImGui::Combo("##HudPosType", (s32*)&hud->hudPos, c_tfeHudPosStrings, IM_ARRAYSIZE(c_tfeHudPosStrings));

		ImGui::Separator();

		ImGui::SetNextItemWidth(196*s_uiScale);
		ImGui::SliderFloat("Scale", &hud->scale, 0.0f, 15.0f, "%.2f"); ImGui::SameLine(0.0f, 32.0f*s_uiScale);
		ImGui::SetNextItemWidth(128 * s_uiScale);
		ImGui::InputFloat("##HudScaleText", &hud->scale, 0.01f, 0.1f, 2);

		ImGui::SetNextItemWidth(196*s_uiScale);
		ImGui::SliderInt("Offset Lt", &hud->pixelOffset[0], -512, 512); ImGui::SameLine(0.0f, 3.0f*s_uiScale);
		ImGui::SetNextItemWidth(128 * s_uiScale);
		ImGui::InputInt("##HudOffsetX0Text", &hud->pixelOffset[0], 1, 10);

		ImGui::SetNextItemWidth(196 * s_uiScale);
		ImGui::SliderInt("Offset Rt", &hud->pixelOffset[1], -512, 512); ImGui::SameLine(0.0f, 3.0f*s_uiScale);
		ImGui::SetNextItemWidth(128 * s_uiScale);
		ImGui::InputInt("##HudOffsetX1Text", &hud->pixelOffset[1], 1, 10);

		ImGui::SetNextItemWidth(196*s_uiScale);
		ImGui::SliderInt("Offset Y", &hud->pixelOffset[2], -512, 512); ImGui::SameLine(0.0f, 10.0f*s_uiScale);
		ImGui::SetNextItemWidth(128 * s_uiScale);
		ImGui::InputInt("##HudOffsetYText", &hud->pixelOffset[2], 1, 10);
	}

	// Uses a percentage slider (0 - 100%) to adjust a floating point value (0.0 - 1.0).
	// TODO: Refactor UI code to use this and similar functionality.
	void labelSliderPercent(f32* floatValue, const char* labelText)
	{
		assert(floatValue && labelText);
		if (!floatValue || !labelText) { return; }

		ImGui::LabelText("##Label", "%s", labelText);
		ImGui::SameLine(f32(128 * s_uiScale));
		s32 percValue = s32((*floatValue) * 100.0f);

		char sliderId[256];
		sprintf_s(sliderId, 256, "##%s", labelText);
		ImGui::SliderInt(sliderId, &percValue, 0, 100, "%d%%");
		*floatValue = clamp(f32(percValue) * 0.01f, 0.0f, 1.0f);
	}


	void configSound()
	{
		TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
		ImGui::LabelText("##ConfigLabel", "Sound Volume");
		labelSliderPercent(&sound->soundFxVolume, "Game SoundFX");
		labelSliderPercent(&sound->musicVolume,   "Game Music");
		labelSliderPercent(&sound->cutsceneSoundFxVolume, "Cutscene SoundFX");
		labelSliderPercent(&sound->cutsceneMusicVolume, "Cutscene Music");

		ImGui::Separator();
		ImGui::LabelText("##ConfigLabel", "Sound Settings");
		bool use16Channels = sound->use16Channels;
		if (ImGui::Checkbox("Enable 16-channel iMuse Digital Audio", &use16Channels))
		{
			sound->use16Channels = use16Channels;
			if (sound->use16Channels)
			{
				ImSetDigitalChannelCount(16);
			}
			else
			{
				ImSetDigitalChannelCount(8);
			}
		}

		bool disableSoundInMenus = sound->disableSoundInMenus;
		if (ImGui::Checkbox("Disable Sound in Menus", &disableSoundInMenus))
		{
			sound->disableSoundInMenus = disableSoundInMenus;
		}

		TFE_Audio::setVolume(sound->soundFxVolume);
		TFE_MidiPlayer::setVolume(sound->musicVolume);

		int MIDI_DeviceCount = TFE_MidiDevice::getDeviceCount();
		const char* MIDI_Devices;
		std::string MIDI_DevicesStr;
		
		for (int i = 0; i < MIDI_DeviceCount; i++) {
			MIDI_DevicesStr += TFE_MidiDevice::getDeviceNameStr(i) + '\0';
		}
		MIDI_Devices = &MIDI_DevicesStr[0];
		
		ImGui::Dummy(ImVec2(0.0f, 20.0f));

		ImGui::LabelText("##ConfigLabel", "MIDI Output Device");
		ImGui::SetNextItemWidth(512.0f);
		static int MIDI_CurrentDevIndex = TFE_Settings::getSoundSettings()->midiDevice;


		if (ImGui::Combo("##MIDI Device", &MIDI_CurrentDevIndex, MIDI_Devices, MIDI_DeviceCount)) {
			if (MIDI_CurrentDevIndex != TFE_Settings::getSoundSettings()->midiDevice) {
				sound->midiDevice = MIDI_CurrentDevIndex;
				TFE_MidiPlayer::destroy();
				TFE_MidiPlayer::init(MIDI_CurrentDevIndex);
				if(s_menuRetState != APP_STATE_MENU)
					MIDI_ShowDevChangeAlert = true;
			}
		}

		ImGui::Dummy(ImVec2(0.0f, 20.0f));

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.17f, 0.68f, 1.0f, 1.0f));
		if(MIDI_ShowDevChangeAlert)
			ImGui::TextWrapped("NOTE: MIDI Instruments may be incorrect until the game is restarted.");
		ImGui::PopStyleColor();
	}

	void pickCurrentResolution()
	{
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();

		const size_t count = TFE_ARRAYSIZE(c_resolutionDim);
		for (size_t i = 0; i < count; i++)
		{
			if (graphics->gameResolution.z == c_resolutionDim[i].z)
			{
				s_resIndex = s32(i);
				return;
			}
		}
		s_resIndex = count;
	}

	////////////////////////////////////////////////////////////////
	// Main menu callbacks
	////////////////////////////////////////////////////////////////
	void menuItem_Start()
	{
		s_appState = APP_STATE_GAME;
	}

	void menuItem_Manual()
	{
		s_subUI = FEUI_MANUAL;
	}

	void menuItem_Credits()
	{
		s_subUI = FEUI_CREDITS;
	}

	void menuItem_Settings()
	{
		s_subUI = FEUI_CONFIG;
		s_configTab = CONFIG_GAME;
		
		pickCurrentResolution();
	}

	void menuItem_Mods()
	{
		s_subUI = FEUI_MODS;
		modLoader_read();
	}

	void menuItem_Editor()
	{
		s_appState = APP_STATE_EDITOR;
	}

	void menuItem_Exit()
	{
		s_appState = APP_STATE_QUIT;
	}

	char* getSelectedMod()
	{
		return s_selectedModCmd;
	}

	void clearSelectedMod()
	{
		s_selectedModCmd[0] = 0;
	}

	void setSelectedMod(const char* mod)
	{
		strcpy(s_selectedModCmd, mod);
	}

	void* getGradientTexture()
	{
		return s_gradientImage.image;
	}

	ImFont* getDialogFont()
	{
		return s_dialogFont;
	}

	void setCanSave(bool canSave)
	{
		s_canSave = canSave;
	}

	bool getCanSave()
	{
		return s_canSave;
	}

	void setState(AppState state)
	{
		s_appState = state;
	}

	void clearMenuState()
	{
		s_subUI = FEUI_NONE;
		s_drawNoGameDataMsg = false;
	}

	void renderBackground()
	{
		if (s_menuRetState != APP_STATE_MENU)
		{
			TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
			TFE_DarkForces::mission_render(graphics->rendererIndex);
		}
	}
}