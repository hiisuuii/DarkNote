#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include "resource.h"

#define COL_BG RGB(30, 30, 30)
#define COL_TEXT RGB(220, 220, 220)
#define COL_PANEL RGB(43, 43, 43)
#define COL_HOT RGB(65, 65, 65)
#define COL_DISABLED RGB(120, 120, 120)
#define COL_DLG RGB(32, 32, 32)
#define COL_INPUT RGB(45, 45, 45)
#define COL_SEP RGB(70, 70, 70)

/* Undocumented menu bar drawing messages; the only way to theme the menu bar itself. */
#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092
typedef struct { HMENU hmenu; HDC hdc; DWORD dwFlags; } UAHMENU;
typedef struct { DWORD rgcx[4]; DWORD fUpdateMaxWidths : 2; } UAHMENUPOPUPMETRICS;
typedef union {
    struct { DWORD cx, cy; } rgsizeBar[2];
    struct { DWORD cx, cy; } rgsizePopup[4];
} UAHMENUITEMMETRICS;
typedef struct { int iPosition; UAHMENUITEMMETRICS umim; UAHMENUPOPUPMETRICS umpm; } UAHMENUITEM;
typedef struct { DRAWITEMSTRUCT dis; UAHMENU um; UAHMENUITEM umi; } UAHDRAWMENUITEM;

/* comctl32 v6 edit control extensions, missing from the MinGW headers */
#ifndef EM_SETEXTENDEDSTYLE
#define EM_SETEXTENDEDSTYLE (ECM_FIRST + 10)
#endif
#define EM_SETENDOFLINE_ (ECM_FIRST + 12)
#define EM_GETCARETINDEX_ (ECM_FIRST + 18)
#define EM_FILELINEFROMCHAR_ (ECM_FIRST + 19)
#define EM_FILELINEINDEX_ (ECM_FIRST + 20)
#define EM_GETFILELINECOUNT_ (ECM_FIRST + 23)
#define ES_EX_ALLOWEOL_ALL_ 0x0003
#define ES_EX_CONVERT_EOL_ON_PASTE_ 0x0004
#define ES_EX_ZOOMABLE_ 0x0010
#define EM_GETZOOM_ (WM_USER + 224)
#define EM_SETZOOM_ (WM_USER + 225)

enum { EOL_CRLF = 1, EOL_CR = 2, EOL_LF = 3 };
enum { ENC_ANSI, ENC_UTF16LE, ENC_UTF16BE, ENC_UTF8, ENC_UTF8BOM, ENC_COUNT };
#define ENC_AUTO 100

#define ID_EDIT 1
#define ID_STATUS 2
#define IDC_WRAPAROUND 2001
#define IDC_HEADER 2002
#define IDC_FOOTER 2003
#define PATH_MAX_ 1024
#define HF_MAX 128

static const WCHAR APP[] = L"DarkNote";
static const WCHAR REGKEY[] = L"Software\\DarkNote";
static const WCHAR *const ENC_NAMES[ENC_COUNT] = {L"ANSI", L"UTF-16 LE", L"UTF-16 BE", L"UTF-8", L"UTF-8 with BOM"};
static const WCHAR *const EOL_NAMES[4] = {L"", L"Windows (CRLF)", L"Macintosh (CR)", L"Unix (LF)"};

static HINSTANCE g_inst;
static HWND g_wnd, g_edit, g_status, g_find;
static WNDPROC g_editProc;
static HFONT g_font;
static LOGFONTW g_lf;
static int g_fontPt10 = 110;
static BOOL g_wrap, g_statusOn = TRUE, g_wrapAround, g_titleMod = -1;
static int g_zoom = 100, g_enc = ENC_UTF8, g_eol = EOL_CRLF;
static WCHAR g_path[PATH_MAX_];
static UINT g_findMsg;
static FINDREPLACEW g_fr;
static WCHAR g_findText[256], g_replText[256];
static RECT g_margins = {750, 1000, 750, 1000};
static WCHAR g_header[HF_MAX] = L"&f", g_footer[HF_MAX] = L"Page &p";
static WCHAR g_hfTmp[2][HF_MAX];
static HGLOBAL g_devMode, g_devNames;
static int g_sLine = -1, g_sCol = -1, g_sZoom = -1;
static HBRUSH g_brBg, g_brPanel, g_brHot, g_brDlg, g_brInput, g_brSep;
static HFONT g_menuFont, g_statusFont;
static WNDPROC g_statusProc;
static BOOL (WINAPI *g_allowDarkWnd)(HWND, BOOL);

static void *Alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), 0, n); }
static void Free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
static LRESULT Ed(UINT m, WPARAM w, LPARAM l) { return SendMessageW(g_edit, m, w, l); }

typedef struct {
    const WCHAR *title, *text;
    UINT icon;
    int n;
    const WCHAR *const *labels;
    const int *ids;
} MsgSpec;

static int DarkMsgBox(HWND owner, const MsgSpec *spec);

static int Msg(HWND owner, UINT flags, const WCHAR *fmt, ...)
{
    WCHAR buf[1100];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    static const WCHAR *const okL[] = {L"OK"}, *const okCancelL[] = {L"OK", L"Cancel"},
                              *const yncL[] = {L"&Yes", L"&No", L"Cancel"};
    static const int okI[] = {IDOK}, okCancelI[] = {IDOK, IDCANCEL}, yncI[] = {IDYES, IDNO, IDCANCEL};
    MsgSpec spec = {APP, buf, flags & MB_ICONMASK, 1, okL, okI};
    if ((flags & MB_TYPEMASK) == MB_OKCANCEL) {
        spec.n = 2;
        spec.labels = okCancelL;
        spec.ids = okCancelI;
    } else if ((flags & MB_TYPEMASK) == MB_YESNOCANCEL) {
        spec.n = 3;
        spec.labels = yncL;
        spec.ids = yncI;
    }
    return DarkMsgBox(owner ? owner : g_wnd, &spec);
}

static void ErrorBox(const WCHAR *path, DWORD err)
{
    WCHAR sys[512];
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, sys, 512, NULL))
        wsprintfW(sys, L"Error %lu", err);
    Msg(NULL, MB_ICONERROR, L"%s\n\n%s", path, sys);
}


/* uxtheme ordinals 133/135/136 (AllowDarkModeForWindow, SetPreferredAppMode, FlushMenuThemes) exist
   from Windows 10 1809 on; they make popup menus, scrollbars and buttons draw dark. */
