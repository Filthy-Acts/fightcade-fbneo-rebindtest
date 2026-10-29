// Burner In-game control binding Overlay
//
// Hold Start + Select (Coin) on any controller for 5 seconds to open it. Emulation pauses and an
// overlay (drawn by vid_overlay.cpp on top of the game) walks Player 1 and then Player 2 through
// every one of their digital game inputs. Each player presses the control they want for the
// highlighted input; when both are done the changes are saved and emulation resumes.
//
//   Tap a control ............ assign it to the highlighted input
//   Hold a control 1.5s ...... skip the input (keep its current binding)
//   Backspace (keyboard) ..... go back one input
//   Delete (keyboard) ........ clear the highlighted input
//   Esc (keyboard) ........... cancel, restoring every binding as it was
//
// On the review screen: tap Start + Select (the same combos that open the overlay, using the new
// bindings) to save and resume; a single-button tap does nothing there. Hold one of your own (just
// bound) controls for 2s to redo only your player's set; hold a control neither player uses to
// redo both.
//
// The open gesture is recognised two ways, so it still works when the bindings are broken:
//   - the controls currently mapped to "Px Start" + "Px Coin", for either player
//   - Start + Back on any connected XInput-style pad ("Joy n Button 7" + "Joy n Button 6" in the
//     input dialog's 0-based numbering)
//
// The overlay is offline-only: an online match can't be paused, so it just shows a message there.
// With a video renderer that has no overlay (anything other than DirectX9 / DirectX9 Alt), the
// gesture opens the classic input dialog instead.

#include "burner.h"

#define OPEN_HOLD_MS		5000		// how long Start + Select must be held to open the overlay
#define OPEN_HINT_MS		1000		// the progress bar appears after this long
#define SKIP_HOLD_MS		1500		// hold a control this long to skip an input
#define REDO_HOLD_MS		2000		// hold a control this long on the review screen to start over
#define TOAST_MS			2500

#define PAD_BACK			0x86		// XInput "Back" as seen through DirectInput (button 6, 0-based)
#define PAD_START			0x87		// XInput "Start" as seen through DirectInput (button 7, 0-based)
#define MAX_PADS			16

static InpoView View;
static unsigned int nItemInput[2][INPO_MAX_ITEMS];	// GameInp index of every listed item

static struct GameInp* pBackup = NULL;				// bindings as they were when the overlay opened
static struct GameInp* pBackupSource = NULL;		// GameInp array the backup belongs to
static unsigned int nBackupCount = 0;
static bool bOldLeftAltkeyMapped = false;
static bool bSaveOnClose = false;

// Capture state machine (same flags as InputFind / the Input Set dialog)
//  4 = waiting for every control to be released
//  8 = waiting for a control to be pressed
// 16 = waiting for the pressed control to be released
static int nCapState = 4;
static int nRedoPlayer = -1;						// player redoing their set on their own, -1 = both
static int nCapCode = -1;
static DWORD nCapPressTime = 0;

// Open gesture
static DWORD nOpenHoldStart = 0;
static bool bOpenHoldFired = false;

// Cached indices of P1/P2 Start and Coin, per driver
static UINT32 nTrigDrv = ~0U;
static UINT32 nTrigCount = ~0U;
static int nTrigStart[2] = { -1, -1 };
static int nTrigCoin[2] = { -1, -1 };

static wchar_t szToast[96] = L"";
static DWORD nToastUntil = 0;

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------

static void Toast(const wchar_t* pszText)
{
	wcsncpy(szToast, pszText, 95);
	szToast[95] = 0;
	nToastUntil = timeGetTime() + TOAST_MS;
	VidSNewShortMsg(pszText);						// for renderers without the overlay
}

// A control counts as "held" for the overlay unless it's a mouse control (those are ignored,
// exactly like the Input Set dialog does when "grab mouse" is off)
static inline bool Held(int nFind)
{
	return nFind >= 0 && nFind < 0x8000;
}

