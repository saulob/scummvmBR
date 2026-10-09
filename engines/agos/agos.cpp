/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/path.h"
#include "common/textconsole.h"
#include "common/translation.h"

#include "engines/traducoes.h"
#include "engines/util.h"

#include "gui/message.h"

#include "agos/debugger.h"
#include "agos/intern.h"
#include "agos/agos.h"
#include "agos/midi.h"
#include "agos/sound.h"

#include "backends/audiocd/audiocd.h"

#include "graphics/surface.h"
#include "graphics/sjis.h"
#include "graphics/fonts/amigafont.h"

#include "audio/mididrv.h"

#ifdef USE_BINK
#include "common/events.h"
#include "common/memstream.h"

#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

#include "video/bink_decoder.h"
#endif

namespace AGOS {

static const GameSpecificSettings simon1_settings = {
	"",                                     // base_filename
	"",                                     // restore_filename
	"",                                     // tbl_filename
	"EFFECTS",                              // effects_filename
	"SIMON",                                // speech_filename
};

static const GameSpecificSettings simon2_settings = {
	"",                                     // base_filename
	"",                                     // restore_filename
	"",                                     // tbl_filename
	"",                                     // effects_filename
	"SIMON2",                               // speech_filename
};

#ifdef ENABLE_AGOS2
static const GameSpecificSettings dimp_settings = {
	"Gdimp",                                // base_filename
	"",                                     // restore_filename
	"",                                     // tbl_filename
	"",                                     // effects_filename
	"MUSIC",                                // speech_filename
};

static const GameSpecificSettings jumble_settings = {
	"Gjumble",                              // base_filename
	"",                                     // restore_filename
	"",                                     // tbl_filename
	"",                                     // effects_filename
	"MUSIC",                                // speech_filename
};

static const GameSpecificSettings puzzle_settings = {
	"Gpuzzle",                              // base_filename
	"",                                     // restore_filename
	"",                                     // tbl_filename
	"",                                     // effects_filename
	"MUSIC",                                // speech_filename
};

static const GameSpecificSettings swampy_settings = {
	"Gswampy",                              // base_filename
	"",                                     // restore_filename
	"",                                     // tbl_filename
	"",                                     // effects_filename
	"MUSIC",                                // speech_filename
};

AGOSEngine_DIMP::AGOSEngine_DIMP(OSystem *system, const AGOSGameDescription *gd)
	: AGOSEngine_PuzzlePack(system, gd) {

	_iconToggleCount = 0;
	_voiceCount = 0;

	_lastTickCount = 0;
	_startSecondCount = 0;
	_tSecondCount = 0;
}

AGOSEngine_PuzzlePack::AGOSEngine_PuzzlePack(OSystem *system, const AGOSGameDescription *gd)
	: AGOSEngine_Feeble(system, gd) {

	_oopsValid = false;
	_gameTime = 0;
}
#endif

AGOSEngine_Simon2::AGOSEngine_Simon2(OSystem *system, const AGOSGameDescription *gd)
	: AGOSEngine_Simon1(system, gd) {
}

AGOSEngine_Simon1::AGOSEngine_Simon1(OSystem *system, const AGOSGameDescription *gd)
	: AGOSEngine_Waxworks(system, gd) {
}

AGOSEngine_Waxworks::AGOSEngine_Waxworks(OSystem *system, const AGOSGameDescription *gd)
	: AGOSEngine_Elvira2(system, gd) {

	_boxCR = false;
	_boxLineCount = 0;
	memset(_boxBuffer, 0, sizeof(_boxBuffer));
	_boxBufferPtr = _boxBuffer;

	_linePtrs[0] = nullptr;
	_linePtrs[1] = nullptr;
	_linePtrs[2] = nullptr;
	_linePtrs[3] = nullptr;
	_linePtrs[4] = nullptr;
	_linePtrs[5] = nullptr;
	memset(_lineCounts, 0, sizeof(_lineCounts));
}

AGOSEngine_Elvira2::AGOSEngine_Elvira2(OSystem *system, const AGOSGameDescription *gd)
	: AGOSEngine_Elvira1(system, gd) {
}

AGOSEngine_Elvira1::AGOSEngine_Elvira1(OSystem *system, const AGOSGameDescription *gd)
	: AGOSEngine(system, gd), _sjisCurChar(0), _sjisFont(nullptr) {
}

AGOSEngine_Elvira1::~AGOSEngine_Elvira1() {
	delete _sjisFont;
}

Common::Error AGOSEngine_Elvira1::init() {
	Common::Error ret = AGOSEngine::init();
	if (ret.getCode() == Common::kNoError && getPlatform() == Common::kPlatformPC98) {
		_sjisFont = Graphics::FontSJIS::createFont(Common::kPlatformPC98);
		if (_sjisFont)
			_sjisFont->toggleFatPrint(true);
		else
			error("AGOSEngine_Elvira1::init(): Failed to load SJIS font.");
	}
	// Automatically start the Atari ST music driver with tune 1
	if (getPlatform() == Common::kPlatformAtariST && (getFeatures() & GF_DEMO)) {
		playMusic(1, 0);
	}
	return ret;
}

AGOSEngine::AGOSEngine(OSystem *system, const AGOSGameDescription *gd)
	: Engine(system), _rnd("agos"), _gameDescription(gd) {

	//Image dumping command disabled as it doesn't work well
#if 0
	DebugMan.addDebugChannel(kDebugImageDump, "image_dump", "Enable dumping of images to files");
#endif
	_vcPtr = nullptr;
	_vcGetOutOfCode = 0;
	_gameOffsetsPtr = nullptr;

	_gameFile = nullptr;
	_opcode = 0;

	_itemMemSize = 0;
	_tableMemSize = 0;
	_vgaMemSize = 0;

	_musicIndexBase = 0;
	_soundIndexBase = 0;
	_tableIndexBase = 0;
	_textIndexBase = 0;

	_numMusic = 0;
	_numSFX = 0;
	_numSpeech = 0;
	_numZone = 0;

	_numBitArray1 = 0;
	_numBitArray2 = 0;
	_numBitArray3 = 0;
	_numItemStore = 0;
	_numTextBoxes = 0;
	_numVars = 0;
	_numVideoOpcodes = 0;
	_vgaBaseDelay = 0;
	_vgaPeriod = 0;

	_strippedTxtMem = nullptr;
	_textMem = nullptr;
	_textSize = 0;
	_stringTabNum = 0;
	_stringTabPos = 0;
	_stringTabSize = 0;
	_stringTabPtr = nullptr;

	_itemArrayPtr = nullptr;
	_itemArraySize = 0;
	_itemArrayInited = 0;

	_iconFilePtr = nullptr;

	_codePtr = nullptr;

	_localStringtable = nullptr;
	_stringIdLocalMin = 0;
	_stringIdLocalMax = 0;

	_roomStates = nullptr;
	_numRoomStates = 0;

	_menuBase = nullptr;
	_roomsList = nullptr;
	_roomsListPtr = nullptr;

	_xtblList = nullptr;
	_xtablesHeapPtrOrg = nullptr;
	_xtablesHeapCurPosOrg = 0;
	_xsubroutineListOrg = nullptr;

	_tblList = nullptr;
	_tablesHeapPtr = nullptr;
	_tablesHeapPtrOrg = nullptr;
	_tablesheapPtrNew = nullptr;
	_tablesHeapSize = 0;
	_tablesHeapCurPos = 0;
	_tablesHeapCurPosOrg = 0;
	_tablesHeapCurPosNew = 0;
	_subroutineListOrg = nullptr;

	_subroutineList = nullptr;

	_recursionDepth = 0;

	_lastVgaTick = 0;

	_marks = 0;
	_scanFlag = false;

	_scriptVar2 = 0;
	_runScriptReturn1 = false;
	_skipVgaWait = false;
	_noParentNotify = false;
	_beardLoaded = false;
	_litBoxFlag = false;
	_mortalFlag = false;
	_displayFlag = 0;
	_syncFlag2 = false;
	_inCallBack = false;
	_cepeFlag = false;
	_fastMode = false;

	_backFlag = false;

	_copyProtection = false;
	_pause = false;
	_speech = false;
	_subtitles = false;

	_traducaoPtBr = false;
	_simon2OverlayLanguage = Common::EN_ANY;
	_useSimon2LanguageOverlay = false;
	_simon2LanguageFlagTimer = 0;
	_simon2LanguageFlagClearPending = false;

	_animatePointer = 0;
	_maxCursorWidth = 0;
	_maxCursorHeight = 0;
	_mouseAnim = 0;
	_mouseAnimMax = 0;
	_mouseCursor = 0;
	_mouseData = nullptr;
	_oldMouseCursor = 0;
	_currentMouseCursor = 0;
	_currentMouseAnim = 0;
	_oldMouseAnimMax = 0;

	_vgaVar9 = 0;
	_chanceModifier = 0;
	_restoreWindow6 = 0;
	_scrollX = 0;
	_scrollY = 0;
	_scrollXMax = 0;
	_scrollYMax = 0;
	_scrollCount = 0;
	_scrollFlag = 0;
	_scrollHeight = 0;
	_scrollWidth = 0;
	_scrollImage = nullptr;
	_boxStarHeight = 0;

	_scriptVerb = 0;
	_scriptNoun1 = 0;
	_scriptNoun2 = 0;
	_scriptAdj1 = 0;
	_scriptAdj2 = 0;

	_curWindow = 0;
	_textWindow = nullptr;
	_pnAmigaFont = nullptr;

	_subjectItem = nullptr;
	_objectItem = nullptr;
	_currentPlayer = nullptr;

	_iOverflow = false;
	_nameLocked = false;
	_hitAreaObjectItem = nullptr;
	_lastHitArea = nullptr;
	_lastNameOn = nullptr;
	_lastHitArea3 = nullptr;
	_hitAreaSubjectItem = nullptr;
	_currentBox = nullptr;
	_currentBoxNum = 0;
	_currentVerbBox = nullptr;
	_lastVerbOn = nullptr;
	_needHitAreaRecalc = 0;
	_verbHitArea = 0;
	_defaultVerb = 0;
	_mouseHideCount = 0;

	_dragAccept = false;
	_dragEnd = false;
	_dragFlag = false;
	_dragMode = false;
	_dragCount = 0;
	_lastClickRem = nullptr;

	_windowNum = 0;

	_printCharCurPos = 0;
	_printCharMaxPos = 0;
	_printCharPixelCount = 0;
	_numLettersToPrint = 0;

	_clockStopped = 0;
	_gameStoppedClock = 0;
	_lastTime = 0;
	_lastMinute = 0;

	_firstTimeStruct = nullptr;
	_pendingDeleteTimeEvent = nullptr;

	_action = kActionNone;

	_initMouse = 0;
	_leftButtonDown = false;
	_rightButtonDown = false;
	_clickOnly = false;
	_oneClick = 0;
	_leftClick = 0;
	_rightClick = 0;
	_noRightClick = false;

	_leftButton = 0;
	_leftButtonCount = 0;
	_leftButtonOld = 0;

	_dummyItem1 = new Item();
	_dummyItem2 = new Item();
	_dummyItem3 = new Item();

	_videoLockOut = 0;
	_scrollUpHitArea = 0;
	_scrollDownHitArea = 0;

	_noOverWrite = 0;
	_rejectBlock = false;

	_fastFadeCount = 0;
	_fastFadeInFlag = 0;
	_fastFadeOutFlag = false;
	_neverFade = false;
	_exitCutscene = 0;
	_paletteFlag = 0;
	_bottomPalette = false;
	_picture8600 = 0;

	_soundFileId = 0;
	_lastMusicPlayed = 0;
	_nextMusicToPlay = 0;
	_sampleEnd = 0;
	_sampleWait = 0;

	_showPreposition = 0;
	_showMessageFlag = 0;

	_newDirtyClip = false;
	_wiped = false;
	_copyScnFlag = 0;
	_vgaSpriteChanged = 0;

	_block = nullptr;
	_blockEnd = nullptr;
	_vgaMemPtr = nullptr;
	_vgaMemEnd = nullptr;
	_vgaMemBase = nullptr;
	_vgaFrozenBase = nullptr;
	_vgaRealBase = nullptr;
	_zoneBuffers = nullptr;
	_pnAmigaUiVisible = false;

	_curVgaFile1 = nullptr;
	_curVgaFile2 = nullptr;
	_curSfxFile = nullptr;
	_curSfxFileSize = 0;

	_syncCount = 0;

	_frameCount = 0;

	_zoneNumber = 0;

	_vgaWaitFor = 0;
	_lastVgaWaitFor = 0;

	_vgaCurZoneNum = 0;
	_vgaCurSpriteId = 0;

	_baseY = 0;
	_scale = 0;

	_feebleRect.left = 0;
	_feebleRect.right = 0;
	_feebleRect.top = 0;
	_feebleRect.bottom = 0;

	_scaleX = 0;
	_scaleY = 0;
	_scaleWidth = 0;
	_scaleHeight = 0;

	_nextVgaTimerToProcess = nullptr;

	_opcode177Var1 = 1;
	_opcode177Var2 = 0;
	_opcode178Var1 = 1;
	_opcode178Var2 = 0;

	_classLine = nullptr;
	_classMask = 0;
	_classMode1 = 0;
	_classMode2 = 0;
	_currentLine = nullptr;
	_currentTable = nullptr;
	_findNextPtr = nullptr;

	_agosMenu = 0;
	_currentRoom = 0;
	_superRoomNumber = 0;
	_wallOn = 0;

	memset(_objectArray, 0, sizeof(_objectArray));
	memset(_itemStore, 0, sizeof(_itemStore));
	memset(_textMenu, 0, sizeof(_textMenu));

	memset(_shortText, 0, sizeof(_shortText));
	memset(_shortTextX, 0, sizeof(_shortText));
	memset(_shortTextY, 0, sizeof(_shortText));
	memset(_longText, 0, sizeof(_longText));
	memset(_longSound, 0, sizeof(_longSound));

	memset(_bitArray, 0, sizeof(_bitArray));
	memset(_bitArrayTwo, 0, sizeof(_bitArrayTwo));
	memset(_bitArrayThree, 0, sizeof(_bitArrayThree));

	_variableArray = nullptr;
	_variableArray2 = nullptr;
	_variableArrayPtr = nullptr;

	memset(_windowArray, 0, sizeof(_windowArray));

	memset(_fcsData1, 0, sizeof(_fcsData1));
	memset(_fcsData2, 0, sizeof(_fcsData2));

	_awaitTwoByteToken = 0;
	_byteTokens = nullptr;
	_byteTokenStrings = nullptr;
	_twoByteTokens = nullptr;
	_twoByteTokenStrings = nullptr;
	_secondTwoByteTokenStrings = nullptr;
	_thirdTwoByteTokenStrings = nullptr;
	memset(_textBuffer, 0, sizeof(_textBuffer));
	_textCount = 0;

	_freeStringSlot = 0;

	memset(_stringReturnBuffer, 0, sizeof(_stringReturnBuffer));

	memset(_pathFindArray, 0, sizeof(_pathFindArray));

	memset(_pathValues, 0, sizeof(_pathValues));
	_PVCount = 0;
	_GPVCount = 0;

	memset(_pathValues1, 0, sizeof(_pathValues1));
	_PVCount1 = 0;
	_GPVCount1 = 0;

	memset(_currentPalette, 0, sizeof(_currentPalette));
	memset(_displayPalette, 0, sizeof(_displayPalette));
	memset(_pnPaletteBanks, 0, sizeof(_pnPaletteBanks));
	memset(_pnFadeCurrent, 0, sizeof(_pnFadeCurrent));
	memset(_pnHavePaletteBank, 0, sizeof(_pnHavePaletteBank));
	_pnDayNightControllerSelectorMask = 0xFFFF;
	_pnDayNightControllerLastStage = 0xFF;
	_pnLastClockMinutes = -1;
	_pnDayNightControllerTickCounter = 0x00C8;

	memset(_videoBuf1, 0, sizeof(_videoBuf1));
	memset(_videoWindows, 0, sizeof(_videoWindows));

	_dummyWindow = new WindowBlock;
	_windowList = new WindowBlock[16];

	memset(_lettersToPrintBuf, 0, sizeof(_lettersToPrintBuf));

	_planarBuf = nullptr;
	_pak98Buf = nullptr;
	_paletteModNext = 16;

	_midiEnabled = false;

	_vgaTickCounter = 0;

	_sound = nullptr;

	_effectsMuted = false;
	_ambientMuted = false;
	_musicMuted = false;
	// Initialize at default ScummVM volumes; these will be overwritten by
	// syncSoundSettings.
	_musicVolume = 192;
	_effectsVolume = 192;
	_useDigitalSfx = true;
	_pendingWaitCommandDelay = false;
	_pendingPNWaitScreenDelay = false;

	_saveLoadType = 0;
	_saveLoadSlot = 0;
	memset(_saveLoadName, 0, sizeof(_saveLoadName));
	memset(_saveBuf, 0, sizeof(_saveBuf));

	_saveGameNameLen = 0;
	_saveLoadRowCurPos = 0;
	_numSaveGameRows = 0;
	_saveDialogFlag = false;
	_saveOrLoad = false;
	_saveLoadEdit = false;

	_hyperLink = 0;
	_interactY = 0;
	_oracleMaxScrollY = 0;
	_noOracleScroll = 0;

	_backGroundBuf = nullptr;
	_backBuf = nullptr;
	_scaleBuf = nullptr;
	_window4BackScn = nullptr;
	_window6BackScn = nullptr;

	_window3Flag = 0;
	_window4Flag = 0;
	_window6Flag = 0;

	_moveXMin = 0;
	_moveYMin = 0;
	_moveXMax = 0;
	_moveYMax = 0;

	_forceAscii = false;
	_useSimon2LanguageOverlay = false;

	_vc10BasePtrOld = nullptr;
	memcpy (_hebrewCharWidths,
		"\x5\x5\x4\x6\x5\x3\x4\x5\x6\x3\x5\x5\x4\x6\x5\x3\x4\x6\x5\x6\x6\x6\x5\x5\x5\x6\x5\x6\x6\x6\x6\x6", 32);

	const Common::FSNode gameDataDir(ConfMan.getPath("path"));

	// Add default file directories for Acorn version of
	// Simon the Sorcerer 1
	SearchMan.addSubDirectoryMatching(gameDataDir, "execute");
	SearchMan.addSubDirectoryMatching(gameDataDir, "tables");
	SearchMan.addSubDirectoryMatching(gameDataDir, "text");
	SearchMan.addSubDirectoryMatching(gameDataDir, "tunes");

	// Add default file directories for Amiga/Macintosh
	// versions of Simon the Sorcerer 2
	SearchMan.addSubDirectoryMatching(gameDataDir, "voices");
	SearchMan.addSubDirectoryMatching(gameDataDir, "data");

	// Add default file directories for Amiga & Macintosh
	// versions of The Feeble Files
	SearchMan.addSubDirectoryMatching(gameDataDir, "gfx");
	SearchMan.addSubDirectoryMatching(gameDataDir, "movies");
	SearchMan.addSubDirectoryMatching(gameDataDir, "sfx");
	SearchMan.addSubDirectoryMatching(gameDataDir, "speech");
}

Common::Error AGOSEngine::init() {
	if (getGameId() == GID_DIMP) {
		_screenWidth = 496;
		_screenHeight = 400;
	} else if (getGameType() == GType_FF || getGameType() == GType_PP) {
		_screenWidth = 640;
		_screenHeight = 480;
	} else {
		_screenWidth = 320;
		_screenHeight = isPnAmiga() ? 240 : 200;
	}

	_internalWidth = _screenWidth;
	_internalHeight = _screenHeight;
	if (isPnAmiga()) {
		_internalWidth = 640;
		_internalHeight = 480;
	}

	if (getPlatform() == Common::kPlatformPC98) {
		_internalWidth <<= 1;
		_internalHeight <<= 1;
	}

	if (ConfMan.hasKey("disable_fade_effects"))
		_neverFade = ConfMan.getBool("disable_fade_effects");

	initGraphics(_internalWidth, _internalHeight);

	_midi = new MidiPlayer(this);

	if ((getGameType() == GType_SIMON2 && getPlatform() == Common::kPlatformWindows) ||
			(getGameType() == GType_SIMON2 && (getPlatform() == Common::kPlatformAmiga || isSimon2MacAmiga())) ||
			(getGameType() == GType_SIMON1 && getPlatform() == Common::kPlatformWindows) ||
			((getFeatures() & GF_TALKIE) && getPlatform() == Common::kPlatformAcorn) ||
			(getPlatform() == Common::kPlatformDOS && getGameType() != GType_PN && getGameType() != GType_FF) ||
			getPlatform() == Common::kPlatformPC98) {

		int ret = _midi->open();
		if (ret)
			warning("MIDI Player init failed: \"%s\"", MidiDriver::getErrorName(ret));

		_midiEnabled = true;
	}
	// Digital SFX are used if MIDI SFX are not available or if the "prefer
	// digital SFX" setting is set to true or is not present at all.
	// Two exceptions to this are:
	// - Elvira 2 DOS needs an optional file to enable digital SFX. If it is
	//   not present, MIDI SFX are used.
	// - Simon 1 DOS floppy has only MIDI SFX.
	// Note that MIDI SFX can be safely used if the MidiPlayer failed to
	// initialize; they just will not play.
	_useDigitalSfx = !_midiEnabled || !_midi->hasMidiSfx() || !ConfMan.hasKey("prefer_digitalsfx") || ConfMan.getBool("prefer_digitalsfx");
	if ((getGameType() == GType_ELVIRA2 && getPlatform() == Common::kPlatformDOS && !SearchMan.hasFile("013.VGA")) ||
			(getGameType() == GType_SIMON1 && getPlatform() == Common::kPlatformDOS && !(getFeatures() & GF_TALKIE))) {
		_useDigitalSfx = false;
	}
	if (!_useDigitalSfx && (getGameType() == GType_ELVIRA2 || getGameType() == GType_WW) && getPlatform() == Common::kPlatformDOS) {
		// Load the MIDI SFX data file for Elvira 2 and Waxworks DOS.
		loadMidiSfx();
	}

	// allocate buffers
	_backGroundBuf = new Graphics::Surface();
	_backGroundBuf->create(_screenWidth, _screenHeight, Graphics::PixelFormat::createFormatCLUT8());

	if (getGameType() == GType_FF || getGameType() == GType_PP ||
			(getGameType() == GType_ELVIRA1 && getPlatform() == Common::kPlatformPC98) || isPnAmiga()) {
		_backBuf = new Graphics::Surface();
		_backBuf->create(_screenWidth, _screenHeight, Graphics::PixelFormat::createFormatCLUT8());
		memset(_backBuf->getPixels(), 0, _backBuf->pitch * _backBuf->h);
		_scaleBuf = new Graphics::Surface();
		_scaleBuf->create(_internalWidth, isPnAmiga() ? _screenHeight : _internalHeight, Graphics::PixelFormat::createFormatCLUT8());
		memset(_scaleBuf->getPixels(), 0, _scaleBuf->pitch * _scaleBuf->h);
	}

	if (getGameType() == GType_SIMON2) {
		_window4BackScn = new Graphics::Surface();
		_window4BackScn->create(_screenWidth, _screenHeight, Graphics::PixelFormat::createFormatCLUT8());
	} else if (getGameType() == GType_SIMON1) {
		_window4BackScn = new Graphics::Surface();
		_window4BackScn->create(_screenWidth, 134, Graphics::PixelFormat::createFormatCLUT8());
	} else if (getGameType() == GType_WW || getGameType() == GType_ELVIRA2) {
		_window4BackScn = new Graphics::Surface();
		_window4BackScn->create(224, 127, Graphics::PixelFormat::createFormatCLUT8());
	} else if (getGameType() == GType_ELVIRA1) {
		_window4BackScn = new Graphics::Surface();
		if (getPlatform() == Common::kPlatformAmiga && (getFeatures() & GF_DEMO)) {
			_window4BackScn->create(224, 196, Graphics::PixelFormat::createFormatCLUT8());
		} else {
			_window4BackScn->create(224, 144, Graphics::PixelFormat::createFormatCLUT8());
		}
		_window6BackScn = new Graphics::Surface();
		_window6BackScn->create(48, 80, Graphics::PixelFormat::createFormatCLUT8());
	}

	setupGame();

	setDebugger(new Debugger(this));
	_sound = new Sound(this, gss, _mixer);

	_copyProtection = ConfMan.getBool("copy_protection");
	_language = Common::parseLanguage(ConfMan.get("language"));

	// ScummVMBR: built-in pt-BR translation, applied in memory over the
	// original game files before any of them is read. checkGameGUIOption()
	// takes option names, not the GUIO codes of the detection table.
	const Common::String guiOptions = Common::getGameGUIOptionsDescription(_gameDescription->desc.guiOptions);
	if (Common::checkGameGUIOption(GAMEOPTION_TRADUCAO_PTBR, guiOptions) &&
			ConfMan.getBool("traducao_ptbr")) {
		Common::U32String reason;
		if (ScummBR::mountTraducao(_gameDescription->desc.gameId, reason)) {
			_language = Common::PT_BRA;
			_traducaoPtBr = true;
		} else {
			reason += Common::U32String("\n\n");
			reason += _("The game will run in its original language.");
			GUI::MessageDialog dialog(reason);
			dialog.runModal();
		}
	}

	_simon2OverlayLanguage = _language;
	loadSimon2LanguageOverlay();

	if (getGameType() == GType_PP) {
		_speech = true;
		_subtitles = false;
	} else if (getFeatures() & GF_TALKIE) {
		_speech = !ConfMan.getBool("speech_mute");
		_subtitles = ConfMan.getBool("subtitles");

		if (getGameType() == GType_SIMON1) {
			// English and German versions lack text for a few speech-only
			// scenes, but speech and subtitles together are still allowed.
			// All versions require speech to be enabled
			if (!_speech) {
				_speech = true;
				_mixer->setVolumeForSoundType(Audio::Mixer::kSpeechSoundType, 0);
			}
		}

		// Default to speech only, if both speech and subtitles disabled
		if (!_speech && !_subtitles)
			_speech = true;
	} else {
		_speech = false;
		_subtitles = true;
	}

	// Setup mixer
	syncSoundSettings();

	return Common::kNoError;
}

static const uint16 initialVideoWindows_Simon[20] = {
	0,  0, 20, 200,
	0,  0,  3, 136,
	17, 0,  3, 136,
	0,  0, 20, 200,
	0,  0, 20, 134
};

static const uint16 initialVideoWindows_Common[20] = {
	 3, 0, 14, 136,
	 0, 0,  3, 136,
	17, 0,  3, 136,
	 0, 0, 20, 200,
	 3, 3, 14, 127,
};

static const uint16 initialVideoWindows_PN[20] = {
	 3, 0, 14, 136,
	 0, 0,  3, 136,
	17, 0,  3, 136,
	 0, 0, 20, 200,
	 3, 2, 14, 129,
};

#ifdef ENABLE_AGOS2
void AGOSEngine_PuzzlePack::setupGame() {
	if (getGameId() == GID_DIMP) {
		gss = &dimp_settings;
	} else if (getGameId() == GID_JUMBLE) {
		gss = &jumble_settings;
	} else if (getGameId() == GID_PUZZLE) {
		gss = &puzzle_settings;
	} else if (getGameId() == GID_SWAMPY) {
		gss = &swampy_settings;
	}
	_numVideoOpcodes = 85;
	_vgaMemSize = 7500000;
	_itemMemSize = 20000;
	_tableMemSize = 200000;
	_frameCount = 1;
	_vgaBaseDelay = 5;
	_vgaPeriod = (getGameId() == GID_DIMP) ? 35 : 30;
	_numBitArray1 = 128;
	_numItemStore = 10;
	_numTextBoxes = 40;
	_numVars = 2048;

	_numZone = 450;

	AGOSEngine::setupGame();
}
#endif

void AGOSEngine_Simon2::setupGame() {
	gss = &simon2_settings;
	_tableIndexBase = 1580 / 4;
	_textIndexBase = 1500 / 4;
	_numVideoOpcodes = 75;
#if defined(__DS__)
	_vgaMemSize = 1300000;
#else
	_vgaMemSize = 2000000;
#endif
	_itemMemSize = 20000;
	_tableMemSize = 100000;
	// Check whether to use MT-32 MIDI tracks in Simon the Sorcerer 2
	if (getGameType() == GType_SIMON2 && getPlatform() == Common::kPlatformDOS && _midi->usesMT32Data())
		_musicIndexBase = MUSIC_INDEX_BASE_SIMON2_MT32;
	else
		_musicIndexBase = MUSIC_INDEX_BASE_SIMON2_GM;
	_soundIndexBase = 1660 / 4;
	_frameCount = 1;
	_vgaBaseDelay = 1;
	_vgaPeriod = 45;
	_numBitArray1 = 16;
	_numBitArray2 = 16;
	_numItemStore = 10;
	_numTextBoxes = 20;
	_numVars = 255;

	_numMusic = 93;
	_numSFX = 222;
	_numSpeech = 11997;
	_numZone = 140;

	AGOSEngine::setupGame();
}

void AGOSEngine_Simon1::setupGame() {
	gss = &simon1_settings;
	_tableIndexBase = 1576 / 4;
	_textIndexBase = 1460 / 4;
	_numVideoOpcodes = 64;
	_vgaMemSize = 1000000;
	_itemMemSize = 20000;
	_tableMemSize = 50000;
	_musicIndexBase = 1316 / 4;
	_soundIndexBase = 0;
	_frameCount = 1;
	_vgaBaseDelay = 1;
	_vgaPeriod = 50;
	_numBitArray1 = 16;
	_numBitArray2 = 16;
	_numItemStore = 10;
	_numTextBoxes = 20;
	_numVars = 255;

	_numMusic = 34;
	_numSFX = 127;
	_numSpeech = 3623;
	_numZone = 164;

	AGOSEngine::setupGame();
}

void AGOSEngine_Waxworks::setupGame() {
	gss = &simon1_settings;
	_numVideoOpcodes = 64;
	_vgaMemSize = 1000000;
	_itemMemSize = 80000;
	_tableMemSize = 50000;
	_frameCount = 4;
	_vgaBaseDelay = 1;
	_vgaPeriod = 50;
	_numBitArray1 = 16;
	_numBitArray2 = 15;
	_numItemStore = 50;
	_numTextBoxes = 10;
	_numVars = 255;

	_numMusic = 26;
	_numZone = 155;

	AGOSEngine::setupGame();
}

void AGOSEngine_Elvira2::setupGame() {
	gss = &simon1_settings;
	_numVideoOpcodes = 60;
	_vgaMemSize = 1000000;
	_itemMemSize = 64000;
	_tableMemSize = 100000;
	_frameCount = 4;
	_vgaBaseDelay = 1;
	_vgaPeriod = 50;
	_numBitArray1 = 16;
	_numBitArray2 = 15;
	_numItemStore = 50;
	_numVars = 255;

	_numMusic = 9;
	_numZone = 99;

	AGOSEngine::setupGame();
}

void AGOSEngine_Elvira1::setupGame() {
	gss = &simon1_settings;
	_numVideoOpcodes = 57;
	_vgaMemSize = 1000000;
	_itemMemSize = 64000;
	_tableMemSize = 256000;
	_frameCount = 4;
	_vgaBaseDelay = 1;
	_vgaPeriod = 50;
	_numVars = 512;

	_numMusic = 14;
	_numZone = 74;

	AGOSEngine::setupGame();
}

void AGOSEngine_PN::setupGame() {
	gss = &simon1_settings;
	_numVideoOpcodes = 57;
	_vgaMemSize = 1000000;
	_frameCount = 4;
	_vgaBaseDelay = 1;
	_vgaPeriod = 50;
	_numVars = 256;

	_numZone = 26;

	AGOSEngine::setupGame();
}

void AGOSEngine::setupGame() {
	allocItemHeap();
	allocTablesHeap();

	if (getGameType() != GType_SIMON2)
		initMouse();

	_variableArray = (int16 *)calloc(_numVars, sizeof(int16));
	_variableArrayPtr = _variableArray;
	if (getGameType() == GType_FF || getGameType() == GType_PP) {
		_variableArray2 = (int16 *)calloc(_numVars, sizeof(int16));
	}

	setupOpcodes();
	setupVgaOpcodes();

	setZoneBuffers();

	_currentMouseCursor = 255;
	_currentMouseAnim = 255;

	_lastMusicPlayed = -1;
	_nextMusicToPlay = -1;

	_noOverWrite = 0xFFFF;

	_stringIdLocalMin = 1;

	_agosMenu = 1;
	_superRoomNumber = 1;

	for (int i = 0; i < 20; i++) {
		if (getGameType() == GType_SIMON1 || getGameType() == GType_SIMON2) {
			_videoWindows[i] = initialVideoWindows_Simon[i];
		} else if (getGameType() == GType_PN) {
			_videoWindows[i] = initialVideoWindows_PN[i];
		} else {
			_videoWindows[i] = initialVideoWindows_Common[i];
		}
	}

	if (getGameType() == GType_ELVIRA2 && getPlatform() == Common::kPlatformAtariST) {
		_videoWindows[9] = 75;
	}
	if (isPnAmiga()) {
		_videoWindows[3] = 134;
		_videoWindows[7] = 134;
		_videoWindows[11] = 134;
		_videoWindows[15] = 240;
		if (_pnAmigaFont == nullptr)
			_pnAmigaFont = new Graphics::AmigaFont(Graphics::AmigaFont::kTopaz9Builtin);
	}
}


bool AGOSEngine::isPnAmiga() const {
	return getGameType() == GType_PN && getPlatform() == Common::kPlatformAmiga;
}

bool AGOSEngine::usePnAmigaDoubleHeightTopaz() const {
	return ConfMan.hasKey("pn_amiga_doubleheight_font") && ConfMan.getBool("pn_amiga_doubleheight_font");
}

const Graphics::AmigaFont *AGOSEngine::getPnAmigaFont() const {
	return _pnAmigaFont;
}

uint16 AGOSEngine::getPnAmigaGlyphAdvance(byte chr) const {
	const Graphics::AmigaFont *font = getPnAmigaFont();
	if (font == nullptr)
		return 0;
	if (chr == 128 || chr == 129)
		return font->getCharAdvanceWidth(' ');
	if (chr < font->getLoChar() || chr > font->getHiChar())
		return 0;

	return font->getCharAdvanceWidth(chr);
}

uint16 AGOSEngine::getPnAmigaGlyphRenderWidth(byte chr) const {
	const Graphics::AmigaFont *font = getPnAmigaFont();
	if (font == nullptr)
		return 0;
	if (chr == 128 && isPnAmiga())
		chr = '_';
	if (chr == 128 || chr == 129)
		return getPnAmigaGlyphAdvance(' ');
	if (chr < font->getLoChar() || chr > font->getHiChar())
		return 0;

	return font->getCharRenderWidth(chr);
}

uint16 AGOSEngine::getPnAmigaGlyphHeight() const {
	const Graphics::AmigaFont *font = getPnAmigaFont();
	const uint16 rawHeight = font ? font->getFontHeight() : 9;
	return usePnAmigaDoubleHeightTopaz() ? rawHeight * 2 : rawHeight;
}

void AGOSEngine::ensurePnAmigaTextPlanes() {
	if (!isPnAmiga())
		return;
	const WindowBlock *mainWindow = _windowArray[0];
	const WindowBlock *inputWindow = _windowArray[2];
	if (mainWindow == nullptr || inputWindow == nullptr)
		return;

	const uint16 mainWidth = getPnAmigaTextPlaneWidth(mainWindow);
	const uint16 mainHeight = getPnAmigaWindowInteriorHeight(mainWindow);
	const uint16 inputWidth = getPnAmigaTextPlaneWidth(inputWindow);
	const uint16 inputHeight = getPnAmigaWindowInteriorHeight(inputWindow);

	if (_pnAmigaMainTextPlane.pixels == nullptr || _pnAmigaMainTextPlane.width != mainWidth || _pnAmigaMainTextPlane.height != mainHeight) {
		delete[] _pnAmigaMainTextPlane.pixels;
		_pnAmigaMainTextPlane.width = mainWidth;
		_pnAmigaMainTextPlane.height = mainHeight;
		_pnAmigaMainTextPlane.pixels = new byte[mainWidth * mainHeight];
		memset(_pnAmigaMainTextPlane.pixels, 0, mainWidth * mainHeight);
	}

	if (_pnAmigaInputTextPlane.pixels == nullptr || _pnAmigaInputTextPlane.width != inputWidth || _pnAmigaInputTextPlane.height != inputHeight) {
		delete[] _pnAmigaInputTextPlane.pixels;
		_pnAmigaInputTextPlane.width = inputWidth;
		_pnAmigaInputTextPlane.height = inputHeight;
		_pnAmigaInputTextPlane.pixels = new byte[inputWidth * inputHeight];
		memset(_pnAmigaInputTextPlane.pixels, 0, inputWidth * inputHeight);
	}
}

AGOSEngine::PnAmigaTextPlane *AGOSEngine::getPnAmigaTextPlane(const WindowBlock *window) {
	if (isPnAmigaMainTextWindow(window))
		return &_pnAmigaMainTextPlane;
	if (isPnAmigaInputWindow(window))
		return &_pnAmigaInputTextPlane;
	return nullptr;
}

const AGOSEngine::PnAmigaTextPlane *AGOSEngine::getPnAmigaTextPlane(const WindowBlock *window) const {
	if (isPnAmigaMainTextWindow(window))
		return &_pnAmigaMainTextPlane;
	if (isPnAmigaInputWindow(window))
		return &_pnAmigaInputTextPlane;
	return nullptr;
}


AGOSEngine::~AGOSEngine() {
	_system->getAudioCDManager()->stop();
	stopMusic();
	delete _pnAmigaFont;

	for (uint i = 0; i < _itemHeap.size(); i++) {
		delete[] _itemHeap[i];
	}
	_itemHeap.clear();

	free(_tablesHeapPtr - _tablesHeapCurPos);

	free(_mouseData);

	free(_gameOffsetsPtr);
	free(_iconFilePtr);
	free(_itemArrayPtr);
	free(_menuBase);
	free(_roomsList);
	free(_roomStates);
	free(_stringTabPtr);
	delete[] _pnAmigaMainTextPlane.pixels;
	delete[] _pnAmigaInputTextPlane.pixels;
	free(_strippedTxtMem);
	free(_tblList);
	free(_textMem);
	free(_xtblList);

	if (_backGroundBuf)
		_backGroundBuf->free();
	delete _backGroundBuf;
	if (_backBuf)
		_backBuf->free();
	delete _backBuf;
	free(_planarBuf);
	delete[] _pak98Buf;
	if (_scaleBuf)
		_scaleBuf->free();
	delete _scaleBuf;
	free(_zoneBuffers);

	if (_window4BackScn)
		_window4BackScn->free();
	delete _window4BackScn;
	if (_window6BackScn)
		_window6BackScn->free();
	delete _window6BackScn;

	delete _midi;

	free(_firstTimeStruct);
	free(_pendingDeleteTimeEvent);

	free(_variableArray);
	free(_variableArray2);

	delete _dummyItem1;
	delete _dummyItem2;
	delete _dummyItem3;

	delete _dummyWindow;
	delete[] _windowList;

	delete _sound;
	delete _gameFile;

	ScummBR::unmountTraducao();
}

void AGOSEngine::pauseEngineIntern(bool pauseIt) {
	if (pauseIt) {
		_keyPressed.reset();
		_action = kActionNone;
		_pause = true;

		_midi->pause(true);
		_mixer->pauseAll(true);
	} else {
		_pause = false;

		_midi->pause(false);
		_mixer->pauseAll(false);
	}
}

void AGOSEngine::pause() {
	PauseToken pt = pauseEngine();

	while (_pause && !shouldQuit()) {
		delay(1);
		if (_action == kActionPause) {
			pt.clear();
			_keyPressed.reset();
			_action = kActionNone;
		}
	}
}

#ifdef USE_BINK
// ScummBR intro video for Simon 1 and Simon 2, embedded from a .bik file by scummbr_intro.py.
#include "agos/scummbr_intro.inc"

// Colours are binned to 5 bits per channel to build the intro palette.
static const int kScummBRIntroBins = 32 * 32 * 32;

static inline int scummBRIntroBinKey(int r, int g, int b) {
	return (r << 10) | (g << 5) | b;
}

// The Bink decoder outputs ARGB32; returns the 5-bit colour bin of a pixel.
static inline int scummBRIntroPixelBin(uint32 pixel, const Graphics::PixelFormat &format) {
	const int r = (pixel >> format.rShift) & 0xFF;
	const int g = (pixel >> format.gShift) & 0xFF;
	const int b = (pixel >> format.bShift) & 0xFF;
	return scummBRIntroBinKey(r >> 3, g >> 3, b >> 3);
}

// Expands a 5-bit channel value back to 8 bits.
static inline int scummBRIntroExpand(int v) {
	return (v << 3) | (v >> 2);
}

struct ScummBRIntroBox {
	uint8 lo[3];
	uint8 hi[3];
	uint32 count;
};

// Trims a box to the bounding box of the colour bins that hold pixels.
static void scummBRIntroShrinkBox(ScummBRIntroBox &box, const uint32 *hist) {
	uint8 lo[3] = { 31, 31, 31 };
	uint8 hi[3] = { 0, 0, 0 };
	uint32 count = 0;

	for (int r = box.lo[0]; r <= box.hi[0]; r++) {
		for (int g = box.lo[1]; g <= box.hi[1]; g++) {
			for (int b = box.lo[2]; b <= box.hi[2]; b++) {
				const uint32 n = hist[scummBRIntroBinKey(r, g, b)];
				if (!n)
					continue;

				const int c[3] = { r, g, b };
				for (int i = 0; i < 3; i++) {
					if (c[i] < lo[i])
						lo[i] = (uint8)c[i];
					if (c[i] > hi[i])
						hi[i] = (uint8)c[i];
				}
				count += n;
			}
		}
	}

	box.count = count;
	if (count) {
		for (int i = 0; i < 3; i++) {
			box.lo[i] = lo[i];
			box.hi[i] = hi[i];
		}
	}
}

// Median cut over the colour histogram. The most populated box is split at
// the median of its widest axis until there are 255 boxes, and each box
// becomes one palette entry. Entry 0 is kept black and the boxes use 1-255.
// lut maps every colour bin used by the video to its palette entry.
static void scummBRIntroBuildPalette(const uint32 *hist, byte *palette, byte *lut) {
	Common::Array<ScummBRIntroBox> boxes;
	ScummBRIntroBox root = { { 0, 0, 0 }, { 31, 31, 31 }, 0 };
	scummBRIntroShrinkBox(root, hist);
	if (root.count)
		boxes.push_back(root);

	while (boxes.size() < 255) {
		int best = -1;
		for (uint i = 0; i < boxes.size(); i++) {
			const ScummBRIntroBox &box = boxes[i];
			// A single colour cannot be split.
			if (box.lo[0] == box.hi[0] && box.lo[1] == box.hi[1] && box.lo[2] == box.hi[2])
				continue;
			if (best < 0 || box.count > boxes[best].count)
				best = i;
		}
		if (best < 0)
			break;

		const ScummBRIntroBox box = boxes[best];
		int axis = 0;
		for (int i = 1; i < 3; i++) {
			if (box.hi[i] - box.lo[i] > box.hi[axis] - box.lo[axis])
				axis = i;
		}

		// Pixel count for each position along the split axis.
		uint32 proj[32];
		memset(proj, 0, sizeof(proj));
		for (int r = box.lo[0]; r <= box.hi[0]; r++) {
			for (int g = box.lo[1]; g <= box.hi[1]; g++) {
				for (int b = box.lo[2]; b <= box.hi[2]; b++) {
					const int c[3] = { r, g, b };
					proj[c[axis]] += hist[scummBRIntroBinKey(r, g, b)];
				}
			}
		}

		// Cut where half of the pixels fall on each side.
		int cut = box.lo[axis];
		uint32 acc = 0;
		for (int v = box.lo[axis]; v < box.hi[axis]; v++) {
			cut = v;
			acc += proj[v];
			if (acc * 2 >= box.count)
				break;
		}

		ScummBRIntroBox left = box;
		ScummBRIntroBox right = box;
		left.hi[axis] = (uint8)cut;
		right.lo[axis] = (uint8)(cut + 1);
		scummBRIntroShrinkBox(left, hist);
		scummBRIntroShrinkBox(right, hist);

		boxes[best] = left;
		boxes.push_back(right);
	}

	memset(palette, 0, 256 * 3);
	memset(lut, 0, kScummBRIntroBins);
	for (uint i = 0; i < boxes.size(); i++) {
		const ScummBRIntroBox &box = boxes[i];
		uint64 sum[3] = { 0, 0, 0 };

		for (int r = box.lo[0]; r <= box.hi[0]; r++) {
			for (int g = box.lo[1]; g <= box.hi[1]; g++) {
				for (int b = box.lo[2]; b <= box.hi[2]; b++) {
					const int key = scummBRIntroBinKey(r, g, b);
					const uint32 n = hist[key];
					if (!n)
						continue;

					const int c[3] = { r, g, b };
					for (int k = 0; k < 3; k++)
						sum[k] += (uint64)n * scummBRIntroExpand(c[k]);
					lut[key] = (byte)(i + 1);
				}
			}
		}

		for (int k = 0; k < 3; k++)
			palette[(i + 1) * 3 + k] = (byte)(sum[k] / box.count);
	}
}
#endif

void AGOSEngine::playScummBRIntro() {
#ifdef USE_BINK
	if (getGameType() != GType_SIMON1 && getGameType() != GType_SIMON2)
		return;

	const Graphics::PixelFormat format = Graphics::PixelFormat::createFormatARGB32();

	// First pass: collect the colours the video uses, so that the palette
	// is built from the video itself. The playback below decodes the same frames.
	uint32 *hist = new uint32[kScummBRIntroBins]();
	{
		Video::BinkDecoder analyzer;
		if (!analyzer.loadStream(new Common::MemoryReadStream(kScummBRIntroBik, kScummBRIntroBikSize)) ||
			!analyzer.setOutputPixelFormat(format)) {
			delete[] hist;
			return;
		}

		const Graphics::Surface *frame;
		while ((frame = analyzer.decodeNextFrame()) != nullptr) {
			for (int y = 0; y < frame->h; y++) {
				const uint32 *row = (const uint32 *)frame->getBasePtr(0, y);
				for (int x = 0; x < frame->w; x++)
					hist[scummBRIntroPixelBin(row[x], format)]++;
			}
		}
	}

	byte palette[256 * 3];
	byte *lut = new byte[kScummBRIntroBins];
	scummBRIntroBuildPalette(hist, palette, lut);
	delete[] hist;

	Video::BinkDecoder bink;
	if (!bink.loadStream(new Common::MemoryReadStream(kScummBRIntroBik, kScummBRIntroBikSize)) ||
		!bink.setOutputPixelFormat(format)) {
		delete[] lut;
		return;
	}

	// Scale the video to cover the whole game screen, cropping the parts that
	// do not fit, so there are no black bars.
	const int screenW = _screenWidth;
	const int screenH = _screenHeight;
	const int videoW = bink.getWidth();
	const int videoH = bink.getHeight();
	int dstW = screenW;
	int dstH = screenH;
	if (videoW * screenH > videoH * screenW)
		dstW = videoW * screenH / videoH;
	else
		dstH = videoH * screenW / videoW;
	const int cropX = (dstW - screenW) / 2;
	const int cropY = (dstH - screenH) / 2;

	const bool mouseVisible = CursorMan.isVisible();
	CursorMan.showMouse(false);
	_system->getPaletteManager()->setPalette(palette, 0, 256);
	_mixer->stopAll();
	bink.start();

	// Esc starts a short fade out of the palette and the sound. Otherwise the
	// video plays to the end, and no other key or click skips it.
	const uint32 kFadeMs = 500;
	bool fading = false;
	uint32 fadeStart = 0;
	byte fadePalette[256 * 3];
	while (!shouldQuit()) {
		Common::Event event;
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_CUSTOM_ENGINE_ACTION_START && event.customType == kActionExitCutscene && !fading) {
				fading = true;
				fadeStart = _system->getMillis();
			}
		}