static void InitDarkMode(void)
{
    DWORD maj, min, build = 0;
    void (WINAPI *ver)(DWORD *, DWORD *, DWORD *) =
        (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers");
    if (ver) ver(&maj, &min, &build);
    g_brBg = CreateSolidBrush(COL_BG);
    g_brPanel = CreateSolidBrush(COL_PANEL);
    g_brHot = CreateSolidBrush(COL_HOT);
    g_brDlg = CreateSolidBrush(COL_DLG);
    g_brInput = CreateSolidBrush(COL_INPUT);
    g_brSep = CreateSolidBrush(COL_SEP);
    if ((build & 0x0FFFFFFF) < 17763) return;
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    int (WINAPI *setMode)(int) = (void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    void (WINAPI *flush)(void) = (void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    g_allowDarkWnd = (void *)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    if (setMode) setMode(2);
    if (flush) flush();
}

static void DarkWindow(HWND h, const WCHAR *theme)
{
    if (g_allowDarkWnd) g_allowDarkWnd(h, TRUE);
    if (theme) SetWindowTheme(h, theme, NULL);
}

static void DarkTitle(HWND h)
{
    BOOL on = TRUE;
    if (FAILED(DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof on)))
        DwmSetWindowAttribute(h, 19, &on, sizeof on);
}

static void PaintButton(HWND h, HDC dc)
{
    RECT rc, tr;
    WCHAR text[128];
    TEXTMETRICW tm;
    int type = GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK;
    int dpi = GetDpiForWindow(h);
    LRESULT ui = SendMessageW(h, WM_QUERYUISTATE, 0, 0);
    DWORD dt = DT_SINGLELINE | ((ui & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0);
    GetClientRect(h, &rc);
    GetWindowTextW(h, text, 128);
    HGDIOBJ oldFont = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, IsWindowEnabled(h) ? COL_TEXT : COL_DISABLED);
    GetTextMetricsW(dc, &tm);

    if (type == BS_GROUPBOX) {
        /* No background fill: the group box overlaps the controls it contains. */
        int pad = MulDiv(6, dpi, 96);
        RECT frame = {rc.left, rc.top + tm.tmHeight / 2, rc.right, rc.bottom};
        FrameRect(dc, &frame, g_brSep);
        SetRect(&tr, rc.left + pad, rc.top, rc.right - pad, rc.top + tm.tmHeight);
        DrawTextW(dc, text, -1, &tr, dt | DT_CALCRECT);
        RECT gap = {tr.left - 2, tr.top, tr.right + 2, tr.bottom};
        FillRect(dc, &gap, g_brDlg);
        DrawTextW(dc, text, -1, &tr, dt);
        SelectObject(dc, oldFont);
        return;
    }

    FillRect(dc, &rc, g_brDlg);
    UINT st = (UINT)SendMessageW(h, BM_GETSTATE, 0, 0);
    int box = MulDiv(13, dpi, 96), top = (rc.top + rc.bottom - box) / 2;
    RECT b = {rc.left, top, rc.left + box, top + box};
    HPEN border = CreatePen(PS_SOLID, 1, IsWindowEnabled(h) ? RGB(150, 150, 150) : COL_SEP);
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, (st & BST_PUSHED) ? g_brHot : g_brInput);
    BOOL radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
    if (radio) {
        Ellipse(dc, b.left, b.top, b.right, b.bottom);
        if (st & BST_CHECKED) {
            int in = box * 3 / 10;
            SelectObject(dc, GetStockObject(WHITE_BRUSH));
            SelectObject(dc, GetStockObject(WHITE_PEN));
            Ellipse(dc, b.left + in, b.top + in, b.right - in, b.bottom - in);
        }
    } else {
        Rectangle(dc, b.left, b.top, b.right, b.bottom);
        if (st & BST_CHECKED) {
            HPEN tick = CreatePen(PS_SOLID, MulDiv(2, dpi, 96), RGB(255, 255, 255));
            POINT pts[3] = {{b.left + box * 22 / 100, b.top + box * 50 / 100},
                            {b.left + box * 42 / 100, b.top + box * 70 / 100},
                            {b.left + box * 78 / 100, b.top + box * 28 / 100}};
            SelectObject(dc, tick);
            Polyline(dc, pts, 3);
            SelectObject(dc, border);
            DeleteObject(tick);
        } else if (st & BST_INDETERMINATE) {
            RECT in = {b.left + box / 4, b.top + box / 4, b.right - box / 4, b.bottom - box / 4};
            FillRect(dc, &in, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);

    SetRect(&tr, b.right + MulDiv(5, dpi, 96), rc.top, rc.right, rc.bottom);
    DrawTextW(dc, text, -1, &tr, dt | DT_VCENTER);
    if (GetFocus() == h && !(ui & UISF_HIDEFOCUS)) {
        RECT fr = tr;
        DrawTextW(dc, text, -1, &fr, dt | DT_CALCRECT);
        OffsetRect(&fr, 0, (rc.bottom - rc.top - (fr.bottom - fr.top)) / 2);
        InflateRect(&fr, 1, 1);
        DrawFocusRect(dc, &fr);
    }
    SelectObject(dc, oldFont);
}

/* Unthemed buttons paint some state changes directly instead of through WM_PAINT, so repaint after those. */
static LRESULT CALLBACK DarkButtonProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    (void)data;
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintButton(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        PaintButton(h, (HDC)w);
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, DarkButtonProc, id);
        break;
    }
    LRESULT r = DefSubclassProc(h, m, w, l);
    switch (m) {
    case WM_MOUSEMOVE:
        if (GetCapture() != h) break;
        /* fall through */
    case BM_SETCHECK: case BM_SETSTATE: case BM_CLICK: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_KEYDOWN: case WM_KEYUP: case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: case WM_SETTEXT:
    case WM_UPDATEUISTATE: case WM_CAPTURECHANGED:
        RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    return r;
}

static BOOL CALLBACK DarkChild(HWND c, LPARAM l)
{
    WCHAR cls[32];
    (void)l;
    GetClassNameW(c, cls, 32);
    if (!lstrcmpiW(cls, L"Button")) {
        LONG t = GetWindowLongW(c, GWL_STYLE) & BS_TYPEMASK;
        if (t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON) {
            DarkWindow(c, L"DarkMode_Explorer");
        } else if (t != BS_OWNERDRAW) {
            SetWindowTheme(c, L"", L"");
            SetWindowSubclass(c, DarkButtonProc, 1, 0);
        }
    } else if (!lstrcmpiW(cls, L"Edit") || !lstrcmpiW(cls, L"ComboBox")) {
        DarkWindow(c, L"DarkMode_CFD");
    } else if (!lstrcmpiW(cls, L"ListBox") || !lstrcmpiW(cls, L"ComboLBox") || !lstrcmpiW(cls, L"ScrollBar")) {
        DarkWindow(c, L"DarkMode_Explorer");
    }
    SendMessageW(c, WM_THEMECHANGED, 0, 0);
    return TRUE;
}

static void DarkDialog(HWND d)
{
    DarkWindow(d, NULL);
    DarkTitle(d);
    EnumChildWindows(d, DarkChild, 0);
}

static INT_PTR DarkCtlColor(UINT m, WPARAM w)
{
    HDC dc = (HDC)w;
    switch (m) {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(dc, COL_TEXT);
        SetBkColor(dc, COL_INPUT);
        return (INT_PTR)g_brInput;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor(dc, COL_TEXT);
        SetBkColor(dc, COL_DLG);
        return (INT_PTR)g_brDlg;
    }
    return 0;
}

static UINT_PTR CALLBACK DarkHook(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)l;
    if (m == WM_INITDIALOG) {
        DarkDialog(h);
        return TRUE;
    }
    return DarkCtlColor(m, w);
}

static void UpdateUiFonts(void)
{
    NONCLIENTMETRICSW ncm = {sizeof ncm};
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, GetDpiForWindow(g_wnd))) return;
    if (g_menuFont) DeleteObject(g_menuFont);
    if (g_statusFont) DeleteObject(g_statusFont);
    g_menuFont = CreateFontIndirectW(&ncm.lfMenuFont);
    g_statusFont = CreateFontIndirectW(&ncm.lfStatusFont);
    if (g_status) SendMessageW(g_status, WM_SETFONT, (WPARAM)g_statusFont, TRUE);
}

static void DrawMenuBar_(UAHMENU *um)
{
    MENUBARINFO mbi = {sizeof mbi};
    RECT wr;
    GetMenuBarInfo(g_wnd, OBJID_MENU, 0, &mbi);
    GetWindowRect(g_wnd, &wr);
    OffsetRect(&mbi.rcBar, -wr.left, -wr.top);
    FillRect(um->hdc, &mbi.rcBar, g_brPanel);
}

static void DrawMenuBarItem(UAHDRAWMENUITEM *mi)
{
    WCHAR buf[64];
    MENUITEMINFOW mii = {sizeof mii};
    UINT st = mi->dis.itemState;
    DWORD dt = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = buf;
    mii.cch = 63;
    if (!GetMenuItemInfoW(mi->um.hmenu, mi->umi.iPosition, TRUE, &mii)) buf[0] = 0;
    if (st & ODS_NOACCEL) dt |= DT_HIDEPREFIX;
    FillRect(mi->um.hdc, &mi->dis.rcItem, (st & (ODS_HOTLIGHT | ODS_SELECTED)) ? g_brHot : g_brPanel);
    HGDIOBJ old = SelectObject(mi->um.hdc, g_menuFont);
    SetBkMode(mi->um.hdc, TRANSPARENT);
    SetTextColor(mi->um.hdc, (st & (ODS_GRAYED | ODS_DISABLED | ODS_INACTIVE)) ? COL_DISABLED : COL_TEXT);
    DrawTextW(mi->um.hdc, buf, -1, &mi->dis.rcItem, dt);
    SelectObject(mi->um.hdc, old);
}

/* Windows draws a 1px light line between the menu bar and the client area; paint over it. */
static void CoverMenuLine(void)
{
    RECT cr, wr;
    GetClientRect(g_wnd, &cr);
    MapWindowPoints(g_wnd, NULL, (POINT *)&cr, 2);
    GetWindowRect(g_wnd, &wr);
    OffsetRect(&cr, -wr.left, -wr.top);
    cr.bottom = cr.top;
    cr.top--;
    HDC dc = GetWindowDC(g_wnd);
    FillRect(dc, &cr, g_brPanel);
    ReleaseDC(g_wnd, dc);
}

static LRESULT CALLBACK StatusProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        RECT rc, r;
        WCHAR buf[64];
        HDC dc = BeginPaint(h, &ps);
        GetClientRect(h, &rc);
        FillRect(dc, &rc, g_brPanel);
        HGDIOBJ old = SelectObject(dc, g_statusFont ? (HGDIOBJ)g_statusFont : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, COL_TEXT);
        int n = (int)SendMessageW(h, SB_GETPARTS, 0, 0);
        for (int i = 0; i < n; i++) {
            SendMessageW(h, SB_GETRECT, i, (LPARAM)&r);
            if (i > 0) {
                RECT sep = {r.left - 1, rc.top + 3, r.left, rc.bottom - 3};
                FillRect(dc, &sep, g_brSep);
            }
            buf[0] = 0;
            if (LOWORD(SendMessageW(h, SB_GETTEXTLENGTHW, i, 0)) < 64) SendMessageW(h, SB_GETTEXTW, i, (LPARAM)buf);
            r.left += 2;
            DrawTextW(dc, buf, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        SelectObject(dc, old);
        EndPaint(h, &ps);
        return 0;
    }
    return CallWindowProcW(g_statusProc, h, m, w, l);
}

