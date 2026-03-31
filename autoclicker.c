/*
 *  AutoClicker v1.0 -- WinAPI / C  (Unicode build)
 *
 *  Compile (MinGW):
 *    i686-w64-mingw32-gcc autoclicker.c -o autoclicker.exe \
 *        -luser32 -lgdi32 -mwindows -O2 -municode
 *
 *  Compile (MSVC, Developer Command Prompt):
 *    cl autoclicker.c user32.lib gdi32.lib /link /subsystem:windows
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

/* ── Control IDs ────────────────────────────────────────────── */
enum {
    IDC_RADIO_MOUSE = 100,
    IDC_RADIO_KEY,
    IDC_COMBO_BTN,
    IDC_COMBO_TYPE,
    IDC_EDIT_SECS,
    IDC_EDIT_MS,
    IDC_BTN_START,
    IDC_BTN_RECORD,
    IDC_LBL_COUNT,
    IDC_LBL_POS,
    IDC_CHK_FREEZE,
    IDC_LBL_KEY,
    IDC_GRP_MOUSE,
    IDC_GRP_KEY
};

#define TID_CLICKER  1
#define TID_POS      2
#define HK_F3        1

/* ── State ───────────────────────────────────────────────────── */
static HWND   g_hwnd    = NULL;
static HFONT  g_font    = NULL;
static BOOL   g_running = FALSE;
static long   g_count   = 0;
static DWORD  g_vk      = VK_SPACE;
static WCHAR  g_keyName[64] = L"Space";
static BOOL   g_capturing   = FALSE;
static HHOOK  g_kbHook      = NULL;
static POINT  g_frozenPt;

/* ── Forward declarations ────────────────────────────────────── */
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK LowKeyHook(int, WPARAM, LPARAM);
BOOL    CALLBACK SetFontCb(HWND, LPARAM);

/* ── Helpers ─────────────────────────────────────────────────── */
static HWND H(int id)       { return GetDlgItem(g_hwnd, id); }
static BOOL MouseMode(void) { return SendMessage(H(IDC_RADIO_MOUSE), BM_GETCHECK, 0, 0) == BST_CHECKED; }
static BOOL FreezeOn(void)  { return SendMessage(H(IDC_CHK_FREEZE),  BM_GETCHECK, 0, 0) == BST_CHECKED; }

static DWORD GetIntervalMs(void) {
    WCHAR buf[16];
    GetWindowText(H(IDC_EDIT_SECS), buf, 16); DWORD s = (DWORD)_wtoi(buf);
    GetWindowText(H(IDC_EDIT_MS),   buf, 16); DWORD m = (DWORD)_wtoi(buf);
    DWORD t = s * 1000 + m;
    return t < 1 ? 1 : t;
}

/* Create a child control and immediately apply the shared font */
static HWND MakeCtrl(const WCHAR *cls, const WCHAR *text, DWORD style,
                     int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowEx(
        0, cls, text,
        WS_CHILD | WS_VISIBLE | style,
        x, y, w, h,
        g_hwnd, (HMENU)(UINT_PTR)id,
        GetModuleHandle(NULL), NULL);
    if (g_font && c)
        SendMessage(c, WM_SETFONT, (WPARAM)g_font, FALSE);
    return c;
}

static void UpdateCount(void) {
    WCHAR buf[48];
    swprintf(buf, 48, L"Clicks: %ld", g_count);
    SetWindowText(H(IDC_LBL_COUNT), buf);
}

/* ── Core: mouse click ───────────────────────────────────────── */
static void DoMouseClick(void) {
    int btn = (int)SendMessage(H(IDC_COMBO_BTN),  CB_GETCURSEL, 0, 0);
    int dbl = (int)SendMessage(H(IDC_COMBO_TYPE), CB_GETCURSEL, 0, 0);

    DWORD dn, up;
    switch (btn) {
        case 1:  dn = MOUSEEVENTF_RIGHTDOWN;  up = MOUSEEVENTF_RIGHTUP;  break;
        case 2:  dn = MOUSEEVENTF_MIDDLEDOWN; up = MOUSEEVENTF_MIDDLEUP; break;
        default: dn = MOUSEEVENTF_LEFTDOWN;   up = MOUSEEVENTF_LEFTUP;   break;
    }

    int i, n = dbl ? 2 : 1;
    for (i = 0; i < n; i++) {
        INPUT inp[2];
        memset(inp, 0, sizeof(inp));
        inp[0].type = INPUT_MOUSE; inp[0].mi.dwFlags = dn;
        inp[1].type = INPUT_MOUSE; inp[1].mi.dwFlags = up;
        SendInput(2, inp, sizeof(INPUT));
        if (dbl && i == 0) Sleep(20);
    }

    if (FreezeOn()) SetCursorPos(g_frozenPt.x, g_frozenPt.y);
}

/* ── Core: key press ─────────────────────────────────────────── */
static void DoKeyPress(void) {
    INPUT inp[2];
    memset(inp, 0, sizeof(inp));
    inp[0].type       = INPUT_KEYBOARD;
    inp[0].ki.wVk     = (WORD)g_vk;
    inp[1].type       = INPUT_KEYBOARD;
    inp[1].ki.wVk     = (WORD)g_vk;
    inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inp, sizeof(INPUT));
}

