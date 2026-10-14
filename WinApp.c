#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include "resource.h"
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
    name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
    processorArchitecture='*' publicKeyToken='6595b64144ccf1df' \
    language='*'\"")

//menus
#define IDM_EXIT        1001
#define IDM_ABOUT       1002
#define IDM_SHOWLOG     1003
#define IDM_OPENFOLDER  1004
#define IDM_LICENSING   2010

//controls
#define EDT_INPUT       2001
#define BTN_BROWSE      2002
#define EDT_USERNAME    2003
#define EDT_PASSWORD    2004
#define CHK_EMBEDAUTH   2005
#define EDT_OUTPUT      2006
#define BTN_SAVE        2007
#define CHK_SPLITFOLDER 2008
#define BTN_GENERATE    2009

//licensing window controls
#define BTN_CURL_LIC    3001
#define BTN_FFMPEG_LIC  3002

//buffer sizes in characters
#define INPUT_CCH       2048
#define CMD_CCH         32768 //CreateProcess limit

//worker thread messages
#define WM_APP_LOG      (WM_APP + 1) //lParam: heap string, receiver frees
#define WM_APP_DONE     (WM_APP + 2) //lParam: RunJob, receiver frees

//a running d2m3u process. hProcess is closed and the job freed by FinishRun
typedef struct {
    HWND hWnd;
    HANDLE hProcess;
    HANDLE hReadPipe;
    DWORD exitCode;
    const WCHAR* outputError; //first problem reading output, NULL if none
    WCHAR outputDir[MAX_PATH];
} RunJob;

HINSTANCE hInst;
HFONT     hFont;
HBRUSH    hBgBrush;
HMENU     hFileMenu;

HWND hEdtInput;
HWND hBtnBrowse;
HWND hLblUsername;
HWND hEdtUsername;
HWND hLblPassword;
HWND hEdtPassword;
HWND hChkEmbedAuth;
HWND hEdtOutput;
HWND hBtnSave;
HWND hChkSplit;
HWND hBtnGenerate;

//log winow
HWND hWndLog = NULL;
HWND hEdtLog = NULL;
BOOL bLogVisible = FALSE;

//one instance each of the licensing and about windows
HWND hWndLicense = NULL;
HWND hWndAbout = NULL;

//only touched on the ui thread
RunJob* pRunJob = NULL;

static BOOL IsUrl(const WCHAR* text) {
    return (wcsncmp(text, L"http://", 7) == 0 ||
        wcsncmp(text, L"https://", 8) == 0);
    //could add ftp support? but need to add in base first
}

//strips whitespace and the quotes added by explorer's "Copy as path"
static void TrimPath(WCHAR* text) {
    size_t len = wcslen(text);
    size_t start = 0;

    while (len > 0 && text[len - 1] <= L' ')
        text[--len] = L'\0';
    while (text[start] && text[start] <= L' ')
        start++;
    if (start > 0) {
        memmove(text, text + start, (len - start + 1) * sizeof(WCHAR));
        len -= start;
    }
    if (len >= 2 && text[0] == L'"' && text[len - 1] == L'"') {
        memmove(text, text + 1, (len - 2) * sizeof(WCHAR));
        text[len - 2] = L'\0';
    }
}