static INT_PTR CALLBACK MsgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    const MsgSpec *spec = (const MsgSpec *)GetWindowLongPtrW(h, DWLP_USER);
    switch (m) {
    case WM_INITDIALOG: {
        spec = (const MsgSpec *)l;
        SetWindowLongPtrW(h, DWLP_USER, l);
        int dpi = GetDpiForWindow(h);
#define S(x) MulDiv(x, dpi, 96)
        int margin = S(24), iconSz = 0, gap = S(12), bh = S(26), bandH = bh + S(24);
        HWND text = GetDlgItem(h, IDC_MSGTEXT), icon = GetDlgItem(h, IDC_MSGICON);
        HDC dc = GetDC(h);
        HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        RECT tr = {0, 0, S(420), 0};
        DrawTextW(dc, spec->text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);

        LPCWSTR sysIcon = NULL;
        switch (spec->icon) {
        case MB_ICONERROR: sysIcon = IDI_ERROR; break;
        case MB_ICONWARNING: sysIcon = IDI_WARNING; break;
        case MB_ICONINFORMATION: sysIcon = IDI_INFORMATION; break;
        case MB_ICONQUESTION: sysIcon = IDI_QUESTION; break;
        }
        HICON hi = NULL;
        if (sysIcon && SUCCEEDED(LoadIconWithScaleDown(NULL, sysIcon, S(32), S(32), &hi))) {
            iconSz = S(32);
            SendMessageW(icon, STM_SETICON, (WPARAM)hi, 0);
        } else {
            ShowWindow(icon, SW_HIDE);
        }

        int bw[3] = {0, 0, 0}, buttonsW = 0;
        for (int i = 0; i < 3; i++) {
            HWND b = GetDlgItem(h, IDC_B1 + i);
            if (i >= spec->n) {
                ShowWindow(b, SW_HIDE);
                continue;
            }
            RECT br = {0, 0, 0, 0};
            DrawTextW(dc, spec->labels[i], -1, &br, DT_CALCRECT | DT_SINGLELINE);
            bw[i] = br.right + S(24) > S(88) ? br.right + S(24) : S(88);
            buttonsW += bw[i] + (i ? S(10) : 0);
            SetWindowTextW(b, spec->labels[i]);
        }
        SelectObject(dc, old);
        ReleaseDC(h, dc);

        int textX = margin + (iconSz ? iconSz + gap : 0);
        int contentW = textX + tr.right + margin, minW = buttonsW + 2 * S(12);
        int cw = contentW > minW ? contentW : minW;
        if (cw < S(280)) cw = S(280);
        int bodyH = (tr.bottom > iconSz ? tr.bottom : iconSz) + 2 * margin;
        int ch = bodyH + bandH;

        MoveWindow(icon, margin, margin, iconSz, iconSz, FALSE);
        MoveWindow(text, textX, margin + (iconSz > tr.bottom ? (iconSz - tr.bottom) / 2 : 0), tr.right, tr.bottom, FALSE);
        SetWindowTextW(text, spec->text);
        int x = cw - S(12) - buttonsW;
        for (int i = 0; i < spec->n; i++) {
            MoveWindow(GetDlgItem(h, IDC_B1 + i), x, bodyH + (bandH - bh) / 2, bw[i], bh, FALSE);
            x += bw[i] + S(10);
        }
#undef S

        RECT wr = {0, 0, cw, ch}, ref;
        AdjustWindowRectExForDpi(&wr, GetWindowLongW(h, GWL_STYLE), FALSE, GetWindowLongW(h, GWL_EXSTYLE), dpi);
        HWND owner = GetWindow(h, GW_OWNER);
        if (owner && IsWindowVisible(owner) && !IsIconic(owner)) {
            GetWindowRect(owner, &ref);
        } else {
            MONITORINFO mi = {sizeof mi};
            GetMonitorInfoW(MonitorFromWindow(owner ? owner : h, MONITOR_DEFAULTTONEAREST), &mi);
            ref = mi.rcWork;
        }
        int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
        SetWindowPos(h, NULL, (ref.left + ref.right - ww) / 2, (ref.top + ref.bottom - wh) / 2, ww, wh, SWP_NOZORDER);
        SetWindowTextW(h, spec->title);
        SendMessageW(h, DM_SETDEFID, IDC_B1, 0);
        DarkDialog(h);
        SetFocus(GetDlgItem(h, IDC_B1));
        return FALSE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT rc, b;
        HDC dc = BeginPaint(h, &ps);
        GetClientRect(h, &rc);
        GetWindowRect(GetDlgItem(h, IDC_B1), &b);
        MapWindowPoints(NULL, h, (POINT *)&b, 2);
        rc.top = b.top - (rc.bottom - b.bottom);
        FillRect(dc, &rc, g_brPanel);
        EndPaint(h, &ps);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id >= IDC_B1 && id < IDC_B1 + spec->n) {
            EndDialog(h, spec->ids[id - IDC_B1]);
            return TRUE;
        }
        if (id == IDCANCEL) {
            /* Esc and the close box mean Cancel, or the only button when there is no Cancel. */
            for (int i = 0; i < spec->n; i++)
                if (spec->ids[i] == IDCANCEL) {
                    EndDialog(h, IDCANCEL);
                    return TRUE;
                }
            if (spec->n == 1) EndDialog(h, spec->ids[0]);
            return TRUE;
        }
        break;
    }
    case WM_DESTROY: {
        HICON hi = (HICON)SendDlgItemMessageW(h, IDC_MSGICON, STM_GETICON, 0, 0);
        if (hi) DestroyIcon(hi);
        break;
    }
    }
    return DarkCtlColor(m, w);
}

static int DarkMsgBox(HWND owner, const MsgSpec *spec)
{
    if (spec->icon) MessageBeep(spec->icon);
    return (int)DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_MSG), owner, MsgProc, (LPARAM)spec);
}

static WCHAR *GetEditText(int *len)
{
    int n = GetWindowTextLengthW(g_edit);
    WCHAR *s = Alloc((n + 1) * sizeof(WCHAR));
    *len = GetWindowTextW(g_edit, s, n + 1);
    return s;
}

static WCHAR *GetSelText(int *len)
{
    DWORD s, e;
    int n;
    Ed(EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    WCHAR *all = GetEditText(&n);
    if (e > (DWORD)n) e = n;
    if (s > e) s = e;
    *len = e - s;
    MoveMemory(all, all + s, *len * sizeof(WCHAR));
    all[*len] = 0;
    return all;
}

static const WCHAR *DisplayName(void)
{
    const WCHAR *p = g_path, *s;
    if (!g_path[0]) return L"Untitled";
    for (s = g_path; *s; s++)
        if (*s == '\\' || *s == '/') p = s + 1;
    return p;
}

static void UpdateTitle(void)
{
    WCHAR t[PATH_MAX_ + 32];
    BOOL mod = (BOOL)Ed(EM_GETMODIFY, 0, 0);
    if (mod == g_titleMod) return;
    g_titleMod = mod;
    wsprintfW(t, L"%s%s - %s", mod ? L"*" : L"", DisplayName(), APP);
    SetWindowTextW(g_wnd, t);
}

static void ForceTitle(void)
{
    g_titleMod = -1;
    UpdateTitle();
}

static void UpdateStatus(void)
{
    WCHAR buf[64];
    DWORD num = 0, den = 0;
    if (!g_status) return;
    DWORD caret = (DWORD)Ed(EM_GETCARETINDEX_, 0, 0);
    int line = (int)Ed(EM_FILELINEFROMCHAR_, caret, 0);
    int col = (int)(caret - (DWORD)Ed(EM_FILELINEINDEX_, line, 0)) + 1;
    Ed(EM_GETZOOM_, (WPARAM)&num, (LPARAM)&den);
    g_zoom = den ? MulDiv(num, 100, den) : 100;
    if (!g_statusOn) return;
    if (line != g_sLine || col != g_sCol) {
        g_sLine = line;
        g_sCol = col;
        wsprintfW(buf, L"  Ln %d, Col %d", line + 1, col);
        SendMessageW(g_status, SB_SETTEXTW, 1, (LPARAM)buf);
    }
    if (g_zoom != g_sZoom) {
        g_sZoom = g_zoom;
        wsprintfW(buf, L"  %d%%", g_zoom);
        SendMessageW(g_status, SB_SETTEXTW, 2, (LPARAM)buf);
    }
}

static void UpdateStatusAll(void)
{
    WCHAR buf[64];
    if (!g_status) return;
    g_sLine = g_sCol = g_sZoom = -1;
    wsprintfW(buf, L"  %s", EOL_NAMES[g_eol]);
    SendMessageW(g_status, SB_SETTEXTW, 3, (LPARAM)buf);
    wsprintfW(buf, L"  %s", ENC_NAMES[g_enc]);
    SendMessageW(g_status, SB_SETTEXTW, 4, (LPARAM)buf);
    UpdateStatus();
}

static void Layout(void)
{
    RECT rc, sr;
    int sh = 0;
    GetClientRect(g_wnd, &rc);
    if (g_statusOn) {
        int dpi = GetDpiForWindow(g_wnd);
        int w = rc.right, parts[5];
        SendMessageW(g_status, WM_SIZE, 0, 0);
        GetWindowRect(g_status, &sr);
        sh = sr.bottom - sr.top;
        parts[0] = w - MulDiv(150 + 60 + 130 + 130, dpi, 96);
        if (parts[0] < 0) parts[0] = 0;
        parts[1] = parts[0] + MulDiv(150, dpi, 96);
        parts[2] = parts[1] + MulDiv(60, dpi, 96);
        parts[3] = parts[2] + MulDiv(130, dpi, 96);
        parts[4] = -1;
        SendMessageW(g_status, SB_SETPARTS, 5, (LPARAM)parts);
    }
    MoveWindow(g_edit, 0, 0, rc.right, rc.bottom - sh, TRUE);
}

static void ApplyFont(void)
{
    LOGFONTW lf = g_lf;
    HFONT old = g_font;
    lf.lfHeight = -MulDiv(g_fontPt10, GetDpiForWindow(g_wnd), 720);
    lf.lfWidth = 0;
    g_font = CreateFontIndirectW(&lf);
    Ed(WM_SETFONT, (WPARAM)g_font, TRUE);
    if (old) DeleteObject(old);
}

static void SetZoom(int z)
{
    if (z < 10) z = 10;
    if (z > 500) z = 500;
    Ed(EM_SETZOOM_, z, 100);
    UpdateStatus();
}

/* Ctrl+Backspace: the stock edit control inserts a box character instead of deleting a word. */
static void DeletePrevWord(void)
{
    DWORD s, e;
    Ed(EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (s != e) {
        Ed(EM_REPLACESEL, TRUE, (LPARAM)L"");
        return;
    }
    int line = (int)Ed(EM_LINEFROMCHAR, s, 0);
    DWORD start = (DWORD)Ed(EM_LINEINDEX, line, 0);
    int n = (int)(s - start);
    if (n <= 0) {
        if (s > 0) SendMessageW(g_edit, WM_CHAR, VK_BACK, 0);
        return;
    }
    WCHAR *buf = Alloc((n + 2) * sizeof(WCHAR));
    *(WORD *)buf = (WORD)(n + 1);
    int got = (int)Ed(EM_GETLINE, line, (LPARAM)buf);
    if (got > n) got = n;
    int i = got;
    while (i > 0 && (buf[i - 1] == ' ' || buf[i - 1] == '\t')) i--;
    if (i > 0 && !IsCharAlphaNumericW(buf[i - 1]) && buf[i - 1] != '_') {
        i--;
    } else {
        while (i > 0 && (IsCharAlphaNumericW(buf[i - 1]) || buf[i - 1] == '_')) i--;
    }
    Free(buf);
    Ed(EM_SETSEL, start + i, s);
    Ed(EM_REPLACESEL, TRUE, (LPARAM)L"");
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CHAR && w == 0x7F) {
        DeletePrevWord();
        UpdateStatus();
        return 0;
    }
    LRESULT r = CallWindowProcW(g_editProc, h, m, w, l);
    switch (m) {
    case WM_MOUSEMOVE:
        if (!(w & MK_LBUTTON)) break;
        /* fall through */
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_MOUSEWHEEL: case WM_SETTEXT: case WM_UNDO: case WM_CUT: case WM_PASTE: case WM_CLEAR:
    case EM_SETSEL: case EM_REPLACESEL: case EM_UNDO:
        UpdateStatus();
    }
    return r;
}

static void CreateEditor(void)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL;
    if (!g_wrap) style |= WS_HSCROLL | ES_AUTOHSCROLL;
    g_edit = CreateWindowExW(0, L"EDIT", NULL, style, 0, 0, 0, 0, g_wnd, (HMENU)ID_EDIT, g_inst, NULL);
    DarkWindow(g_edit, L"DarkMode_Explorer");
    Ed(EM_SETLIMITTEXT, 0, 0);
    DWORD ex = ES_EX_ALLOWEOL_ALL_ | ES_EX_CONVERT_EOL_ON_PASTE_ | ES_EX_ZOOMABLE_;
    Ed(EM_SETEXTENDEDSTYLE, ex, ex);
    Ed(EM_SETENDOFLINE_, g_eol, 0);
    if (g_font) Ed(WM_SETFONT, (WPARAM)g_font, FALSE);
    if (g_zoom != 100) Ed(EM_SETZOOM_, g_zoom, 100);
    g_editProc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
}