/* ── Start / Stop ────────────────────────────────────────────── */
static void StartClicker(void) {
    if (g_running) return;
    g_running = TRUE;
    g_count   = 0;
    UpdateCount();
    if (FreezeOn()) GetCursorPos(&g_frozenPt);
    SetWindowText(H(IDC_BTN_START), L"Stop  [F3]");
    SetTimer(g_hwnd, TID_CLICKER, GetIntervalMs(), NULL);
}

static void StopClicker(void) {
    if (!g_running) return;
    g_running = FALSE;
    KillTimer(g_hwnd, TID_CLICKER);
    SetWindowText(H(IDC_BTN_START), L"Start  [F3]");
}

static void ToggleRunning(void) {
    if (g_running) StopClicker(); else StartClicker();
}

/* ── Low-level keyboard hook (key capture) ───────────────────── */
LRESULT CALLBACK LowKeyHook(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode == HC_ACTION && wp == WM_KEYDOWN && g_capturing) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lp;

        if (kb->vkCode != VK_ESCAPE && kb->vkCode != VK_F3) {
            g_vk = kb->vkCode;
            GetKeyNameText((LONG)(kb->scanCode << 16), g_keyName, 64);
            WCHAR buf[80];
            swprintf(buf, 80, L"Key: [%s]", g_keyName);
            SetWindowText(H(IDC_LBL_KEY), buf);
        }

        SetWindowText(H(IDC_BTN_RECORD), L"Record");
        g_capturing = FALSE;
        UnhookWindowsHookEx(g_kbHook);
        g_kbHook = NULL;
        return 1; /* consume event */
    }
    return CallNextHookEx(g_kbHook, nCode, wp, lp);
}

static void StartKeyCapture(void) {
    if (g_running) StopClicker();
    g_capturing = TRUE;
    SetWindowText(H(IDC_BTN_RECORD), L"Press a key...");
    g_kbHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowKeyHook,
                                GetModuleHandle(NULL), 0);
}

/* ── Mode switch ─────────────────────────────────────────────── */
static void SwitchToMouse(void) {
    ShowWindow(H(IDC_GRP_MOUSE),  SW_SHOW);
    ShowWindow(H(IDC_COMBO_BTN),  SW_SHOW);
    ShowWindow(H(IDC_COMBO_TYPE), SW_SHOW);
    ShowWindow(H(IDC_LBL_POS),    SW_SHOW);
    ShowWindow(H(IDC_CHK_FREEZE), SW_SHOW);
    ShowWindow(H(IDC_GRP_KEY),    SW_HIDE);
    ShowWindow(H(IDC_LBL_KEY),    SW_HIDE);
    ShowWindow(H(IDC_BTN_RECORD), SW_HIDE);
    if (g_running) StopClicker();
}

static void SwitchToKey(void) {
    ShowWindow(H(IDC_GRP_MOUSE),  SW_HIDE);
    ShowWindow(H(IDC_COMBO_BTN),  SW_HIDE);
    ShowWindow(H(IDC_COMBO_TYPE), SW_HIDE);
    ShowWindow(H(IDC_LBL_POS),    SW_HIDE);
    ShowWindow(H(IDC_CHK_FREEZE), SW_HIDE);
    ShowWindow(H(IDC_GRP_KEY),    SW_SHOW);
    ShowWindow(H(IDC_LBL_KEY),    SW_SHOW);
    ShowWindow(H(IDC_BTN_RECORD), SW_SHOW);
    if (g_running) StopClicker();
}

/* ── Font enum callback ──────────────────────────────────────── */
BOOL CALLBACK SetFontCb(HWND child, LPARAM font) {
    SendMessage(child, WM_SETFONT, (WPARAM)font, FALSE);
    return TRUE;
}