static BOOL IsDriveLetter(WCHAR c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

//C:\blabla or \\server\share
static BOOL IsAbsolutePath(const WCHAR* path) {
    if (IsDriveLetter(path[0]) && path[1] == L':')
        return path[2] == L'\\' || path[2] == L'/';
    return (path[0] == L'\\' || path[0] == L'/') &&
        (path[1] == L'\\' || path[1] == L'/');
}

static BOOL IsDirectory(const WCHAR* path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

//drops the last component, keeps the trailing separator
static void StripFileName(WCHAR* path) {
    WCHAR* slash = wcsrchr(path, L'\\');
    if (slash)
        *(slash + 1) = L'\0';
}

static BOOL HasInvalidPathChars(const WCHAR* path) {
    for (size_t i = 0; path[i]; i++) {
        WCHAR c = path[i];
        if (c < L' ' || wcschr(L"<>\"|?*", c))
            return TRUE;
        if (c == L':' && !(i == 1 && IsDriveLetter(path[0]) && IsAbsolutePath(path)))
            return TRUE;
    }
    return FALSE;
}

//returns NULL on success with the full path in dst, otherwise the message to show.
//a relative path is placed next to the input, an existing folder gets playlist.m3u
//and .m3u is appended when missing so dst is exactly what d2m3u writes
static const WCHAR* ResolveOutputPath(const WCHAR* input, BOOL inputIsUrl,
    const WCHAR* output, WCHAR* dst, size_t dstCch) {
    WCHAR joined[MAX_PATH * 2];
    WCHAR parent[MAX_PATH];
    DWORD len;
    size_t n;

    if (HasInvalidPathChars(output))
        return L"The output path contains characters that are not allowed in file names.";

    if (IsAbsolutePath(output)) {
        if (wcsncpy_s(joined, ARRAYSIZE(joined), output, _TRUNCATE) != 0)
            return L"The output path is too long.";
    }
    else {
        WCHAR base[MAX_PATH];
        size_t base_len;

        if (inputIsUrl)
            return L"When the input is a URL, the output must be a full path, for example C:\\Music\\playlist.m3u";
        if (wcsncpy_s(base, ARRAYSIZE(base), input, _TRUNCATE) != 0)
            return L"The input path is too long.";
        if (!IsDirectory(base))
            StripFileName(base); //input is a single file
        base_len = wcslen(base);
        if (base_len == 0 || !IsDirectory(base))
            return L"The input folder does not exist.";
        if (_snwprintf_s(joined, ARRAYSIZE(joined), _TRUNCATE, L"%s%s%s", base,
            base[base_len - 1] == L'\\' ? L"" : L"\\", output) < 0)
            return L"The output path is too long.";
    }

    len = GetFullPathNameW(joined, (DWORD)dstCch, dst, NULL);
    if (len == 0 || len >= dstCch)
        return L"The output path is invalid or too long.";
    //names like NUL or CON resolve to a device path
    if (wcsncmp(dst, L"\\\\.\\", 4) == 0)
        return L"The output file name is reserved by Windows.";

    n = wcslen(dst);
    if (IsDirectory(dst)) {
        if (_snwprintf_s(dst + n, dstCch - n, _TRUNCATE, L"%splaylist.m3u",
            dst[n - 1] == L'\\' ? L"" : L"\\") < 0)
            return L"The output path is too long.";
        return NULL;
    }

    if (n < 4 || _wcsicmp(dst + n - 4, L".m3u") != 0) {
        if (wcsncat_s(dst, dstCch, L".m3u", _TRUNCATE) != 0)
            return L"The output path is too long.";
    }
    if (IsDirectory(dst))
        return L"A folder with the output name already exists.";

    if (wcsncpy_s(parent, ARRAYSIZE(parent), dst, _TRUNCATE) != 0)
        return L"The output path is too long.";
    StripFileName(parent);
    if (!IsDirectory(parent))
        return L"The folder for the output file does not exist.";

    return NULL;
}

static BOOL AnyFileMatches(const WCHAR* pattern) {
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(pattern, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE)
        return FALSE;
    FindClose(find_handle);
    return TRUE;
}

//true if going to overwite. check splitting
static BOOL OutputExists(const WCHAR* output, BOOL splitting) {
    WCHAR dir[MAX_PATH];
    WCHAR pattern[MAX_PATH + 8];

    if (!splitting)
        return AnyFileMatches(output);

    if (wcsncpy_s(dir, ARRAYSIZE(dir), output, _TRUNCATE) != 0)
        return TRUE;
    StripFileName(dir);
    const WCHAR* name = output + wcslen(dir);

    if (_snwprintf_s(pattern, ARRAYSIZE(pattern), _TRUNCATE, L"%s??_%s", dir, name) < 0 ||
        AnyFileMatches(pattern))
        return TRUE;
    return FALSE;
}

static void UpdateAuthFields(HWND hWnd) {
    WCHAR text[1024];
    GetWindowTextW(hEdtInput, text, 1024);
    BOOL url_mode = IsUrl(text);
    EnableWindow(hEdtUsername, url_mode);
    EnableWindow(hEdtPassword, url_mode);
    EnableWindow(hLblUsername, url_mode);
    EnableWindow(hLblPassword, url_mode);
    EnableWindow(hChkEmbedAuth, url_mode);
}

static HWND MakeLabel(HWND hWnd, const WCHAR* text, int x, int y, int w, int h) {
    HWND hLabel = CreateWindowW(L"STATIC", text,
        WS_VISIBLE | WS_CHILD,
        x, y, w, h,
        hWnd, NULL, hInst, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    return hLabel;
}

static BOOL BrowseForFolder(HWND hWnd, WCHAR* outPath, DWORD outSize) {
    BOOL result = FALSE;
    IFileOpenDialog* pDlg = NULL;

    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
        &IID_IFileOpenDialog, (void**)&pDlg)))
        return FALSE;

    DWORD opts;
    pDlg->lpVtbl->GetOptions(pDlg, &opts);
    pDlg->lpVtbl->SetOptions(pDlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pDlg->lpVtbl->SetTitle(pDlg, L"Select Input Folder");

    if (SUCCEEDED(pDlg->lpVtbl->Show(pDlg, hWnd))) {
        IShellItem* pItem = NULL;
        if (SUCCEEDED(pDlg->lpVtbl->GetResult(pDlg, &pItem))) {
            PWSTR path = NULL;
            if (SUCCEEDED(pItem->lpVtbl->GetDisplayName(pItem, SIGDN_FILESYSPATH, &path))) {
                wcsncpy_s(outPath, outSize, path, _TRUNCATE);
                CoTaskMemFree(path);
                result = TRUE;
            }
            pItem->lpVtbl->Release(pItem);
        }
    }
    pDlg->lpVtbl->Release(pDlg);
    return result;
}

static BOOL BrowseForSaveFile(HWND hWnd, WCHAR* outPath, DWORD outSize) {
    BOOL result = FALSE;
    IFileSaveDialog* pDlg = NULL;

    if (FAILED(CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
        &IID_IFileSaveDialog, (void**)&pDlg)))
        return FALSE;

    COMDLG_FILTERSPEC filter = { L"M3U Playlist (*.m3u)", L"*.m3u" };
    pDlg->lpVtbl->SetFileTypes(pDlg, 1, &filter);
    pDlg->lpVtbl->SetDefaultExtension(pDlg, L"m3u");
    pDlg->lpVtbl->SetFileName(pDlg, L"playlist.m3u");
    pDlg->lpVtbl->SetTitle(pDlg, L"Select Save Location of Playlist");

    DWORD opts;
    pDlg->lpVtbl->GetOptions(pDlg, &opts);
    pDlg->lpVtbl->SetOptions(pDlg, opts | FOS_FORCEFILESYSTEM);

    if (SUCCEEDED(pDlg->lpVtbl->Show(pDlg, hWnd))) {
        IShellItem* pItem = NULL;
        if (SUCCEEDED(pDlg->lpVtbl->GetResult(pDlg, &pItem))) {
            PWSTR path = NULL;
            if (SUCCEEDED(pItem->lpVtbl->GetDisplayName(pItem, SIGDN_FILESYSPATH, &path))) {
                wcsncpy_s(outPath, outSize, path, _TRUNCATE);
                CoTaskMemFree(path);
                result = TRUE;
            }
            pItem->lpVtbl->Release(pItem);
        }
    }
    pDlg->lpVtbl->Release(pDlg);
    return result;
}