static void SetWrap(BOOL on)
{
    int len;
    DWORD s, e;
    WCHAR *t = GetEditText(&len);
    BOOL mod = (BOOL)Ed(EM_GETMODIFY, 0, 0);
    Ed(EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    HWND old = g_edit;
    g_wrap = on;
    CreateEditor();
    SetWindowTextW(g_edit, t);
    Free(t);
    Ed(EM_SETMODIFY, mod, 0);
    Ed(EM_SETSEL, s, e);
    DestroyWindow(old);
    Layout();
    Ed(EM_SCROLLCARET, 0, 0);
    SetFocus(g_edit);
    UpdateStatusAll();
}

static void TimeDate(WCHAR *buf, int cch)
{
    int n = GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, NULL, NULL, buf, cch);
    if (n <= 0) n = 1;
    buf[n - 1] = ' ';
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, buf + n, cch - n);
}

static const WCHAR *EolStr(void)
{
    return g_eol == EOL_LF ? L"\n" : g_eol == EOL_CR ? L"\r" : L"\r\n";
}


static WCHAR *Decode(BYTE *b, DWORD n, int force, int *enc, int *outLen)
{
    int e = force, skip = 0, len;
    WCHAR *s;
    BOOL bomLE = n >= 2 && b[0] == 0xFF && b[1] == 0xFE;
    BOOL bomBE = n >= 2 && b[0] == 0xFE && b[1] == 0xFF;
    BOOL bom8 = n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF;
    if (e == ENC_AUTO) {
        if (bomLE) e = ENC_UTF16LE;
        else if (bomBE) e = ENC_UTF16BE;
        else if (bom8) e = ENC_UTF8BOM;
        else if (n == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (char *)b, n, NULL, 0) > 0) e = ENC_UTF8;
        else e = ENC_ANSI;
    }
    if ((e == ENC_UTF16LE && bomLE) || (e == ENC_UTF16BE && bomBE)) skip = 2;
    if ((e == ENC_UTF8 || e == ENC_UTF8BOM) && bom8) skip = 3;
    b += skip;
    n -= skip;
    if (e == ENC_UTF16LE || e == ENC_UTF16BE) {
        len = n / 2;
        s = Alloc((len + 1) * sizeof(WCHAR));
        CopyMemory(s, b, len * sizeof(WCHAR));
        if (e == ENC_UTF16BE)
            for (int i = 0; i < len; i++) s[i] = (WCHAR)((s[i] >> 8) | (s[i] << 8));
    } else {
        UINT cp = e == ENC_ANSI ? CP_ACP : CP_UTF8;
        len = n ? MultiByteToWideChar(cp, 0, (char *)b, n, NULL, 0) : 0;
        s = Alloc((len + 1) * sizeof(WCHAR));
        if (len) MultiByteToWideChar(cp, 0, (char *)b, n, s, len);
    }
    s[len] = 0;
    for (int i = 0; i < len; i++)
        if (!s[i]) s[i] = ' ';
    *enc = e;
    *outLen = len;
    return s;
}

static int DetectEol(const WCHAR *s, int len)
{
    for (int i = 0; i < len; i++) {
        if (s[i] == '\r') return i + 1 < len && s[i + 1] == '\n' ? EOL_CRLF : EOL_CR;
        if (s[i] == '\n') return EOL_LF;
    }
    return EOL_CRLF;
}

static BOOL LoadFile(const WCHAR *path, int force)
{
    LARGE_INTEGER size;
    DWORD got;
    int enc, len;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        ErrorBox(path, GetLastError());
        return FALSE;
    }
    GetFileSizeEx(h, &size);
    if (size.QuadPart > 0x40000000) {
        CloseHandle(h);
        Msg(NULL, MB_ICONERROR, L"%s\n\nThe file is too large to open.", path);
        return FALSE;
    }
    BYTE *b = Alloc(size.LowPart + 1);
    if (!b || !ReadFile(h, b, size.LowPart, &got, NULL)) {
        ErrorBox(path, GetLastError());
        CloseHandle(h);
        Free(b);
        return FALSE;
    }
    CloseHandle(h);
    WCHAR *s = Decode(b, got, force, &enc, &len);
    Free(b);

    g_enc = enc;
    g_eol = DetectEol(s, len);
    Ed(EM_SETENDOFLINE_, g_eol, 0);
    SetWindowTextW(g_edit, s);
    Ed(EM_SETMODIFY, FALSE, 0);
    Ed(EM_SETSEL, 0, 0);
    Ed(EM_SCROLLCARET, 0, 0);
    if (g_path != path) lstrcpynW(g_path, path, PATH_MAX_);
    if (len >= 4 && s[0] == '.' && s[1] == 'L' && s[2] == 'O' && s[3] == 'G') {
        WCHAR stamp[128], ins[140];
        TimeDate(stamp, 128);
        wsprintfW(ins, L"%s%s%s", EolStr(), stamp, EolStr());
        Ed(EM_SETSEL, len, len);
        Ed(EM_REPLACESEL, TRUE, (LPARAM)ins);
        Ed(EM_SCROLLCARET, 0, 0);
    }
    Free(s);
    ForceTitle();
    UpdateStatusAll();
    return TRUE;
}

static BOOL SaveTo(const WCHAR *path, int enc)
{
    int len;
    DWORD outLen, written;
    BYTE *out;
    WCHAR *t = GetEditText(&len);
    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
        outLen = 2 + len * 2;
        out = Alloc(outLen);
        WCHAR *o = (WCHAR *)out;
        o[0] = 0xFEFF;
        for (int i = 0; i < len; i++) o[i + 1] = t[i];
        if (enc == ENC_UTF16BE)
            for (int i = 0; i <= len; i++) o[i] = (WCHAR)((o[i] >> 8) | (o[i] << 8));
    } else {
        BOOL lossy = FALSE;
        UINT cp = enc == ENC_ANSI ? CP_ACP : CP_UTF8;
        DWORD flags = enc == ENC_ANSI ? WC_NO_BEST_FIT_CHARS : 0;
        BOOL *lp = enc == ENC_ANSI ? &lossy : NULL;
        int bom = enc == ENC_UTF8BOM ? 3 : 0;
        int need = len ? WideCharToMultiByte(cp, flags, t, len, NULL, 0, NULL, lp) : 0;
        outLen = bom + need;
        out = Alloc(outLen + 1);
        if (bom) CopyMemory(out, "\xEF\xBB\xBF", 3);
        if (need) WideCharToMultiByte(cp, flags, t, len, (char *)out + bom, need, NULL, lp);
        if (lossy && Msg(NULL, MB_OKCANCEL | MB_ICONWARNING,
                L"This file contains characters that will be lost if you save it as ANSI. "
                L"To keep them, click Cancel and choose a Unicode encoding in Save As.\n\nContinue?") != IDOK) {
            Free(out);
            Free(t);
            return FALSE;
        }
    }
    Free(t);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ok = h != INVALID_HANDLE_VALUE && WriteFile(h, out, outLen, &written, NULL) && SetEndOfFile(h);
    DWORD err = GetLastError();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    Free(out);
    if (!ok) {
        ErrorBox(path, err);
        return FALSE;
    }
    if (g_path != path) lstrcpynW(g_path, path, PATH_MAX_);
    g_enc = enc;
    Ed(EM_SETMODIFY, FALSE, 0);
    ForceTitle();
    UpdateStatusAll();
    return TRUE;
}