		if (fading) {
			const uint32 elapsed = _system->getMillis() - fadeStart;
			if (elapsed >= kFadeMs)
				break;

			const int level = 255 - elapsed * 255 / kFadeMs;
			for (int i = 0; i < 256 * 3; i++)
				fadePalette[i] = (byte)(palette[i] * level / 255);
			_system->getPaletteManager()->setPalette(fadePalette, 0, 256);
			bink.setVolume((byte)level);
		} else if (bink.endOfVideo()) {
			break;
		}

		if (bink.needsUpdate()) {
			const Graphics::Surface *frame = bink.decodeNextFrame();
			if (frame) {
				Graphics::Surface *screen = getBackendSurface();
				for (int y = 0; y < screenH; y++) {
					const uint32 *src = (const uint32 *)frame->getBasePtr(0, (y + cropY) * videoH / dstH);
					byte *dst = (byte *)screen->getBasePtr(0, y);
					for (int x = 0; x < screenW; x++)
						dst[x] = lut[scummBRIntroPixelBin(src[(x + cropX) * videoW / dstW], format)];
				}

				updateBackendSurface();
				_system->updateScreen();
			}
		}

		if (fading)
			_system->updateScreen();

		_system->delayMillis(10);
	}
	bink.stop();

	// Leave the screen black; the normal game loading takes over from here.
	Graphics::Surface *screen = getBackendSurface();
	for (int y = 0; y < screenH; y++)
		memset(screen->getBasePtr(0, y), 0, screenW);
	updateBackendSurface();
	memset(palette, 0, sizeof(palette));
	_system->getPaletteManager()->setPalette(palette, 0, 256);
	_system->updateScreen();

	CursorMan.showMouse(mouseVisible);
	delete[] lut;