static void DescribeCode(int c, wchar_t* pszOut)
{
	static const wchar_t* szDir[4] = { L"Left", L"Right", L"Up", L"Down" };
	static const wchar_t* szAxis[8] = { L"X", L"Y", L"Z", L"rX", L"rY", L"rZ", L"s0", L"s1" };

	if (c >= 0x4000 && c < 0x8000) {
		int nJoy = (c >> 8) & 0x3F;
		int nCode = c & 0xFF;
		if (nCode >= 0x80) {
			swprintf(pszOut, INPO_TEXT_LEN, L"Joy %d Button %d", nJoy, nCode & 0x7F);
			return;
		}
		if (nCode < 0x04) {
			swprintf(pszOut, INPO_TEXT_LEN, L"Joy %d %ls", nJoy, szDir[nCode]);
			return;
		}
		if (nCode < 0x10) {
			swprintf(pszOut, INPO_TEXT_LEN, L"Joy %d %ls%ls", nJoy, szAxis[nCode >> 1], (nCode & 1) ? L"+" : L"-");
			return;
		}
		if (nCode < 0x20) {
			swprintf(pszOut, INPO_TEXT_LEN, L"Joy %d Hat %ls", nJoy, szDir[nCode & 3]);
			return;
		}
	}

	wcsncpy(pszOut, InputCodeDesc(c), INPO_TEXT_LEN - 1);
	pszOut[INPO_TEXT_LEN - 1] = 0;
}

static void DescribeInput(unsigned int i, wchar_t* pszOut)
{
	struct GameInp* pgi = GameInp + i;

	switch (pgi->nInput) {
		case GIT_SWITCH:
			if (pgi->Input.Switch.nCode == 0) {
				wcscpy(pszOut, L"-");
			} else {
				DescribeCode(pgi->Input.Switch.nCode, pszOut);
			}
			break;
		case GIT_CONSTANT:
			wcscpy(pszOut, L"Constant");
			break;
		case 0:
			wcscpy(pszOut, L"-");
			break;
		default:
			wcscpy(pszOut, L"Analog");
			break;
	}
}

static void RefreshBindings()
{
	for (int p = 0; p < 2; p++) {
		for (int n = 0; n < View.nCount[p]; n++) {
			DescribeInput(nItemInput[p][n], View.szBind[p][n]);
		}
	}
}

// List every digital "P1 ..." / "P2 ..." input of the running game
static void BuildItems()
{
	View.nCount[0] = View.nCount[1] = 0;

	for (unsigned int i = 0; i < nGameInpCount; i++) {
		struct BurnInputInfo bii;
		memset(&bii, 0, sizeof(bii));
		if (BurnDrvGetInputInfo(&bii, i)) {
			continue;
		}
		if (bii.nType != BIT_DIGITAL || bii.szInfo == NULL || bii.szName == NULL) {
			continue;
		}

		int p = -1;
		if (strncmp(bii.szInfo, "p1 ", 3) == 0) p = 0;
		if (strncmp(bii.szInfo, "p2 ", 3) == 0) p = 1;
		if (p < 0 || View.nCount[p] >= INPO_MAX_ITEMS) {
			continue;
		}

		// "P1 Weak Punch" -> "Weak Punch"
		const char* pszName = bii.szName;
		if ((pszName[0] == 'P' || pszName[0] == 'p') && pszName[1] == '1' + p && pszName[2] == ' ') {
			pszName += 3;
		}

		int n = View.nCount[p]++;
		nItemInput[p][n] = i;
		int c = 0;
		for (; c < INPO_TEXT_LEN - 1 && pszName[c]; c++) {
			View.szName[p][n][c] = (unsigned char)pszName[c];		// driver input names are plain ASCII
		}
		View.szName[p][n][c] = 0;
	}

	RefreshBindings();
}

// ---------------------------------------------------------------------------------------------
// open gesture
// ---------------------------------------------------------------------------------------------

static void CacheTriggerInputs()
{
	if (nTrigDrv == nBurnDrvActive && nTrigCount == nGameInpCount) {
		return;
	}
	nTrigDrv = nBurnDrvActive;
	nTrigCount = nGameInpCount;
	nTrigStart[0] = nTrigStart[1] = nTrigCoin[0] = nTrigCoin[1] = -1;

	for (unsigned int i = 0; i < nGameInpCount; i++) {
		struct BurnInputInfo bii;
		memset(&bii, 0, sizeof(bii));
		if (BurnDrvGetInputInfo(&bii, i) || bii.szInfo == NULL) {
			continue;
		}
		if (strcmp(bii.szInfo, "p1 start") == 0) nTrigStart[0] = i;
		if (strcmp(bii.szInfo, "p2 start") == 0) nTrigStart[1] = i;
		if (strcmp(bii.szInfo, "p1 coin") == 0) nTrigCoin[0] = i;
		if (strcmp(bii.szInfo, "p2 coin") == 0) nTrigCoin[1] = i;
	}
}