static BOOL FileDialog(BOOL save, WCHAR *path, int *enc)
{
    static BOOL coInit;
    static const COMDLG_FILTERSPEC types[] = {{L"Text Documents (*.txt)", L"*.txt"}, {L"All Files (*.*)", L"*.*"}};
    IFileDialog *fd;
    IFileDialogCustomize *fc = NULL;
    BOOL ok = FALSE;
    if (!coInit) {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        coInit = TRUE;
    }
    if (FAILED(CoCreateInstance(save ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileDialog, (void **)&fd)))
        return FALSE;
    IFileDialog_SetFileTypes(fd, 2, types);
    IFileDialog_SetDefaultExtension(fd, L"txt");
    if (g_path[0]) {
        WCHAR dir[PATH_MAX_];
        IShellItem *si;
        lstrcpynW(dir, g_path, PATH_MAX_);
        WCHAR *cut = dir + (DisplayName() - g_path);
        if (cut > dir) cut[-1] = 0;
        if (SUCCEEDED(SHCreateItemFromParsingName(dir, NULL, &IID_IShellItem, (void **)&si))) {
            IFileDialog_SetFolder(fd, si);
            IShellItem_Release(si);
        }
        if (save) IFileDialog_SetFileName(fd, DisplayName());
    } else if (save) {
        IFileDialog_SetFileName(fd, L"*.txt");
    }
    if (SUCCEEDED(IFileDialog_QueryInterface(fd, &IID_IFileDialogCustomize, (void **)&fc))) {
        IFileDialogCustomize_StartVisualGroup(fc, 1, L"&Encoding:");
        IFileDialogCustomize_AddComboBox(fc, 2);
        if (!save) IFileDialogCustomize_AddControlItem(fc, 2, ENC_AUTO, L"Auto-Detect");
        for (int i = 0; i < ENC_COUNT; i++) IFileDialogCustomize_AddControlItem(fc, 2, i, ENC_NAMES[i]);
        IFileDialogCustomize_EndVisualGroup(fc);
        IFileDialogCustomize_SetSelectedControlItem(fc, 2, save ? (DWORD)*enc : ENC_AUTO);
    }
    if (SUCCEEDED(IFileDialog_Show(fd, g_wnd))) {
        IShellItem *res;
        WCHAR *p;
        if (SUCCEEDED(IFileDialog_GetResult(fd, &res))) {
            if (SUCCEEDED(IShellItem_GetDisplayName(res, SIGDN_FILESYSPATH, &p))) {
                lstrcpynW(path, p, PATH_MAX_);
                CoTaskMemFree(p);
                ok = TRUE;
            }
            IShellItem_Release(res);
        }
        DWORD sel;
        if (fc && SUCCEEDED(IFileDialogCustomize_GetSelectedControlItem(fc, 2, &sel))) *enc = (int)sel;
        else *enc = save ? *enc : ENC_AUTO;
    }
    if (fc) IFileDialogCustomize_Release(fc);
    IFileDialog_Release(fd);
    return ok;
}

static BOOL SaveAs(void)
{
    WCHAR path[PATH_MAX_];
    int enc = g_enc;
    return FileDialog(TRUE, path, &enc) && SaveTo(path, enc);
}

static BOOL Save(void)
{
    return g_path[0] ? SaveTo(g_path, g_enc) : SaveAs();
}

static BOOL CheckSave(void)
{
    static const WCHAR *const labels[] = {L"&Save", L"Do&n't Save", L"Cancel"};
    static const int ids[] = {IDYES, IDNO, IDCANCEL};
    WCHAR text[PATH_MAX_ + 64];
    if (!Ed(EM_GETMODIFY, 0, 0)) return TRUE;
    wsprintfW(text, L"Do you want to save changes to %s?", g_path[0] ? g_path : L"Untitled");
    MsgSpec spec = {APP, text, 0, 3, labels, ids};
    int btn = DarkMsgBox(g_wnd, &spec);
    if (btn == IDYES) return Save();
    return btn == IDNO;
}

static void NewFile(void)
{
    g_path[0] = 0;
    g_enc = ENC_UTF8;
    g_eol = EOL_CRLF;
    Ed(EM_SETENDOFLINE_, g_eol, 0);
    SetWindowTextW(g_edit, L"");
    Ed(EM_SETMODIFY, FALSE, 0);
    ForceTitle();
    UpdateStatusAll();
}

/* Matches Notepad: the whole remainder of the command line is one path, quoted or not. */
static void OpenFromCommandLine(void)
{
    WCHAR *p = GetCommandLineW(), full[PATH_MAX_], arg[PATH_MAX_];
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++);
        if (*p) p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return;
    lstrcpynW(arg, p, PATH_MAX_);
    int n = lstrlenW(arg);
    while (n && (arg[n - 1] == ' ' || arg[n - 1] == '\t')) arg[--n] = 0;
    if (arg[0] == '"') {
        MoveMemory(arg, arg + 1, n * sizeof(WCHAR));
        n--;
        if (n && arg[n - 1] == '"') arg[--n] = 0;
    }
    if (!GetFullPathNameW(arg, PATH_MAX_, full, NULL)) return;
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
        WCHAR *name = full, *s;
        for (s = full; *s; s++)
            if (*s == '\\') name = s + 1;
        BOOL hasExt = FALSE;
        for (s = name; *s; s++)
            if (*s == '.') hasExt = TRUE;
        if (!hasExt && lstrlenW(full) + 4 < PATH_MAX_) {
            lstrcatW(full, L".txt");
            if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) {
                LoadFile(full, ENC_AUTO);
                return;
            }
        }
        int r = Msg(NULL, MB_YESNOCANCEL | MB_ICONEXCLAMATION,
                    L"Cannot find the %s file.\n\nDo you want to create a new file?", full);
        if (r == IDCANCEL) ExitProcess(0);
        if (r == IDYES) {
            HANDLE h = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                ErrorBox(full, GetLastError());
                return;
            }
            CloseHandle(h);
            LoadFile(full, ENC_AUTO);
        }
        return;
    }
    LoadFile(full, ENC_AUTO);
}


static UINT_PTR CALLBACK FindHook(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)w; (void)l;
    if (m == WM_INITDIALOG) {
        /* Take the hidden "whole word" slot for Match case and put Wrap around where Match case was. */
        HWND c1 = GetDlgItem(h, chx1), c2 = GetDlgItem(h, chx2);
        RECT r1, r2;
        GetWindowRect(c1, &r1);
        GetWindowRect(c2, &r2);
        MapWindowPoints(NULL, h, (POINT *)&r1, 2);
        MapWindowPoints(NULL, h, (POINT *)&r2, 2);
        MoveWindow(c2, r1.left, r1.top, r2.right - r2.left, r2.bottom - r2.top, FALSE);
        HWND wrap = CreateWindowExW(0, L"BUTTON", L"W&rap around", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                    r2.left, r2.top, r2.right - r2.left, r2.bottom - r2.top, h,
                                    (HMENU)IDC_WRAPAROUND, g_inst, NULL);
        SendMessageW(wrap, WM_SETFONT, SendMessageW(h, WM_GETFONT, 0, 0), FALSE);
        SetWindowPos(wrap, c2, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        CheckDlgButton(h, IDC_WRAPAROUND, g_wrapAround ? BST_CHECKED : BST_UNCHECKED);
        DarkDialog(h);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(w) == IDC_WRAPAROUND)
        g_wrapAround = IsDlgButtonChecked(h, IDC_WRAPAROUND) == BST_CHECKED;
    return DarkCtlColor(m, w);
}

static void OpenFind(BOOL replace)
{
    int len;
    if (g_find) {
        BOOL isReplace = GetDlgItem(g_find, edt2) != NULL;
        if (isReplace == replace) {
            SetFocus(g_find);
            return;
        }
        DestroyWindow(g_find);
        g_find = NULL;
    }
    WCHAR *sel = GetSelText(&len);
    if (len > 0 && len < 256) {
        BOOL multi = FALSE;
        for (int i = 0; i < len; i++)
            if (sel[i] == '\r' || sel[i] == '\n') multi = TRUE;
        if (!multi) lstrcpynW(g_findText, sel, 256);
    }
    Free(sel);
    g_fr.lStructSize = sizeof g_fr;
    g_fr.hwndOwner = g_wnd;
    g_fr.lpstrFindWhat = g_findText;
    g_fr.wFindWhatLen = 256;
    g_fr.lpstrReplaceWith = g_replText;
    g_fr.wReplaceWithLen = 256;
    g_fr.Flags = (g_fr.Flags & (FR_MATCHCASE | FR_DOWN)) | FR_HIDEWHOLEWORD | FR_ENABLEHOOK;
    g_fr.lpfnHook = FindHook;
    g_find = replace ? ReplaceTextW(&g_fr) : FindTextW(&g_fr);
}