#endif
}

Common::Error AGOSEngine::go() {
#ifdef ENABLE_AGOS2
	loadArchives();
#endif

	// The ScummBR intro comes with the pt-BR translation only: the built-in
	// one, or game files translated by the external package (detected by the
	// pt-BR entries). It plays before the game data is loaded.
	if (_traducaoPtBr || getLanguage() == Common::PT_BRA)
		playScummBRIntro();

	loadGamePcFile();

	addTimeEvent(0, 1);

	if (getFileName(GAME_GMEFILE) != nullptr) {
		openGameFile();
	}

	if (getGameType() == GType_FF) {
		loadIconData();
	} else if (getFileName(GAME_ICONFILE) != nullptr) {
		loadIconFile();
	}

	if (getFileName(GAME_MENUFILE) != nullptr) {
		loadMenuFile();
	}

	vc34_setMouseOff();

	if (getGameType() != GType_PP && getGameType() != GType_FF) {
		uint16 count = (getGameType() == GType_SIMON2) ? 5 : _frameCount;
		addVgaEvent(count, ANIMATE_INT, nullptr, 0, 0);
	}

	if (getGameType() == GType_ELVIRA1 && getPlatform() == Common::kPlatformAtariST &&
		(getFeatures() & GF_DEMO)) {
		setWindowImage(3, 9900);
		while (!shouldQuit())
			delay(0);
	}

	if (getGameType() == GType_ELVIRA1 && getPlatform() == Common::kPlatformAmiga &&
		(getFeatures() & GF_DEMO)) {
		playMusic(0, 0);
	}

	runSubroutine101();
	permitInput();

	while (!shouldQuit()) {
		waitForInput();

		if (getGameType() == GType_SIMON2 && (_keyPressed.keycode == Common::KEYCODE_SPACE) && hasSimon2LanguageFiles()) {
			cycleSimon2LanguageOverlay();
			_keyPressed.reset();
			continue;
		}

		handleVerbClicked(_verbHitArea);
		delay(100);
	}

	return Common::kNoError;
}