//log window funcs
static void LogPrint(const WCHAR* text) {
    if (!hEdtLog) return;
    int len = GetWindowTextLengthW(hEdtLog);
    SendMessage(hEdtLog, EM_SETSEL, len, len);
    SendMessage(hEdtLog, EM_REPLACESEL, FALSE, (LPARAM)text);
}

static LRESULT CALLBACK LogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        if (hEdtLog) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            MoveWindow(hEdtLog, 0, 0, rc.right, rc.bottom, TRUE);
        }
        break;
    }
    case WM_CLOSE:
        //hides, doesnt destroy to preserve log contents
        ShowWindow(hWnd, SW_HIDE);
        bLogVisible = FALSE;
        {
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_STATE;
            mii.fState = MFS_UNCHECKED;
            SetMenuItemInfoW(hFileMenu, IDM_SHOWLOG, FALSE, &mii);
        }
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static void CreateLogWindow(HWND hParent) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = LogWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"LogWindow";
    RegisterClassW(&wc);

    hWndLog = CreateWindowW(L"LogWindow", L"d2m3u - Log",
        WS_OVERLAPPEDWINDOW,
        150, 150, 600, 400,
        hParent, NULL, hInst, NULL);

    hEdtLog = CreateWindowW(L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, 0, 600, 400,
        hWndLog, NULL, hInst, NULL);
    //override 30k limit to avoid truncation
    SendMessage(hEdtLog, EM_LIMITTEXT, 0, 0);

    HFONT hMono = CreateFontW( //steeze
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
        L"Lucida Console");
    SendMessage(hEdtLog, WM_SETFONT, (WPARAM)hMono, TRUE);
}

//quotes by the CRT argv rules so backslashes and quotes survive. false if dst is too small
static BOOL QuotedArg(WCHAR* dst, size_t dstCch, const WCHAR* src) {
    size_t len = 0;

    if (dstCch < 3)
        return FALSE;
    dst[len++] = L'"';

    for (;;) {
        size_t slashes = 0;
        while (*src == L'\\') {
            slashes++;
            src++;
        }

        //backslashes before a quote or the closing quote have to be doubled
        size_t run = slashes;
        if (*src == L'\0' || *src == L'"')
            run = slashes * 2;
        if (*src == L'"')
            run++;
        if (len + run + 3 > dstCch)
            return FALSE;
        for (size_t i = 0; i < run; i++)
            dst[len++] = L'\\';

        if (*src == L'\0')
            break;
        dst[len++] = *src++;
    }

    dst[len++] = L'"';
    dst[len] = L'\0';
    return TRUE;
}