static bool MappedSwitchHeld(int i)
{
	if (i < 0 || (unsigned int)i >= nGameInpCount) {
		return false;
	}
	struct GameInp* pgi = GameInp + i;
	if (pgi->nInput != GIT_SWITCH || pgi->Input.Switch.nCode == 0) {
		return false;
	}
	return InputReadSwitch(pgi->Input.Switch.nCode) != 0;
}

static bool OpenGestureHeld()
{
	// The controls the player has mapped to Start + Coin
	CacheTriggerInputs();
	for (int p = 0; p < 2; p++) {
		if (MappedSwitchHeld(nTrigStart[p]) && MappedSwitchHeld(nTrigCoin[p])) {
			return true;
		}
	}

	// Start + Back on any XInput-style pad, whatever it is currently mapped to
	for (int i = 0; i < MAX_PADS; i++) {
		int nBase = 0x4000 | (i << 8);
		if (InputReadSwitch(nBase | PAD_START) && InputReadSwitch(nBase | PAD_BACK)) {
			return true;
		}
	}

	return false;
}

// ---------------------------------------------------------------------------------------------
// binding state machine
// ---------------------------------------------------------------------------------------------

static void StartCapture()
{
	InputFind(2);									// new baseline for analog axes
	nCapState = 4;
	nCapCode = -1;
	View.bWaitingRelease = 1;
	View.fHoldProgress = 0.f;
	View.szPressed[0] = 0;
	View.nHoldTarget = -1;
	View.bSaveChord = 0;
}

// nPlayer = 0 / 1: (re)bind only that player's set, -1: both players, Player 1 first
static void FirstItem(int nPlayer = -1)
{
	nRedoPlayer = (nPlayer >= 0 && View.nCount[nPlayer] > 0) ? nPlayer : -1;
	View.nPhase = INPO_PHASE_BIND;
	View.nPlayer = (nRedoPlayer >= 0) ? nRedoPlayer : (View.nCount[0] ? 0 : 1);
	View.nOnlyPlayer = nRedoPlayer;
	View.nItem = 0;
}

// Which player's set a control is bound to (0 / 1), or -1 when it belongs to neither
static int OwnerOf(int nCode)
{
	for (int p = 0; p < 2; p++) {
		for (int n = 0; n < View.nCount[p]; n++) {
			struct GameInp* pgi = GameInp + nItemInput[p][n];
			if (pgi->nInput == GIT_SWITCH && pgi->Input.Switch.nCode == nCode) {
				return p;
			}
		}
	}
	return -1;
}

static void Advance()
{
	View.nItem++;
	if (View.nItem < View.nCount[View.nPlayer]) {
		return;
	}
	if (nRedoPlayer < 0 && View.nPlayer == 0 && View.nCount[1] > 0) {
		View.nPlayer = 1;
		View.nItem = 0;
		return;
	}
	View.nPhase = INPO_PHASE_REVIEW;
	View.nItem = 0;
}

static void StepBack()
{
	if (View.nItem > 0) {
		View.nItem--;
	} else if (nRedoPlayer < 0 && View.nPlayer == 1 && View.nCount[0] > 0) {
		View.nPlayer = 0;
		View.nItem = View.nCount[0] - 1;
	}
}

// Assign nCode to the highlighted input (0 clears it)
static void Assign(int nCode)
{
	int p = View.nPlayer;
	int n = View.nItem;
	struct GameInp* pgi = GameInp + nItemInput[p][n];

	if (nCode) {
		// A control drives one player input: drop it from any other P1/P2 input that used it
		for (int q = 0; q < 2; q++) {
			for (int m = 0; m < View.nCount[q]; m++) {
				if (q == p && m == n) {
					continue;
				}
				struct GameInp* pOther = GameInp + nItemInput[q][m];
				if (pOther->nInput == GIT_SWITCH && pOther->Input.Switch.nCode == nCode) {
					pOther->nInput = 0;
					pOther->Input.Switch.nCode = 0;
				}
			}
		}
		pgi->nInput = GIT_SWITCH;
		pgi->Input.Switch.nCode = (UINT16)nCode;
	} else {
		pgi->nInput = 0;
		pgi->Input.Switch.nCode = 0;
	}

	RefreshBindings();
}