uint32 AGOSEngine::getTime() const {
	return _system->getMillis() / 1000;
}

void AGOSEngine::syncSoundSettings() {
	Engine::syncSoundSettings();

	int newMusicVolume = ConfMan.getInt("music_volume");
	int newEffectsVolume = ConfMan.getInt("sfx_volume");

	_musicMuted = newMusicVolume == 0;
	if (newMusicVolume != 0)
		_musicVolume = newMusicVolume;
	if (getGameType() == GType_SIMON2) {
		// Simon 2 has regular and ambient SFX, which can be toggled on and off
		// separately.
		if (newEffectsVolume == 0) {
			// Global SFX volume 0 mutes both regular and ambient SFX.
			_effectsMuted = _ambientMuted = true;
		} else {
			// If global SFX volume is > 0 and both regular and ambient SFX are
			// muted, unmute them. If only one of them is muted, the volume
			// change will only affect that type of SFX. If both are not muted,
			// it will affect both types.
			if (_effectsMuted && _ambientMuted)
				_effectsMuted = _ambientMuted = false;
			_effectsVolume = newEffectsVolume;
		}
		// Engine::syncSoundSettings applies SFX volume to all SFX handles,
		// so manage the regular and ambient handles separately here.
		_sound->effectsMute(_effectsMuted, _effectsVolume);
		_sound->ambientMute(_ambientMuted, _effectsVolume);
	} else {
		// Other games only have one SFX setting.
		_effectsMuted = newEffectsVolume == 0;
		if (newEffectsVolume != 0)
			_effectsVolume = newEffectsVolume;
	}
	_speech = !ConfMan.getBool("speech_mute");

	if (_midiEnabled)
		_midi->syncSoundSettings();
}