static void OpenLicenseFile(HWND hWnd, const WCHAR* filename) {
    WCHAR bin_dir[MAX_PATH];
    GetModuleFileNameW(NULL, bin_dir, MAX_PATH);
    WCHAR* slash = wcsrchr(bin_dir, L'\\');
    if (slash) *(slash + 1) = L'\0';

    WCHAR path[MAX_PATH];
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%slicenses\\%s", bin_dir, filename);

    HINSTANCE result = ShellExecuteW(hWnd, L"open", path, NULL, NULL, SW_SHOW);
    if ((INT_PTR)result <= 32) {
        MessageBoxW(hWnd, L"Could not open license file.\nMake sure it exists in the licenses\\ subfolder.",
            L"Error", MB_ICONERROR | MB_OK);
    }
}

static LRESULT CALLBACK LicenseWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hF = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND hTxt = CreateWindowW(L"STATIC",
            L"This software uses the following third-party libraries:\r\n\r\n"
            L"curl licensed under the curl license\r\n"
            L"FFmpeg licensed under the GNU LGPLv2.1\r\n\r\n"
            L"Click below to view the full license texts:",
            WS_VISIBLE | WS_CHILD,
            10, 10, 380, 120, hWnd, NULL, hInst, NULL);
        SendMessage(hTxt, WM_SETFONT, (WPARAM)hF, TRUE);

        HWND hBtnCurl = CreateWindowW(L"BUTTON", L"View curl License",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
            10, 110, 180, 28, hWnd, (HMENU)BTN_CURL_LIC, hInst, NULL);
        SendMessage(hBtnCurl, WM_SETFONT, (WPARAM)hF, TRUE);

        HWND hBtnFfmpeg = CreateWindowW(L"BUTTON", L"View FFmpeg License",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
            200, 110, 180, 28, hWnd, (HMENU)BTN_FFMPEG_LIC, hInst, NULL);
        SendMessage(hBtnFfmpeg, WM_SETFONT, (WPARAM)hF, TRUE);
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case BTN_CURL_LIC:
            OpenLicenseFile(hWnd, L"CURL_LICENSE.txt"); break;
        case BTN_FFMPEG_LIC:
            OpenLicenseFile(hWnd, L"FFMPEG_LICENSE.txt"); break;
        }
        break;
    case WM_DESTROY:
        hWndLicense = NULL;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void ShowLicenseWindow(HWND hParent) {
    if (hWndLicense) {
        SetForegroundWindow(hWndLicense);
        return;
    }

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = LicenseWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"LicenseWindow";
    RegisterClassW(&wc);

    hWndLicense = CreateWindowW(L"LicenseWindow", L"Licensing",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        200, 200, 410, 188,
        hParent, NULL, hInst, NULL);
    ShowWindow(hWndLicense, SW_SHOW);
    UpdateWindow(hWndLicense);
}