static void Open()
{
	BuildItems();
	if (View.nCount[0] + View.nCount[1] == 0) {
		Toast(L"This game has no player controls to bind");
		return;
	}

	pBackup = (struct GameInp*)malloc(nGameInpCount * sizeof(struct GameInp));
	if (pBackup == NULL) {
		return;
	}
	memcpy(pBackup, GameInp, nGameInpCount * sizeof(struct GameInp));
	pBackupSource = GameInp;
	nBackupCount = nGameInpCount;

	// Let Left Alt be captured as a control without activating the menu bar (same as the Input Set dialog)
	bOldLeftAltkeyMapped = bLeftAltkeyMapped;
	bLeftAltkeyMapped = true;

	bSaveOnClose = false;
	nToastUntil = 0;
	FirstItem();
	StartCapture();								// Start + Select are still held: wait for them to be released

	AudBlankSound();
}

// Drop the overlay without touching any binding (driver went away, etc.)
static void Abandon()
{
	free(pBackup);
	pBackup = NULL;
	pBackupSource = NULL;
	nBackupCount = 0;
	View.nPhase = INPO_PHASE_CLOSED;
	bLeftAltkeyMapped = bOldLeftAltkeyMapped;
}

static void Finish()
{
	Abandon();
	GameInpCheckLeftAlt();

	if (bSaveOnClose) {
		if (bSaveInputs) {
			ConfigGameSave(true);
		}
		Toast(L"Controls saved");
	} else {
		Toast(L"Changes discarded");
	}

	// Don't re-open until the gesture is released and held again
	nOpenHoldStart = 0;
	bOpenHoldFired = true;
}

void InpoClose(bool bSave)
{
	if (View.nPhase == INPO_PHASE_CLOSED || View.nPhase == INPO_PHASE_CLOSING) {
		return;
	}

	if (!bSave && pBackup && pBackupSource == GameInp && nBackupCount == nGameInpCount) {
		memcpy(GameInp, pBackup, nGameInpCount * sizeof(struct GameInp));
		RefreshBindings();
	}
	bSaveOnClose = bSave;

	// Resume only once nothing is held, so the button that closed the overlay never reaches the game
	View.nPhase = INPO_PHASE_CLOSING;
	View.fHoldProgress = 0.f;
	View.szPressed[0] = 0;
}

static void UpdateActive()
{
	DWORD nNow = timeGetTime();

	if (View.nPhase == INPO_PHASE_CLOSING) {
		if (!Held(InputFind(4))) {
			Finish();
		}
		return;
	}

	int nFind = InputFind(nCapState);

	switch (nCapState) {
		case 4: {									// waiting for everything to be released
			if (Held(nFind)) {
				return;
			}
			nCapState = 8;
			View.bWaitingRelease = 0;
			return;
		}

		case 8: {									// waiting for a press
			if (!Held(nFind)) {
				return;
			}

			// Reserved keyboard keys
			if (nFind == FBK_ESCAPE) {
				InpoClose(false);
				return;
			}
			if (View.nPhase == INPO_PHASE_BIND && nFind == FBK_BACK) {
				StepBack();
				StartCapture();
				return;
			}
			if (View.nPhase == INPO_PHASE_BIND && nFind == FBK_DELETE) {
				Assign(0);
				Advance();
				StartCapture();
				return;
			}

			nCapCode = nFind;
			nCapPressTime = nNow;
			nCapState = 16;
			DescribeCode(nCapCode, View.szPressed);
			return;
		}

		case 16: {									// waiting for the release
			DWORD nHoldMs = (View.nPhase == INPO_PHASE_REVIEW) ? REDO_HOLD_MS : SKIP_HOLD_MS;
			DWORD nHeld = nNow - nCapPressTime;

			if (Held(nFind)) {
				if (nFind != nCapCode) {			// e.g. an axis settling on its final direction
					nCapCode = nFind;
					DescribeCode(nCapCode, View.szPressed);
				}
				View.fHoldProgress = (nHeld >= nHoldMs) ? 1.f : (float)nHeld / (float)nHoldMs;
				if (View.nPhase == INPO_PHASE_REVIEW) {
					View.nHoldTarget = OwnerOf(nCapCode);
					if (OpenGestureHeld()) {
						View.bSaveChord = 1;		// once seen, Start + Select wins over any hold
					}
					if (View.bSaveChord) {
						View.fHoldProgress = 0.f;
					}
				}
				return;
			}

			bool bLongHold = nHeld >= nHoldMs;

			if (View.nPhase == INPO_PHASE_REVIEW) {
				if (View.bSaveChord) {
					InpoClose(true);				// waits for every control to be released, then saves
				} else if (bLongHold) {
					// Holding one of your own controls redoes just your set; any other control redoes both
					FirstItem(OwnerOf(nCapCode));
					StartCapture();
				} else {
					StartCapture();					// a single-button tap doesn't close the review screen
				}
				return;
			}

			if (!bLongHold) {
				Assign(nCapCode);
			}
			Advance();
			StartCapture();
			return;
		}
	}

	StartCapture();
}