/* ── Window Procedure ────────────────────────────────────────── */
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        g_hwnd = hwnd;
        g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        /* Mode group */
        MakeCtrl(L"BUTTON", L"Mode", BS_GROUPBOX,
                 6, 4, 360, 50, 0);
        MakeCtrl(L"BUTTON", L"Mouse",
                 BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                 20, 22, 80, 22, IDC_RADIO_MOUSE);
        MakeCtrl(L"BUTTON", L"Keyboard",
                 BS_AUTORADIOBUTTON | WS_TABSTOP,
                 110, 22, 100, 22, IDC_RADIO_KEY);
        SendMessage(H(IDC_RADIO_MOUSE), BM_SETCHECK, BST_CHECKED, 0);

        /* Mouse panel */
        MakeCtrl(L"BUTTON", L"Mouse Settings", BS_GROUPBOX,
                 6, 58, 360, 108, IDC_GRP_MOUSE);

        MakeCtrl(L"STATIC", L"Button:", SS_CENTERIMAGE,
                 18, 77, 55, 20, 0);
        {
            HWND cb = MakeCtrl(L"COMBOBOX", NULL,
                               CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                               76, 75, 90, 80, IDC_COMBO_BTN);
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)L"Left");
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)L"Right");
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)L"Middle");
            SendMessage(cb, CB_SETCURSEL, 0, 0);
        }

        MakeCtrl(L"STATIC", L"Type:", SS_CENTERIMAGE,
                 180, 77, 35, 20, 0);
        {
            HWND ct = MakeCtrl(L"COMBOBOX", NULL,
                               CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                               218, 75, 100, 80, IDC_COMBO_TYPE);
            SendMessage(ct, CB_ADDSTRING, 0, (LPARAM)L"Single");
            SendMessage(ct, CB_ADDSTRING, 0, (LPARAM)L"Double");
            SendMessage(ct, CB_SETCURSEL, 0, 0);
        }

        MakeCtrl(L"STATIC", L"Position:", SS_CENTERIMAGE,
                 18, 108, 62, 20, 0);
        MakeCtrl(L"STATIC", L"X:0    Y:0", SS_CENTERIMAGE,
                 84, 108, 170, 20, IDC_LBL_POS);

        MakeCtrl(L"BUTTON", L"Freeze cursor", BS_AUTOCHECKBOX | WS_TABSTOP,
                 18, 136, 150, 20, IDC_CHK_FREEZE);

        /* Keyboard panel (hidden by default) */
        MakeCtrl(L"BUTTON", L"Keyboard Settings", BS_GROUPBOX,
                 6, 58, 360, 108, IDC_GRP_KEY);
        MakeCtrl(L"STATIC", L"Key: [Space]", SS_CENTERIMAGE,
                 18, 90, 200, 24, IDC_LBL_KEY);
        MakeCtrl(L"BUTTON", L"Record", BS_PUSHBUTTON | WS_TABSTOP,
                 228, 88, 120, 28, IDC_BTN_RECORD);

        ShowWindow(H(IDC_GRP_KEY),    SW_HIDE);
        ShowWindow(H(IDC_LBL_KEY),    SW_HIDE);
        ShowWindow(H(IDC_BTN_RECORD), SW_HIDE);

        /* Interval group */
        MakeCtrl(L"BUTTON", L"Interval", BS_GROUPBOX,
                 6, 172, 360, 52, 0);
        MakeCtrl(L"STATIC", L"Seconds:", SS_CENTERIMAGE,
                 18, 190, 62, 20, 0);
        MakeCtrl(L"EDIT", L"0",
                 ES_NUMBER | ES_CENTER | WS_BORDER | WS_TABSTOP,
                 82, 188, 60, 24, IDC_EDIT_SECS);
        MakeCtrl(L"STATIC", L"Ms:", SS_CENTERIMAGE,
                 160, 190, 30, 20, 0);
        MakeCtrl(L"EDIT", L"100",
                 ES_NUMBER | ES_CENTER | WS_BORDER | WS_TABSTOP,
                 194, 188, 65, 24, IDC_EDIT_MS);

        /* Counter + Start button */
        MakeCtrl(L"STATIC", L"Clicks: 0", SS_CENTERIMAGE,
                 8, 232, 180, 22, IDC_LBL_COUNT);

        MakeCtrl(L"BUTTON", L"Start  [F3]",
                 BS_PUSHBUTTON | BS_CENTER | WS_TABSTOP,
                 8, 258, 360, 36, IDC_BTN_START);

        MakeCtrl(L"STATIC", L"F3 -- start/stop from any window",
                 SS_CENTER, 8, 300, 360, 18, 0);

        /* Apply font to all children */
        EnumChildWindows(hwnd, SetFontCb, (LPARAM)g_font);

        /* Global F3 hotkey */
        RegisterHotKey(hwnd, HK_F3, 0, VK_F3);

        /* Live cursor position update */
        SetTimer(hwnd, TID_POS, 80, NULL);
        return 0;
    }

    case WM_TIMER: {
        if (wp == TID_CLICKER) {
            if (MouseMode()) DoMouseClick();
            else             DoKeyPress();
            g_count++;
            UpdateCount();
        }
        if (wp == TID_POS && MouseMode()) {
            POINT pt;
            GetCursorPos(&pt);
            WCHAR buf[32];
            swprintf(buf, 32, L"X:%-5d  Y:%-5d", pt.x, pt.y);
            SetWindowText(H(IDC_LBL_POS), buf);
        }
        return 0;
    }

    case WM_HOTKEY:
        if (LOWORD(wp) == HK_F3) ToggleRunning();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BTN_START:   ToggleRunning();   break;
        case IDC_BTN_RECORD:  StartKeyCapture(); break;
        case IDC_RADIO_MOUSE: SwitchToMouse();   break;
        case IDC_RADIO_KEY:   SwitchToKey();     break;
        }
        return 0;

    case WM_DESTROY:
        if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = NULL; }
        UnregisterHotKey(hwnd, HK_F3);
        KillTimer(hwnd, TID_CLICKER);
        KillTimer(hwnd, TID_POS);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ── Entry point ─────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;

    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"AC_WNDCLASS";
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        L"AC_WNDCLASS",
        L"AutoClicker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 388, 350,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