void AGOSEngine::syncSoundSettingsIntern() {
	ConfMan.setBool("speech_mute", !_speech);
	ConfMan.setInt("music_volume", _musicMuted ? 0 : _musicVolume);
	bool sfxMute = getGameType() == GType_SIMON2 ?
		_effectsMuted && _ambientMuted : _effectsMuted;
	ConfMan.setInt("sfx_volume", sfxMute ? 0 : _effectsVolume);

	Engine::syncSoundSettings();

	if (getGameType() == GType_SIMON2) {
		// Simon 2 has ambient sound effects, which can be toggled on and off
		// separately from the other SFX.
		// Engine::syncSoundSettings applies SFX volume to all SFX handles,
		// so manage the regular and ambient handles separately here.
		_sound->effectsMute(_effectsMuted, _effectsVolume);
		_sound->ambientMute(_ambientMuted, _effectsVolume);
	}

	if (_midiEnabled)
		_midi->syncSoundSettings();
}

static bool decodeSimon2LanguageFile(const Common::String &filename, Common::Array<Common::String> &entries) {
	Common::SeekableReadStream *in = SearchMan.createReadStreamForMember(Common::Path(filename));
	if (!in)
		return false;

	Common::String current;
	while (!in->eos()) {
		byte raw = in->readByte();
		byte decoded = raw + 1;
		if (decoded == 0) {
			entries.push_back(current);
			current.clear();
		} else {
			current += (char)decoded;
		}
	}

	delete in;
	return !entries.empty();
}