static BOOL DoFind(BOOL down, BOOL quiet)
{
    int len, n = lstrlenW(g_findText), idx = -1;
    DWORD s, e;
    BOOL ic = !(g_fr.Flags & FR_MATCHCASE);
    if (!n) return FALSE;
    WCHAR *t = GetEditText(&len);
    Ed(EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (down) {
        if ((int)e < len) idx = FindStringOrdinal(FIND_FROMSTART, t + e, len - e, g_findText, n, ic);
        if (idx >= 0) idx += e;
        else if (g_wrapAround) idx = FindStringOrdinal(FIND_FROMSTART, t, len, g_findText, n, ic);
    } else {
        if (s > 0) idx = FindStringOrdinal(FIND_FROMEND, t, s, g_findText, n, ic);
        if (idx < 0 && g_wrapAround) idx = FindStringOrdinal(FIND_FROMEND, t, len, g_findText, n, ic);
    }
    Free(t);
    if (idx >= 0) {
        Ed(EM_SETSEL, idx, idx + n);
        Ed(EM_SCROLLCARET, 0, 0);
        return TRUE;
    }
    if (!quiet) Msg(g_find, MB_ICONINFORMATION, L"Cannot find \"%s\"", g_findText);
    return FALSE;
}

static void DoReplace(void)
{
    int len, n = lstrlenW(g_findText);
    WCHAR *sel = GetSelText(&len);
    if (n && len == n && CompareStringOrdinal(sel, n, g_findText, n, !(g_fr.Flags & FR_MATCHCASE)) == CSTR_EQUAL)
        Ed(EM_REPLACESEL, TRUE, (LPARAM)g_replText);
    Free(sel);
    DoFind(TRUE, FALSE);
}

static void DoReplaceAll(void)
{
    int len, n = lstrlenW(g_findText), rn = lstrlenW(g_replText), count = 0, i, j;
    BOOL ic = !(g_fr.Flags & FR_MATCHCASE);
    if (!n) return;
    WCHAR *t = GetEditText(&len);
    for (i = 0; i < len;) {
        int k = FindStringOrdinal(FIND_FROMSTART, t + i, len - i, g_findText, n, ic);
        if (k < 0) break;
        count++;
        i += k + n;
    }
    if (!count) {
        Free(t);
        Msg(g_find, MB_ICONINFORMATION, L"Cannot find \"%s\"", g_findText);
        return;
    }
    WCHAR *out = Alloc(((SIZE_T)len + (SIZE_T)count * rn + 1) * sizeof(WCHAR)), *o = out;
    for (i = 0; i < len;) {
        int k = FindStringOrdinal(FIND_FROMSTART, t + i, len - i, g_findText, n, ic);
        if (k < 0) k = len - i;
        for (j = 0; j < k; j++) *o++ = t[i + j];
        i += k;
        if (i < len) {
            for (j = 0; j < rn; j++) *o++ = g_replText[j];
            i += n;
        }
    }
    *o = 0;
    Free(t);
    Ed(EM_SETSEL, 0, -1);
    Ed(EM_REPLACESEL, TRUE, (LPARAM)out);
    Ed(EM_SETSEL, 0, 0);
    Ed(EM_SCROLLCARET, 0, 0);
    Free(out);
}

static void OnFindMsg(FINDREPLACEW *fr)
{
    if (fr->Flags & FR_DIALOGTERM) {
        g_find = NULL;
        return;
    }
    if (fr->Flags & FR_FINDNEXT) DoFind(fr->Flags & FR_DOWN, FALSE);
    else if (fr->Flags & FR_REPLACE) DoReplace();
    else if (fr->Flags & FR_REPLACEALL) DoReplaceAll();
}


static INT_PTR CALLBACK GotoProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_INITDIALOG) {
        SetDlgItemInt(h, IDC_LINE, (UINT)l, FALSE);
        SendDlgItemMessageW(h, IDC_LINE, EM_SETSEL, 0, -1);
        DarkDialog(h);
        return TRUE;
    }
    if (m == WM_COMMAND) {
        if (LOWORD(w) == IDOK) {
            BOOL ok;
            UINT n = GetDlgItemInt(h, IDC_LINE, &ok, FALSE);
            UINT count = (UINT)Ed(EM_GETFILELINECOUNT_, 0, 0);
            if (!ok || n < 1 || n > count) {
                static const WCHAR *const okL[] = {L"OK"};
                static const int okI[] = {IDOK};
                MsgSpec spec = {L"DarkNote - Goto Line", L"The line number is beyond the total number of lines", 0, 1, okL, okI};
                DarkMsgBox(h, &spec);
                SetFocus(GetDlgItem(h, IDC_LINE));
                SendDlgItemMessageW(h, IDC_LINE, EM_SETSEL, 0, -1);
                return TRUE;
            }
            EndDialog(h, n);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
    }
    return DarkCtlColor(m, w);
}

static void GoTo(void)
{
    DWORD caret = (DWORD)Ed(EM_GETCARETINDEX_, 0, 0);
    int cur = (int)Ed(EM_FILELINEFROMCHAR_, caret, 0) + 1;
    INT_PTR n = DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_GOTO), g_wnd, GotoProc, cur);
    if (n > 0) {
        DWORD idx = (DWORD)Ed(EM_FILELINEINDEX_, n - 1, 0);
        Ed(EM_SETSEL, idx, idx);
        Ed(EM_SCROLLCARET, 0, 0);
    }
}


static void ChooseEditorFont(void)
{
    CHOOSEFONTW cf = {sizeof cf};
    LOGFONTW lf = g_lf;
    HDC dc = GetDC(NULL);
    lf.lfHeight = -MulDiv(g_fontPt10, GetDeviceCaps(dc, LOGPIXELSY), 720);
    ReleaseDC(NULL, dc);
    cf.hwndOwner = g_wnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS | CF_ENABLEHOOK;
    cf.lpfnHook = DarkHook;
    if (!ChooseFontW(&cf)) return;
    g_lf = lf;
    g_fontPt10 = cf.iPointSize ? cf.iPointSize : 110;
    ApplyFont();
    UpdateStatus();
}


typedef struct { int top, delta; HWND dlg; } MoveCtx;

static BOOL CALLBACK MoveDown(HWND c, LPARAM l)
{
    MoveCtx *m = (MoveCtx *)l;
    RECT r;
    if (GetParent(c) != m->dlg) return TRUE;
    GetWindowRect(c, &r);
    MapWindowPoints(NULL, m->dlg, (POINT *)&r, 2);
    if (r.top >= m->top)
        SetWindowPos(c, NULL, r.left, r.top + m->delta, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}

static UINT_PTR CALLBACK PageHook(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)l;
    if (m == WM_INITDIALOG) {
        RECT ok, cr, wr, du = {7, 18, 36, 12}, off = {0, 2, 0, 0};
        HFONT font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        MapDialogRect(h, &du);
        MapDialogRect(h, &off);
        GetWindowRect(GetDlgItem(h, IDOK), &ok);
        MapWindowPoints(NULL, h, (POINT *)&ok, 2);
        MoveCtx ctx = {ok.top - 2, du.top * 2 + du.top / 3, h};
        EnumChildWindows(h, MoveDown, (LPARAM)&ctx);
        GetWindowRect(h, &wr);
        SetWindowPos(h, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top + ctx.delta, SWP_NOMOVE | SWP_NOZORDER);
        GetClientRect(h, &cr);
        for (int i = 0; i < 2; i++) {
            int y = ok.top + i * du.top;
            HWND lab = CreateWindowExW(0, L"STATIC", i ? L"&Footer:" : L"&Header:", WS_CHILD | WS_VISIBLE,
                                       du.left, y + off.top, du.right, du.bottom, h, NULL, g_inst, NULL);
            HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", i ? g_footer : g_header,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, du.left + du.right, y,
                                      cr.right - du.left * 2 - du.right, du.bottom + off.top, h,
                                      (HMENU)(INT_PTR)(i ? IDC_FOOTER : IDC_HEADER), g_inst, NULL);
            SendMessageW(lab, WM_SETFONT, (WPARAM)font, FALSE);
            SendMessageW(ed, WM_SETFONT, (WPARAM)font, FALSE);
            SendMessageW(ed, EM_SETLIMITTEXT, HF_MAX - 1, 0);
        }
        DarkDialog(h);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(w) == IDOK) {
        GetDlgItemTextW(h, IDC_HEADER, g_hfTmp[0], HF_MAX);
        GetDlgItemTextW(h, IDC_FOOTER, g_hfTmp[1], HF_MAX);
    }
    return DarkCtlColor(m, w);
}

static void PageSetup(void)
{
    PAGESETUPDLGW psd = {sizeof psd};
    psd.hwndOwner = g_wnd;
    psd.hDevMode = g_devMode;
    psd.hDevNames = g_devNames;
    psd.Flags = PSD_MARGINS | PSD_INTHOUSANDTHSOFINCHES | PSD_ENABLEPAGESETUPHOOK;
    psd.rtMargin = g_margins;
    psd.lpfnPageSetupHook = PageHook;
    lstrcpyW(g_hfTmp[0], g_header);
    lstrcpyW(g_hfTmp[1], g_footer);
    BOOL ok = PageSetupDlgW(&psd);
    g_devMode = psd.hDevMode;
    g_devNames = psd.hDevNames;
    if (ok) {
        g_margins = psd.rtMargin;
        lstrcpyW(g_header, g_hfTmp[0]);
        lstrcpyW(g_footer, g_hfTmp[1]);
    }
}

static void Append(WCHAR *seg, int *n, const WCHAR *s)
{
    while (*s && *n < 255) seg[(*n)++] = *s++;
}

static void PrintHeaderFooter(HDC dc, const WCHAR *fmt, int y, int left, int right, int page)
{
    WCHAR seg[3][256], tmp[64];
    int n[3] = {0, 0, 0}, cur = 1;
    SIZE sz;
    for (const WCHAR *p = fmt; *p; p++) {
        if (*p == '&' && p[1]) {
            p++;
            switch (*p | 0x20) {
            case 'l': cur = 0; break;
            case 'c': cur = 1; break;
            case 'r': cur = 2; break;
            case 'f': Append(seg[cur], &n[cur], DisplayName()); break;
            case 'p': wsprintfW(tmp, L"%d", page); Append(seg[cur], &n[cur], tmp); break;
            case 'd': GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, tmp, 64); Append(seg[cur], &n[cur], tmp); break;
            case 't': GetTimeFormatW(LOCALE_USER_DEFAULT, 0, NULL, NULL, tmp, 64); Append(seg[cur], &n[cur], tmp); break;
            case '&': Append(seg[cur], &n[cur], L"&"); break;
            }
        } else if (n[cur] < 255) {
            seg[cur][n[cur]++] = *p;
        }
    }
    if (n[0]) TextOutW(dc, left, y, seg[0], n[0]);
    if (n[1]) {
        GetTextExtentPoint32W(dc, seg[1], n[1], &sz);
        TextOutW(dc, left + (right - left - sz.cx) / 2, y, seg[1], n[1]);
    }
    if (n[2]) {
        GetTextExtentPoint32W(dc, seg[2], n[2], &sz);
        TextOutW(dc, right - sz.cx, y, seg[2], n[2]);
    }
}