// ---------------------------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------------------------

bool InpoIsActive()
{
	return View.nPhase != INPO_PHASE_CLOSED;
}

// Called from GetInput() after InputMake(), so the input devices have just been polled
void InpoUpdate()
{
	if (View.nPhase != INPO_PHASE_CLOSED) {
		if (!bDrvOkay || pBackupSource != GameInp || nBackupCount != nGameInpCount) {
			Abandon();								// the game was closed / changed underneath us
			return;
		}
		if (View.nPhase != INPO_PHASE_CLOSING) {
			if (hInpdDlg || hInpsDlg) {
				InpoClose(true);					// input dialog opened from the menu: keep what's done, let it take over
			} else if (!VidOverlayIsReady()) {
				InpoClose(false);					// switched to a renderer that can't draw us: don't stay paused invisibly
			}
		}
		UpdateActive();
		return;
	}

	View.fOpenProgress = 0.f;

	if (!bDrvOkay || !bHasFocus || hInpdDlg || hInpsDlg || bEditActive || !OpenGestureHeld()) {
		nOpenHoldStart = 0;
		bOpenHoldFired = false;
		return;
	}
	if (bOpenHoldFired) {							// already handled this hold, wait for a release
		return;
	}

	DWORD nNow = timeGetTime();
	if (nOpenHoldStart == 0) {
		nOpenHoldStart = nNow ? nNow : 1;
	}
	DWORD nHeld = nNow - nOpenHoldStart;

	if (!kNetGame && nHeld > OPEN_HINT_MS) {
		View.fOpenProgress = (float)(nHeld - OPEN_HINT_MS) / (float)(OPEN_HOLD_MS - OPEN_HINT_MS);
		if (View.fOpenProgress > 1.f) {
			View.fOpenProgress = 1.f;
		}
	}
	if (nHeld < OPEN_HOLD_MS) {
		return;
	}

	bOpenHoldFired = true;
	View.fOpenProgress = 0.f;

	if (kNetGame) {
		Toast(L"Controls can't be changed during an online match");
		return;
	}

	if (!VidOverlayIsReady()) {
		// This video renderer can't draw the overlay: fall back to the regular input dialog
		PostMessage(hScrnWnd, WM_COMMAND, MAKEWPARAM(MENU_INPUT, 0), 0);
		return;
	}

	Open();
}

bool InpoSwallowMessage(const MSG* pMsg)
{
	if (View.nPhase == INPO_PHASE_CLOSED) {
		return false;
	}
	if (pMsg->message < WM_KEYFIRST || pMsg->message > WM_KEYLAST) {
		return false;
	}
	if (pMsg->hwnd != hScrnWnd && !IsChild(hScrnWnd, pMsg->hwnd)) {
		return false;
	}
	// Keep Alt+F4 working, everything else (Esc, Backspace, F-keys, accelerators...) belongs to the overlay
	if (pMsg->message == WM_SYSKEYDOWN && pMsg->wParam == VK_F4) {
		return false;
	}
	return true;
}

const InpoView* InpoGetView()
{
	if (nToastUntil && (int)(timeGetTime() - nToastUntil) < 0) {
		wcscpy(View.szToast, szToast);
	} else {
		View.szToast[0] = 0;
		nToastUntil = 0;
	}
	return &View;
}