bool AGOSEngine::hasSimon2LanguageFiles() const {
	if (getGameType() != GType_SIMON2)
		return false;

	return SearchMan.hasFile("simon2.english") || SearchMan.hasFile("simon2.german") ||
		SearchMan.hasFile("simon2.italian") || SearchMan.hasFile("simon2.french");
}

Common::Language AGOSEngine::getNextSimon2OverlayLanguage() const {
	Common::Array<Common::Language> available;

	if (SearchMan.hasFile("simon2.english"))
		available.push_back(Common::EN_ANY);
	if (SearchMan.hasFile("simon2.german"))
		available.push_back(Common::DE_DEU);
	if (SearchMan.hasFile("simon2.italian"))
		available.push_back(Common::IT_ITA);
	if (SearchMan.hasFile("simon2.french"))
		available.push_back(Common::FR_FRA);

	if (available.empty())
		return _language;

	for (uint i = 0; i < available.size(); ++i) {
		if (available[i] == _language)
			return available[(i + 1) % available.size()];
	}

	return available[0];
}

void AGOSEngine::cycleSimon2LanguageOverlay() {
	if (!hasSimon2LanguageFiles())
		return;

	_language = getNextSimon2OverlayLanguage();
	_simon2OverlayLanguage = _language;
	loadSimon2LanguageOverlay();
	_simon2LanguageFlagTimer = 0x32;
	_simon2LanguageFlagClearPending = false;
	_displayFlag = 1;
}