static void PrintText(HDC dc, const WCHAR *text, int len)
{
    int dx = GetDeviceCaps(dc, LOGPIXELSX), dy = GetDeviceCaps(dc, LOGPIXELSY);
    int offX = GetDeviceCaps(dc, PHYSICALOFFSETX), offY = GetDeviceCaps(dc, PHYSICALOFFSETY);
    int pw = GetDeviceCaps(dc, PHYSICALWIDTH), ph = GetDeviceCaps(dc, PHYSICALHEIGHT);
    int L = MulDiv(g_margins.left, dx, 1000) - offX, R = pw - MulDiv(g_margins.right, dx, 1000) - offX;
    int T = MulDiv(g_margins.top, dy, 1000) - offY, B = ph - MulDiv(g_margins.bottom, dy, 1000) - offY;
    LOGFONTW lf = g_lf;
    TEXTMETRICW tm;
    DOCINFOW di = {sizeof di};
    int page = 0, y = 0, pos = 0, cap = 0;
    WCHAR *buf = NULL;
    BOOL ok = TRUE;

    if (L < 0) L = 0;
    if (T < 0) T = 0;
    lf.lfHeight = -MulDiv(g_fontPt10, dy, 720);
    lf.lfWidth = 0;
    HFONT font = CreateFontIndirectW(&lf), oldFont = SelectObject(dc, font);
    GetTextMetricsW(dc, &tm);
    int lh = tm.tmHeight + tm.tmExternalLeading;
    int bodyTop = T + (g_header[0] ? 2 * lh : 0), bodyBot = B - (g_footer[0] ? 2 * lh : 0);
    if (R - L < tm.tmMaxCharWidth || bodyBot - bodyTop < lh) {
        Msg(NULL, MB_ICONERROR, L"The margins are too large to print on this page size.");
        goto done;
    }
    di.lpszDocName = DisplayName();
    if (StartDocW(dc, &di) <= 0) {
        ErrorBox(L"Print", GetLastError());
        goto done;
    }

#define NEW_PAGE() do { \
        if (page && EndPage(dc) <= 0) { ok = FALSE; goto end; } \
        if (StartPage(dc) <= 0) { ok = FALSE; goto end; } \
        page++; SelectObject(dc, font); \
        if (g_header[0]) PrintHeaderFooter(dc, g_header, T, L, R, page); \
        if (g_footer[0]) PrintHeaderFooter(dc, g_footer, B - lh, L, R, page); \
        y = bodyTop; \
    } while (0)

    NEW_PAGE();
    while (pos < len) {
        int end = pos, n = 0;
        while (end < len && text[end] != '\r' && text[end] != '\n') end++;
        int need = 1;
        for (int i = pos; i < end; i++) need += text[i] == '\t' ? 8 : 1;
        if (need > cap) {
            Free(buf);
            cap = need;
            buf = Alloc(cap * sizeof(WCHAR));
        }
        for (int i = pos; i < end; i++) {
            if (text[i] == '\t') {
                do buf[n++] = ' '; while (n % 8);
            } else {
                buf[n++] = text[i];
            }
        }
        int p = 0;
        for (;;) {
            if (y + lh > bodyBot) NEW_PAGE();
            if (!n) {
                y += lh;
                break;
            }
            int fit = 0;
            SIZE sz;
            GetTextExtentExPointW(dc, buf + p, n - p, R - L, &fit, NULL, &sz);
            if (fit <= 0) fit = 1;
            if (p + fit < n) {
                int k = fit;
                while (k > 0 && buf[p + k - 1] != ' ') k--;
                if (k > 0) fit = k;
            }
            TextOutW(dc, L, y, buf + p, fit);
            y += lh;
            p += fit;
            if (p >= n) break;
        }
        pos = end;
        if (pos < len && text[pos] == '\r') pos++;
        if (pos < len && text[pos] == '\n' && (pos == end || text[pos - 1] == '\r')) pos++;
    }
end:
    if (ok && EndPage(dc) > 0) EndDoc(dc);
    else AbortDoc(dc);
#undef NEW_PAGE
done:
    Free(buf);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