static LRESULT CALLBACK AboutWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HBITMAP hBmpScaled = NULL;
    static int imgW = 0, imgH = 0;

    switch (msg) {
    case WM_CREATE: {
        HBITMAP hBmpOrig = LoadBitmapW(hInst, MAKEINTRESOURCE(IDB_BITMAP1));
        if (hBmpOrig) {
            BITMAP bm;
            GetObject(hBmpOrig, sizeof(bm), &bm);
            imgW = bm.bmWidth * 20 / 100;
            imgH = bm.bmHeight * 20 / 100;

            HDC hdcScreen = GetDC(NULL);
            HDC hdcSrc = CreateCompatibleDC(hdcScreen);
            HDC hdcDst = CreateCompatibleDC(hdcScreen);
            hBmpScaled = CreateCompatibleBitmap(hdcScreen, imgW, imgH);

            SelectObject(hdcSrc, hBmpOrig);
            SelectObject(hdcDst, hBmpScaled);

            SetStretchBltMode(hdcDst, HALFTONE);
            StretchBlt(hdcDst, 0, 0, imgW, imgH,
                hdcSrc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);

            DeleteDC(hdcSrc);
            DeleteDC(hdcDst);
            ReleaseDC(NULL, hdcScreen);
            DeleteObject(hBmpOrig);
        }

        HFONT hF = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND hTxt = CreateWindowW(L"STATIC",
            L"d2m3u GUI for Windows\r\nprod. fujimite\r\n\r\n"
            L"Generates an m3u playlist from a local or web directory.",
            WS_VISIBLE | WS_CHILD,
            10, 10, 380, 80, hWnd, NULL, hInst, NULL);
        SendMessage(hTxt, WM_SETFONT, (WPARAM)hF, TRUE);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)(COLOR_BTNFACE + 1)); //clear first
        if (hBmpScaled) {
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hOld = SelectObject(hdcMem, hBmpScaled);
            int x = (rc.right - imgW) / 2;
            BitBlt(hdc, x, 60, imgW, imgH, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hOld);
            DeleteDC(hdcMem);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (hBmpScaled) { DeleteObject(hBmpScaled); hBmpScaled = NULL; }
        hWndAbout = NULL;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void ShowAboutWindow(HWND hParent) {
    if (hWndAbout) {
        SetForegroundWindow(hWndAbout);
        return;
    }

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = AboutWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"AboutWindow";
    RegisterClassW(&wc);

    hWndAbout = CreateWindowW(L"AboutWindow", L"About d2m3u",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        200, 200, 410, 320,
        hParent, NULL, hInst, NULL);
    ShowWindow(hWndAbout, SW_SHOW);
    UpdateWindow(hWndAbout);
}

//forwards d2m3u verbose output to the ui thread until the pipe closes
static DWORD WINAPI ReadOutputThread(LPVOID param) {
    RunJob* job = (RunJob*)param;
    char buf[1024];
    DWORD bytesRead;

    while (ReadFile(job->hReadPipe, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
        WCHAR* text = malloc((bytesRead + 1) * sizeof(WCHAR));
        if (!text) {
            if (!job->outputError)
                job->outputError = L"Ran out of memory while reading the output of d2m3u";
            continue;
        }
        int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, (int)bytesRead, text, (int)bytesRead);
        if (wlen <= 0) {
            free(text);
            if (!job->outputError)
                job->outputError = L"The output of d2m3u could not be converted to text";
            continue;
        }
        text[wlen] = L'\0';
        if (!PostMessage(job->hWnd, WM_APP_LOG, 0, (LPARAM)text))
            free(text);
    }

    CloseHandle(job->hReadPipe);
    WaitForSingleObject(job->hProcess, INFINITE);
    GetExitCodeProcess(job->hProcess, &job->exitCode);

    //the window is gone if posting fails, nobody else will free the job
    if (!PostMessage(job->hWnd, WM_APP_DONE, 0, (LPARAM)job)) {
        CloseHandle(job->hProcess);
        free(job);
    }
    return 0;
}

static void FinishRun(HWND hWnd, RunJob* job) {
    DWORD exit_code = job->exitCode;
    const WCHAR* output_error = job->outputError; //string no heap
    WCHAR output_dir[MAX_PATH];
    WCHAR log_note[256] = L"";
    wcsncpy_s(output_dir, ARRAYSIZE(output_dir), job->outputDir, _TRUNCATE);
    if (output_error) {
        _snwprintf_s(log_note, ARRAYSIZE(log_note), _TRUNCATE,
            L"\n\nLog warning: %s, so some of the log is missing.", output_error);
    }

    CloseHandle(job->hProcess);
    free(job);
    pRunJob = NULL;

    EnableWindow(hBtnGenerate, TRUE);
    SetWindowTextW(hBtnGenerate, L"Generate");

    if (exit_code == 0) {
        WCHAR msg[MAX_PATH + 320];
        _snwprintf_s(msg, ARRAYSIZE(msg), _TRUNCATE,
            L"Playlist generated successfully.\n\nSaved in:\n%s%s", output_dir, log_note);
        MessageBoxW(hWnd, msg, L"Done", output_error ? MB_ICONWARNING | MB_OK : MB_ICONINFORMATION | MB_OK);
    }
    else {
        WCHAR msg[384];
        _snwprintf_s(msg, ARRAYSIZE(msg), _TRUNCATE,
            L"d2m3u.exe exited with code %lu.\nCheck the log for details.%s", exit_code, log_note);
        MessageBoxW(hWnd, msg, L"Error", MB_ICONERROR | MB_OK);
        if (!bLogVisible) {
            bLogVisible = TRUE;
            ShowWindow(hWndLog, SW_SHOW);
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_STATE;
            mii.fState = MFS_CHECKED;
            SetMenuItemInfoW(hFileMenu, IDM_SHOWLOG, FALSE, &mii);
        }
    }
}

static void RunBase(HWND hWnd) {
    WCHAR* input = calloc(INPUT_CCH, sizeof(WCHAR));
    WCHAR* full_input = calloc(INPUT_CCH, sizeof(WCHAR));
    WCHAR* output = calloc(MAX_PATH, sizeof(WCHAR));
    WCHAR* full_output = calloc(MAX_PATH, sizeof(WCHAR));
    WCHAR* username = calloc(256, sizeof(WCHAR));
    WCHAR* password = calloc(256, sizeof(WCHAR));
    WCHAR* bin_dir = calloc(MAX_PATH, sizeof(WCHAR));
    WCHAR* bin_path = calloc(MAX_PATH, sizeof(WCHAR));
    WCHAR* cmd_buf = calloc(CMD_CCH, sizeof(WCHAR));
    WCHAR* quoted_input = calloc(INPUT_CCH * 2 + 4, sizeof(WCHAR));
    WCHAR* quoted_output = calloc(MAX_PATH * 2 + 4, sizeof(WCHAR));
    WCHAR* quoted_bin = calloc(MAX_PATH * 2 + 4, sizeof(WCHAR));

    if (!input || !full_input || !output || !full_output || !username ||
        !password || !bin_dir || !bin_path || !cmd_buf || !quoted_input ||
        !quoted_output || !quoted_bin) {
        MessageBoxW(hWnd, L"Out of memory.", L"Error", MB_ICONERROR | MB_OK);
        goto cleanup;
    }

    GetWindowTextW(hEdtInput, input, INPUT_CCH);
    GetWindowTextW(hEdtOutput, output, MAX_PATH);
    GetWindowTextW(hEdtUsername, username, 256);
    GetWindowTextW(hEdtPassword, password, 256);
    TrimPath(input);
    TrimPath(output);

    if (wcslen(input) == 0) {
        MessageBoxW(hWnd, L"Please enter an input folder or URL.", L"Missing Input", MB_ICONWARNING | MB_OK);
        goto cleanup;
    }
    if (wcslen(output) == 0) {
        MessageBoxW(hWnd, L"Please enter an output file path.", L"Missing Output", MB_ICONWARNING | MB_OK);
        goto cleanup;
    }

    //need to ignore binary location and use absolute path for in/out
    BOOL inputIsUrl = IsUrl(input);
    if (!inputIsUrl) {
        DWORD full_len = GetFullPathNameW(input, INPUT_CCH, full_input, NULL);
        if (full_len == 0 || full_len >= INPUT_CCH) {
            MessageBoxW(hWnd, L"The input path is invalid or too long.", L"Invalid Input", MB_ICONWARNING | MB_OK);
            goto cleanup;
        }
        if (GetFileAttributesW(full_input) == INVALID_FILE_ATTRIBUTES) {
            MessageBoxW(hWnd, L"The input folder does not exist.", L"Invalid Input", MB_ICONWARNING | MB_OK);
            goto cleanup;
        }
    }
    const WCHAR* final_input = inputIsUrl ? input : full_input;

    const WCHAR* output_error = ResolveOutputPath(final_input, inputIsUrl, output, full_output, MAX_PATH);
    if (output_error) {
        MessageBoxW(hWnd, output_error, L"Invalid Output", MB_ICONWARNING | MB_OK);
        goto cleanup;
    }

    BOOL embedAuth = inputIsUrl && (SendMessage(hChkEmbedAuth, BM_GETCHECK, 0, 0) == BST_CHECKED);
    BOOL splitDir = (SendMessage(hChkSplit, BM_GETCHECK, 0, 0) == BST_CHECKED);

    BOOL splitting = splitDir && (inputIsUrl || IsDirectory(full_input));
    if (OutputExists(full_output, splitting)) {
        WCHAR* overwrite_msg = calloc(MAX_PATH + 128, sizeof(WCHAR));
        if (!overwrite_msg) {
            MessageBoxW(hWnd, L"Out of memory.", L"Error", MB_ICONERROR | MB_OK);
            goto cleanup;
        }
        //split playlists are numbered, so name the folder instead of the typed file
        WCHAR shown[MAX_PATH];
        wcsncpy_s(shown, ARRAYSIZE(shown), full_output, _TRUNCATE);
        if (splitting)
            StripFileName(shown);
        _snwprintf_s(overwrite_msg, MAX_PATH + 128, _TRUNCATE,
            splitting ? L"Playlists with this name already exist in this folder and will be overwritten:\n\n%s\n\nContinue?"
            : L"This file already exists and will be overwritten:\n\n%s\n\nContinue?",
            shown);
        int answer = MessageBoxW(hWnd, overwrite_msg, L"Overwrite", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        free(overwrite_msg);
        if (answer != IDYES)
            goto cleanup;
    }

    GetModuleFileNameW(NULL, bin_dir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(bin_dir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    _snwprintf_s(bin_path, MAX_PATH, _TRUNCATE, L"%sd2m3u.exe", bin_dir);

    if (!QuotedArg(quoted_input, INPUT_CCH * 2 + 4, final_input) ||
        !QuotedArg(quoted_output, MAX_PATH * 2 + 4, full_output) ||
        !QuotedArg(quoted_bin, MAX_PATH * 2 + 4, bin_path)) {
        MessageBoxW(hWnd, L"Failed to build the command line.", L"Error", MB_ICONERROR | MB_OK);
        goto cleanup;
    }

    _snwprintf_s(cmd_buf, CMD_CCH, _TRUNCATE, L"%s", quoted_bin);
    wcsncat_s(cmd_buf, CMD_CCH, L" -v", _TRUNCATE);
    if (embedAuth)
        wcsncat_s(cmd_buf, CMD_CCH, L" -e", _TRUNCATE);
    if (splitDir)
        wcsncat_s(cmd_buf, CMD_CCH, L" -s", _TRUNCATE);

    if (inputIsUrl && wcslen(username) > 0) {
        WCHAR qUser[256 * 2 + 4], qPass[256 * 2 + 4];
        if (!QuotedArg(qUser, ARRAYSIZE(qUser), username) ||
            !QuotedArg(qPass, ARRAYSIZE(qPass), password)) {
            MessageBoxW(hWnd, L"Failed to build the command line.", L"Error", MB_ICONERROR | MB_OK);
            goto cleanup;
        }

        wcsncat_s(cmd_buf, CMD_CCH, L" -u ", _TRUNCATE);
        wcsncat_s(cmd_buf, CMD_CCH, qUser, _TRUNCATE);
        //an empty -p value would swallow the next argument
        if (wcslen(password) > 0) {
            wcsncat_s(cmd_buf, CMD_CCH, L" -p ", _TRUNCATE);
            wcsncat_s(cmd_buf, CMD_CCH, qPass, _TRUNCATE);
        }
    }

    wcsncat_s(cmd_buf, CMD_CCH, L" ", _TRUNCATE);
    wcsncat_s(cmd_buf, CMD_CCH, quoted_input, _TRUNCATE);
    wcsncat_s(cmd_buf, CMD_CCH, L" ", _TRUNCATE);
    wcsncat_s(cmd_buf, CMD_CCH, quoted_output, _TRUNCATE);

    if (bLogVisible) {
        LogPrint(L"Running: ");
        LogPrint(cmd_buf);
        LogPrint(L"\r\n");
    }

    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        MessageBoxW(hWnd, L"Failed to create pipe.", L"Error", MB_ICONERROR | MB_OK);
        goto cleanup;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessW(NULL, cmd_buf, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, bin_dir, &si, &pi)) {
        DWORD err = GetLastError();
        WCHAR msg[256];
        _snwprintf_s(msg, ARRAYSIZE(msg), _TRUNCATE,
            L"Failed to launch d2m3u.exe (error %lu).\n"
            L"Make sure d2m3u.exe is in the same folder as d2m3u-gui.exe.", err);
        MessageBoxW(hWnd, msg, L"Launch Error", MB_ICONERROR | MB_OK);
        CloseHandle(hWritePipe);
        CloseHandle(hReadPipe);
        goto cleanup;
    }

    CloseHandle(hWritePipe);
    CloseHandle(pi.hThread);

    RunJob* job = calloc(1, sizeof(RunJob));
    HANDLE hThread = NULL;
    if (job) {
        job->hWnd = hWnd;
        job->hProcess = pi.hProcess;
        job->hReadPipe = hReadPipe;
        wcsncpy_s(job->outputDir, ARRAYSIZE(job->outputDir), full_output, _TRUNCATE);
        StripFileName(job->outputDir);
        hThread = CreateThread(NULL, 0, ReadOutputThread, job, 0, NULL);
    }
    if (!hThread) {
        MessageBoxW(hWnd, L"Failed to start the worker thread.", L"Error", MB_ICONERROR | MB_OK);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(hReadPipe);
        free(job);
        goto cleanup;
    }
    CloseHandle(hThread);

    pRunJob = job;
    EnableWindow(hBtnGenerate, FALSE);
    SetWindowTextW(hBtnGenerate, L"Generating...");

cleanup:
    free(input);
    free(full_input);
    free(output);
    free(full_output);
    free(username);
    free(password);
    free(bin_dir);
    free(bin_path);
    free(cmd_buf);
    free(quoted_input);
    free(quoted_output);
    free(quoted_bin);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        HMENU hMenuBar = CreateMenu();
        hFileMenu = CreatePopupMenu();
        HMENU hHelp = CreatePopupMenu();
        AppendMenuW(hFileMenu, MF_STRING, IDM_OPENFOLDER, L"&Open Folder...");
        AppendMenuW(hFileMenu, MF_STRING, IDM_SHOWLOG, L"Show &Log");
        AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hFileMenu, MF_STRING, IDM_EXIT, L"E&xit");
        AppendMenuW(hHelp, MF_STRING, IDM_LICENSING, L"&Licensing");
        AppendMenuW(hHelp, MF_STRING, IDM_ABOUT, L"&About");
        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"&File");
        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hHelp, L"&Help");
        SetMenu(hWnd, hMenuBar);

        int x = 10, y = 10, w = 490, lh = 18, eh = 24, gap = 10;

        //input folder/url
        MakeLabel(hWnd, L"Input Folder / URL:", x, y, w, lh);
        y += lh + 2;
        hEdtInput = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            x, y, w - 40, eh,
            hWnd, (HMENU)EDT_INPUT, hInst, NULL);
        SendMessage(hEdtInput, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hEdtInput, EM_LIMITTEXT, INPUT_CCH - 1, 0);
        hBtnBrowse = CreateWindowW(L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
            x + w - 36, y, 30, eh,
            hWnd, (HMENU)BTN_BROWSE, hInst, NULL);
        SendMessage(hBtnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap;

        //username
        hLblUsername = MakeLabel(hWnd, L"Username:", x, y, w, lh);
        y += lh + 2;
        hEdtUsername = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            x, y, w - 7, eh,
            hWnd, (HMENU)EDT_USERNAME, hInst, NULL);
        SendMessage(hEdtUsername, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hEdtUsername, EM_LIMITTEXT, 255, 0);
        y += eh + gap;

        //password
        hLblPassword = MakeLabel(hWnd, L"Password:", x, y, w, lh);
        y += lh + 2;
        hEdtPassword = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
            x, y, w - 7, eh,
            hWnd, (HMENU)EDT_PASSWORD, hInst, NULL);
        SendMessage(hEdtPassword, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hEdtPassword, EM_LIMITTEXT, 255, 0);
        y += eh + gap;

        //-e flag
        hChkEmbedAuth = CreateWindowW(L"BUTTON", L"Embed authentication",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
            x, y, w, eh,
            hWnd, (HMENU)CHK_EMBEDAUTH, hInst, NULL);
        SendMessage(hChkEmbedAuth, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap;

        //output playlsit
        MakeLabel(hWnd, L"Output File:", x, y, w, lh);
        y += lh + 2;
        hEdtOutput = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            x, y, w - 40, eh,
            hWnd, (HMENU)EDT_OUTPUT, hInst, NULL);
        SendMessage(hEdtOutput, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hEdtOutput, EM_LIMITTEXT, MAX_PATH - 1, 0);
        hBtnSave = CreateWindowW(L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
            x + w - 36, y, 30, eh,
            hWnd, (HMENU)BTN_SAVE, hInst, NULL);
        SendMessage(hBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap;

        //-s flag
        hChkSplit = CreateWindowW(L"BUTTON", L"Split by folder",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
            x, y, w, eh,
            hWnd, (HMENU)CHK_SPLITFOLDER, hInst, NULL);
        SendMessage(hChkSplit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap * 2;

        //run button
        int btnW = 120, btnH = 30;
        hBtnGenerate = CreateWindowW(L"BUTTON", L"Generate",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
            x + w / 2 - btnW / 2, y, btnW, btnH,
            hWnd, (HMENU)BTN_GENERATE, hInst, NULL);
        SendMessage(hBtnGenerate, WM_SETFONT, (WPARAM)hFont, TRUE);

        CreateLogWindow(hWnd);
        UpdateAuthFields(hWnd);
        break;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, hBgBrush);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)hBgBrush;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case EDT_INPUT:
            if (HIWORD(wParam) == EN_CHANGE)
                UpdateAuthFields(hWnd);
            break;

        case BTN_BROWSE: {
            WCHAR path[MAX_PATH] = { 0 };
            if (BrowseForFolder(hWnd, path, MAX_PATH))
                SetWindowTextW(hEdtInput, path);
            break;
        }

        case BTN_SAVE: {
            WCHAR path[MAX_PATH] = { 0 };
            if (BrowseForSaveFile(hWnd, path, MAX_PATH))
                SetWindowTextW(hEdtOutput, path);
            break;
        }

        case BTN_GENERATE:
            RunBase(hWnd);
            break;

        case IDM_OPENFOLDER: {
            WCHAR path[MAX_PATH] = { 0 };
            if (BrowseForFolder(hWnd, path, MAX_PATH))
                SetWindowTextW(hEdtInput, path);
            break;
        }

        case IDM_SHOWLOG: {
            bLogVisible = !bLogVisible;
            ShowWindow(hWndLog, bLogVisible ? SW_SHOW : SW_HIDE);
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_STATE;
            mii.fState = bLogVisible ? MFS_CHECKED : MFS_UNCHECKED;
            SetMenuItemInfoW(hFileMenu, IDM_SHOWLOG, FALSE, &mii);
            break;
        }

        case IDM_LICENSING:
            ShowLicenseWindow(hWnd);
            break;

        case IDM_ABOUT:
            ShowAboutWindow(hWnd);
            break;

        case IDM_EXIT:
            SendMessage(hWnd, WM_CLOSE, 0, 0);
            break;
        }
        break;

    case WM_APP_LOG:
        LogPrint((const WCHAR*)lParam);
        free((void*)lParam);
        return 0;

    case WM_APP_DONE:
        FinishRun(hWnd, (RunJob*)lParam);
        return 0;

    case WM_CLOSE:
        if (pRunJob && MessageBoxW(hWnd, L"d2m3u is still running. Stop it and exit?",
            L"Exit", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
            return 0;
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        //unemployement time
        if (pRunJob)
            TerminateProcess(pRunJob->hProcess, 1);
        DeleteObject(hFont);
        DeleteObject(hBgBrush);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrev, _In_ LPSTR lpCmd, _In_ int nShow) {
    hInst = hInstance;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBox(NULL, L"COM init failed.", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    hBgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = hBgBrush;
    wc.lpszClassName = L"MainWindow";
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1));
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowW(L"MainWindow", L"d2m3u",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        100, 100, 525, 400,
        NULL, NULL, hInst, NULL);

    ShowWindow(hWnd, nShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    CoUninitialize();
    return (int)msg.wParam;
}