void AGOSEngine::loadSimon2LanguageOverlay() {
	_useSimon2LanguageOverlay = false;
	_simon2LanguageOverlay.clear();

	if (!hasSimon2LanguageFiles())
		return;

	Common::String targetFile;
	switch (_language) {
	case Common::DE_DEU:
		targetFile = "simon2.german";
		break;
	case Common::IT_ITA:
		targetFile = "simon2.italian";
		break;
	case Common::FR_FRA:
		targetFile = "simon2.french";
		break;
	default:
		return;
	}

	Common::Array<Common::String> englishEntries;
	Common::Array<Common::String> translatedEntries;
	if (!decodeSimon2LanguageFile("simon2.english", englishEntries))
		return;
	if (!decodeSimon2LanguageFile(targetFile, translatedEntries))
		return;

	size_t count = MIN(englishEntries.size(), translatedEntries.size());
	for (size_t i = 0; i < count; ++i) {
		if (!englishEntries[i].empty() && englishEntries[i] != translatedEntries[i])
			_simon2LanguageOverlay.setVal(englishEntries[i], translatedEntries[i]);
	}

	_useSimon2LanguageOverlay = !_simon2LanguageOverlay.empty();
}

Common::String AGOSEngine::translateLanguageOverlay(const Common::String &english) const {
	if (!_useSimon2LanguageOverlay)
		return english;
	if (!_simon2LanguageOverlay.contains(english))
		return english;
	return _simon2LanguageOverlay.getValOrDefault(english);
}

} // End of namespace AGOS