static void Print(void)
{
    PRINTPAGERANGE range = {1, 1};
    PRINTDLGEXW pd = {sizeof pd};
    DWORD s, e;
    Ed(EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    pd.hwndOwner = g_wnd;
    pd.hDevMode = g_devMode;
    pd.hDevNames = g_devNames;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOCURRENTPAGE | PD_USEDEVMODECOPIESANDCOLLATE | (s == e ? PD_NOSELECTION : 0);
    pd.nMaxPageRanges = 1;
    pd.lpPageRanges = &range;
    pd.nCopies = 1;
    pd.nStartPage = START_PAGE_GENERAL;
    if (FAILED(PrintDlgExW(&pd))) return;
    g_devMode = pd.hDevMode;
    g_devNames = pd.hDevNames;
    if (pd.dwResultAction == PD_RESULT_PRINT && pd.hDC) {
        int len;
        HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
        WCHAR *t = (pd.Flags & PD_SELECTION) ? GetSelText(&len) : GetEditText(&len);
        PrintText(pd.hDC, t, len);
        Free(t);
        SetCursor(old);
    }
    if (pd.hDC) DeleteDC(pd.hDC);
}


static void SearchBing(void)
{
    static const char hex[] = "0123456789ABCDEF";
    int len;
    WCHAR *sel = GetSelText(&len);
    if (len > 0) {
        if (len > 500) len = 500;
        int n = WideCharToMultiByte(CP_UTF8, 0, sel, len, NULL, 0, NULL, NULL);
        char *u = Alloc(n + 1);
        WideCharToMultiByte(CP_UTF8, 0, sel, len, u, n, NULL, NULL);
        WCHAR *url = Alloc((40 + n * 3) * sizeof(WCHAR)), *o = url;
        for (const WCHAR *p = L"https://www.bing.com/search?q="; *p; p++) *o++ = *p;
        for (int i = 0; i < n; i++) {
            unsigned char c = (unsigned char)u[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                *o++ = c;
            } else {
                *o++ = '%';
                *o++ = hex[c >> 4];
                *o++ = hex[c & 15];
            }
        }
        *o = 0;
        ShellExecuteW(g_wnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        Free(url);
        Free(u);
    }
    Free(sel);
}

static void NewWindow(void)
{
    WCHAR exe[PATH_MAX_];
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    GetModuleFileNameW(NULL, exe, PATH_MAX_);
    if (CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static void ToggleStatus(void)
{
    g_statusOn = !g_statusOn;
    ShowWindow(g_status, g_statusOn ? SW_SHOW : SW_HIDE);
    Layout();
    if (g_statusOn) UpdateStatusAll();
}

static void InitMenu(HMENU m)
{
    DWORD s, e;
    Ed(EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    UINT sel = s != e ? MF_ENABLED : MF_GRAYED;
    UINT any = GetWindowTextLengthW(g_edit) ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(m, IDM_UNDO, Ed(EM_CANUNDO, 0, 0) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IDM_CUT, sel);
    EnableMenuItem(m, IDM_COPY, sel);
    EnableMenuItem(m, IDM_DELETE, sel);
    EnableMenuItem(m, IDM_BING, sel);
    EnableMenuItem(m, IDM_PASTE, IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IDM_FIND, any);
    EnableMenuItem(m, IDM_FINDNEXT, any);
    EnableMenuItem(m, IDM_FINDPREV, any);
    CheckMenuItem(m, IDM_WRAP, g_wrap ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_STATUSBAR, g_statusOn ? MF_CHECKED : MF_UNCHECKED);
}

static void OnCommand(int id)
{
    WCHAR buf[PATH_MAX_];
    int enc;
    switch (id) {
    case IDM_NEW: if (CheckSave()) NewFile(); break;
    case IDM_NEWWIN: NewWindow(); break;
    case IDM_OPEN:
        if (CheckSave() && FileDialog(FALSE, buf, &enc)) LoadFile(buf, enc);
        break;
    case IDM_SAVE: Save(); break;
    case IDM_SAVEAS: SaveAs(); break;
    case IDM_PAGESETUP: PageSetup(); break;
    case IDM_PRINT: Print(); break;
    case IDM_EXIT: PostMessageW(g_wnd, WM_CLOSE, 0, 0); break;
    case IDM_UNDO: Ed(WM_UNDO, 0, 0); break;
    case IDM_CUT: Ed(WM_CUT, 0, 0); break;
    case IDM_COPY: Ed(WM_COPY, 0, 0); break;
    case IDM_PASTE: Ed(WM_PASTE, 0, 0); break;
    case IDM_DELETE: Ed(WM_CLEAR, 0, 0); break;
    case IDM_BING: SearchBing(); break;
    case IDM_FIND: OpenFind(FALSE); break;
    case IDM_REPLACE: OpenFind(TRUE); break;
    case IDM_FINDNEXT:
    case IDM_FINDPREV:
        if (g_findText[0]) DoFind(id == IDM_FINDNEXT, FALSE);
        else OpenFind(FALSE);
        break;
    case IDM_GOTO: GoTo(); break;
    case IDM_SELECTALL: Ed(EM_SETSEL, 0, -1); break;
    case IDM_TIMEDATE:
        TimeDate(buf, 128);
        Ed(EM_REPLACESEL, TRUE, (LPARAM)buf);
        break;
    case IDM_WRAP: SetWrap(!g_wrap); break;
    case IDM_FONT: ChooseEditorFont(); break;
    case IDM_ZOOMIN: SetZoom(g_zoom + 10); break;
    case IDM_ZOOMOUT: SetZoom(g_zoom - 10); break;
    case IDM_ZOOMRESET: SetZoom(100); break;
    case IDM_STATUSBAR: ToggleStatus(); break;
    case IDM_ABOUT:
        ShellAboutW(g_wnd, APP, L"A lightweight Notepad replacement.", LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_APP)));
        break;
    }
}


static DWORD RegDword(HKEY k, const WCHAR *name, DWORD def)
{
    DWORD v, sz = sizeof v, t;
    if (k && RegQueryValueExW(k, name, NULL, &t, (BYTE *)&v, &sz) == ERROR_SUCCESS && t == REG_DWORD) return v;
    return def;
}

static void RegString(HKEY k, const WCHAR *name, WCHAR *buf, DWORD cch)
{
    DWORD sz = (cch - 1) * sizeof(WCHAR), t;
    WCHAR tmp[256];
    if (k && RegQueryValueExW(k, name, NULL, &t, (BYTE *)tmp, &sz) == ERROR_SUCCESS && t == REG_SZ) {
        tmp[sz / sizeof(WCHAR)] = 0;
        lstrcpynW(buf, tmp, cch);
    }
}

static void SetDword(HKEY k, const WCHAR *name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, (BYTE *)&v, sizeof v); }
static void SetString(HKEY k, const WCHAR *name, const WCHAR *s) { RegSetValueExW(k, name, 0, REG_SZ, (BYTE *)s, (lstrlenW(s) + 1) * sizeof(WCHAR)); }

static RECT g_winRect;

static void LoadSettings(void)
{
    HKEY k = NULL;
    RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &k);
    lstrcpyW(g_lf.lfFaceName, L"Consolas");
    RegString(k, L"lfFaceName", g_lf.lfFaceName, LF_FACESIZE);
    g_fontPt10 = RegDword(k, L"iPointSize", 110);
    g_lf.lfWeight = RegDword(k, L"lfWeight", FW_NORMAL);
    g_lf.lfItalic = (BYTE)RegDword(k, L"lfItalic", 0);
    g_lf.lfCharSet = (BYTE)RegDword(k, L"lfCharSet", DEFAULT_CHARSET);
    g_lf.lfPitchAndFamily = (BYTE)RegDword(k, L"lfPitchAndFamily", FIXED_PITCH | FF_MODERN);
    g_wrap = RegDword(k, L"fWrap", 0);
    g_statusOn = RegDword(k, L"StatusBar", 1);
    g_wrapAround = RegDword(k, L"fWrapAround", 0);
    g_fr.Flags = FR_DOWN | (RegDword(k, L"fMatchCase", 0) ? FR_MATCHCASE : 0);
    g_margins.left = RegDword(k, L"iMarginLeft", 750);
    g_margins.top = RegDword(k, L"iMarginTop", 1000);
    g_margins.right = RegDword(k, L"iMarginRight", 750);
    g_margins.bottom = RegDword(k, L"iMarginBottom", 1000);
    RegString(k, L"szHeader", g_header, HF_MAX);
    RegString(k, L"szTrailer", g_footer, HF_MAX);
    g_winRect.left = RegDword(k, L"iWindowPosX", CW_USEDEFAULT);
    g_winRect.top = RegDword(k, L"iWindowPosY", CW_USEDEFAULT);
    g_winRect.right = RegDword(k, L"iWindowPosDX", 0);
    g_winRect.bottom = RegDword(k, L"iWindowPosDY", 0);
    if (k) RegCloseKey(k);
}

static void SaveSettings(void)
{
    HKEY k;
    WINDOWPLACEMENT wp = {sizeof wp};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return;
    SetString(k, L"lfFaceName", g_lf.lfFaceName);
    SetDword(k, L"iPointSize", g_fontPt10);
    SetDword(k, L"lfWeight", g_lf.lfWeight);
    SetDword(k, L"lfItalic", g_lf.lfItalic);
    SetDword(k, L"lfCharSet", g_lf.lfCharSet);
    SetDword(k, L"lfPitchAndFamily", g_lf.lfPitchAndFamily);
    SetDword(k, L"fWrap", g_wrap);
    SetDword(k, L"StatusBar", g_statusOn);
    SetDword(k, L"fWrapAround", g_wrapAround);
    SetDword(k, L"fMatchCase", (g_fr.Flags & FR_MATCHCASE) != 0);
    SetDword(k, L"iMarginLeft", g_margins.left);
    SetDword(k, L"iMarginTop", g_margins.top);
    SetDword(k, L"iMarginRight", g_margins.right);
    SetDword(k, L"iMarginBottom", g_margins.bottom);
    SetString(k, L"szHeader", g_header);
    SetString(k, L"szTrailer", g_footer);
    if (GetWindowPlacement(g_wnd, &wp)) {
        SetDword(k, L"iWindowPosX", wp.rcNormalPosition.left);
        SetDword(k, L"iWindowPosY", wp.rcNormalPosition.top);
        SetDword(k, L"iWindowPosDX", wp.rcNormalPosition.right - wp.rcNormalPosition.left);
        SetDword(k, L"iWindowPosDY", wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
    }
    RegCloseKey(k);
}


static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == g_findMsg && g_findMsg) {
        OnFindMsg((FINDREPLACEW *)l);
        return 0;
    }
    switch (m) {
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, COL_TEXT);
        SetBkColor((HDC)w, COL_BG);
        return (LRESULT)g_brBg;
    case WM_UAHDRAWMENU:
        DrawMenuBar_((UAHMENU *)l);
        return TRUE;
    case WM_UAHDRAWMENUITEM:
        DrawMenuBarItem((UAHDRAWMENUITEM *)l);
        return TRUE;
    case WM_NCPAINT:
    case WM_NCACTIVATE: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        CoverMenuLine();
        return r;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_EDIT) {
            if (HIWORD(w) == EN_CHANGE) UpdateTitle();
            else if (HIWORD(w) == EN_ERRSPACE || HIWORD(w) == EN_MAXTEXT)
                Msg(NULL, MB_ICONERROR, L"Not enough memory to complete this operation.");
            return 0;
        }
        OnCommand(LOWORD(w));
        return 0;
    case WM_INITMENUPOPUP:
        InitMenu((HMENU)w);
        return 0;
    case WM_SIZE:
        Layout();
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_edit);
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateUiFonts();
        ApplyFont();
        Layout();
        return 0;
    }
    case WM_DROPFILES: {
        WCHAR path[PATH_MAX_];
        UINT ok = DragQueryFileW((HDROP)w, 0, path, PATH_MAX_);
        DragFinish((HDROP)w);
        SetForegroundWindow(h);
        if (ok && CheckSave()) LoadFile(path, ENC_AUTO);
        return 0;
    }
    case WM_QUERYENDSESSION:
        return CheckSave();
    case WM_ENDSESSION:
        if (w) SaveSettings();
        return 0;
    case WM_CLOSE:
        if (CheckSave()) {
            SaveSettings();
            DestroyWindow(h);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd;
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_BAR_CLASSES};
    WNDCLASSEXW wc = {sizeof wc};
    MSG msg;
    g_inst = inst;
    InitCommonControlsEx(&icc);
    InitDarkMode();
    LoadSettings();

    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszMenuName = MAKEINTRESOURCEW(IDM_MAIN);
    wc.lpszClassName = APP;
    RegisterClassExW(&wc);

    BOOL hasPos = g_winRect.right > 0 && g_winRect.bottom > 0;
    g_wnd = CreateWindowExW(WS_EX_ACCEPTFILES, APP, APP, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, inst, NULL);
    DarkWindow(g_wnd, NULL);
    DarkTitle(g_wnd);
    CreateEditor();
    g_status = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | SBARS_SIZEGRIP | (g_statusOn ? WS_VISIBLE : 0),
                               0, 0, 0, 0, g_wnd, (HMENU)ID_STATUS, inst, NULL);
    g_statusProc = (WNDPROC)SetWindowLongPtrW(g_status, GWLP_WNDPROC, (LONG_PTR)StatusProc);
    UpdateUiFonts();
    Layout();
    ApplyFont();
    g_findMsg = RegisterWindowMessageW(FINDMSGSTRINGW);
    ForceTitle();
    UpdateStatusAll();
    OpenFromCommandLine();

    if (hasPos) {
        WINDOWPLACEMENT wp = {sizeof wp};
        GetWindowPlacement(g_wnd, &wp);
        SetRect(&wp.rcNormalPosition, g_winRect.left, g_winRect.top, g_winRect.left + g_winRect.right,
                g_winRect.top + g_winRect.bottom);
        wp.showCmd = show;
        SetWindowPlacement(g_wnd, &wp);
    } else {
        ShowWindow(g_wnd, show);
    }
    Layout();
    SetFocus(g_edit);

    HACCEL acc = LoadAcceleratorsW(inst, MAKEINTRESOURCEW(IDA_MAIN));
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_find && IsDialogMessageW(g_find, &msg)) continue;
        if (TranslateAcceleratorW(g_wnd, acc, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

/* Without the C runtime, some gcc versions still emit calls to these; build.bat passes
   -fno-tree-loop-distribute-patterns so the loops are not turned back into self-calls. */
void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    return memmove(dst, src, n);
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

/* Built without the C runtime so startup loads no CRT DLLs; this replaces its entry point. */
void WINAPI Entry(void)
{
    STARTUPINFOW si;
    GetStartupInfoW(&si);
    int show = (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT;
    ExitProcess(wWinMain(GetModuleHandleW(NULL), NULL, NULL, show));
}
