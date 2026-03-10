#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
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

static BOOL IsUrl(const WCHAR* text) {
    return (wcsncmp(text, L"http://", 7) == 0 ||
        wcsncmp(text, L"https://", 8) == 0);
    //could add ftp support? but need to add in base first
}

static void UpdateAuthFields(HWND hWnd) {
    WCHAR text[1024];
    GetWindowTextW(hEdtInput, text, 1024);
    BOOL url_mode = IsUrl(text);
    EnableWindow(hEdtUsername, url_mode);
    EnableWindow(hEdtPassword, url_mode);
    EnableWindow(hLblUsername, url_mode);
    EnableWindow(hLblPassword, url_mode);
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
    pDlg->lpVtbl->SetOptions(pDlg, opts | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM);

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

    hWndLog = CreateWindowW(L"LogWindow", L"d2m3u — Log",
        WS_OVERLAPPEDWINDOW,
        150, 150, 600, 400,
        hParent, NULL, hInst, NULL);

    hEdtLog = CreateWindowW(L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, 0, 600, 400,
        hWndLog, NULL, hInst, NULL);

    HFONT hMono = CreateFontW( //steeze
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
        L"Lucida Console");
    SendMessage(hEdtLog, WM_SETFONT, (WPARAM)hMono, TRUE);
}

static void QuotedArg(WCHAR* dst, size_t dstCch, const WCHAR* src) {
    if (wcschr(src, L' ') || wcschr(src, L'\t'))
        _snwprintf_s(dst, dstCch, _TRUNCATE, L"\"%s\"", src);
    else
        wcsncpy_s(dst, dstCch, src, _TRUNCATE);
}

static void RunBase(HWND hWnd) {
    WCHAR* input = calloc(MAX_PATH, sizeof(WCHAR));
    WCHAR* output = calloc(MAX_PATH, sizeof(WCHAR));
    WCHAR* username = calloc(256, sizeof(WCHAR));
    WCHAR* password = calloc(256, sizeof(WCHAR));
    WCHAR* bin_dir = calloc(MAX_PATH, sizeof(WCHAR));
    WCHAR* bin_path = calloc(MAX_PATH, sizeof(WCHAR));
    WCHAR* cmd_buf = calloc(4096, sizeof(WCHAR));
    WCHAR* quoted_input = calloc(MAX_PATH + 2, sizeof(WCHAR));
    WCHAR* quoted_output = calloc(MAX_PATH + 2, sizeof(WCHAR));
    WCHAR* quoted_bin = calloc(MAX_PATH + 2, sizeof(WCHAR));
    WCHAR* pos_buf = calloc(MAX_PATH * 2 + 8, sizeof(WCHAR));

    if (!input || !output || !username || !password || !bin_dir ||
        !bin_path || !cmd_buf || !quoted_input || !quoted_output || !quoted_bin || !pos_buf) {
        MessageBoxW(hWnd, L"Out of memory.", L"Error", MB_ICONERROR | MB_OK);
        goto cleanup;
    }

    GetWindowTextW(hEdtInput, input, MAX_PATH);
    GetWindowTextW(hEdtOutput, output, MAX_PATH);
    GetWindowTextW(hEdtUsername, username, 256);
    GetWindowTextW(hEdtPassword, password, 256);

    if (wcslen(input) == 0) {
        MessageBoxW(hWnd, L"Please enter an input folder or URL.", L"Missing Input", MB_ICONWARNING | MB_OK);
        goto cleanup;
    }
    if (wcslen(output) == 0) {
        MessageBoxW(hWnd, L"Please enter an output file path.", L"Missing Output", MB_ICONWARNING | MB_OK);
        goto cleanup;
    }

    BOOL embedAuth = (SendMessage(hChkEmbedAuth, BM_GETCHECK, 0, 0) == BST_CHECKED);
    BOOL splitDir = (SendMessage(hChkSplit, BM_GETCHECK, 0, 0) == BST_CHECKED);

    GetModuleFileNameW(NULL, bin_dir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(bin_dir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    _snwprintf_s(bin_path, MAX_PATH, _TRUNCATE, L"%sd2m3u.exe", bin_dir);

    QuotedArg(quoted_input, MAX_PATH + 2, input);
    QuotedArg(quoted_output, MAX_PATH + 2, output);
    QuotedArg(quoted_bin, MAX_PATH + 2, bin_path);

    _snwprintf_s(cmd_buf, 4096, _TRUNCATE, L"%s", quoted_bin);
    wcsncat_s(cmd_buf, 4096, L" -v", _TRUNCATE);
    if (embedAuth)
        wcsncat_s(cmd_buf, 4096, L" -e", _TRUNCATE);
    if (splitDir)
        wcsncat_s(cmd_buf, 4096, L" -s", _TRUNCATE);

    if (IsUrl(input) && wcslen(username) > 0) {
        WCHAR qUser[258], qPass[258];
        QuotedArg(qUser, ARRAYSIZE(qUser), username);
        QuotedArg(qPass, ARRAYSIZE(qPass), wcslen(password) > 0 ? password : L"");

        WCHAR credBuf[600];
        _snwprintf_s(credBuf, ARRAYSIZE(credBuf), _TRUNCATE,
            L" -u %s -p %s", qUser, qPass);
        wcsncat_s(cmd_buf, 4096, credBuf, _TRUNCATE);
    }

    _snwprintf_s(pos_buf, MAX_PATH * 2 + 8, _TRUNCATE, L" %s %s", quoted_input, quoted_output);
    wcsncat_s(cmd_buf, 4096, pos_buf, _TRUNCATE);

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

    EnableWindow(hBtnGenerate, FALSE);
    SetWindowTextW(hBtnGenerate, L"Generating...");

    char buf[1024];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';

        WCHAR wbuf[2048];
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, ARRAYSIZE(wbuf));
        LogPrint(wbuf);

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    CloseHandle(hReadPipe);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    EnableWindow(hBtnGenerate, TRUE);
    SetWindowTextW(hBtnGenerate, L"Generate");

    if (exit_code == 0) {
        MessageBoxW(hWnd, L"Playlist generated successfully.", L"Done", MB_ICONINFORMATION | MB_OK);
    }
    else {
        WCHAR msg[128];
        _snwprintf_s(msg, ARRAYSIZE(msg), _TRUNCATE,
            L"d2m3u.exe exited with code %lu.\nCheck the log for details.", exit_code);
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

cleanup:
    free(input);
    free(output);
    free(username);
    free(password);
    free(bin_dir);
    free(bin_path);
    free(cmd_buf);
    free(quoted_input);
    free(quoted_output);
    free(quoted_bin);
    free(pos_buf);
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
        AppendMenuW(hFileMenu, MF_STRING, IDM_SHOWLOG, L"Show &Log");
        AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hFileMenu, MF_STRING, IDM_EXIT, L"E&xit");
        AppendMenuW(hHelp, MF_STRING, IDM_ABOUT, L"&About");
        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"&File");
        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hHelp, L"&Help");
        SetMenu(hWnd, hMenuBar);

        int x = 10, y = 10, w = 490, lh = 18, eh = 24, gap = 10;

		//input folder/url
        MakeLabel(hWnd, L"Input Folder / URL:", x, y, w, lh);
        y += lh + 2;
        hEdtInput = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            x, y, w - 40, eh,
            hWnd, (HMENU)EDT_INPUT, hInst, NULL);
        SendMessage(hEdtInput, WM_SETFONT, (WPARAM)hFont, TRUE);
        hBtnBrowse = CreateWindowW(L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x + w - 36, y, 30, eh,
            hWnd, (HMENU)BTN_BROWSE, hInst, NULL);
        SendMessage(hBtnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap;

        //username
        hLblUsername = MakeLabel(hWnd, L"Username:", x, y, w, lh);
        y += lh + 2;
        hEdtUsername = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            x, y, w - 7, eh,
            hWnd, (HMENU)EDT_USERNAME, hInst, NULL);
        SendMessage(hEdtUsername, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap;

        //password
        hLblPassword = MakeLabel(hWnd, L"Password:", x, y, w, lh);
        y += lh + 2;
        hEdtPassword = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
            x, y, w - 7, eh,
            hWnd, (HMENU)EDT_PASSWORD, hInst, NULL);
        SendMessage(hEdtPassword, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap;

        //-e flag
        hChkEmbedAuth = CreateWindowW(L"BUTTON", L"Embed authentication",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            x, y, w, eh,
            hWnd, (HMENU)CHK_EMBEDAUTH, hInst, NULL);
        SendMessage(hChkEmbedAuth, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap;

		//output playlsit
        MakeLabel(hWnd, L"Output File:", x, y, w, lh);
        y += lh + 2;
        hEdtOutput = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            x, y, w - 40, eh,
            hWnd, (HMENU)EDT_OUTPUT, hInst, NULL);
        SendMessage(hEdtOutput, WM_SETFONT, (WPARAM)hFont, TRUE);
        hBtnSave = CreateWindowW(L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x + w - 36, y, 30, eh,
            hWnd, (HMENU)BTN_SAVE, hInst, NULL);
        SendMessage(hBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap;

        //-s flag
        hChkSplit = CreateWindowW(L"BUTTON", L"Split by folder",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            x, y, w, eh,
            hWnd, (HMENU)CHK_SPLITFOLDER, hInst, NULL);
        SendMessage(hChkSplit, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += eh + gap * 2;

		//run button
        int btnW = 120, btnH = 30;
        hBtnGenerate = CreateWindowW(L"BUTTON", L"Generate",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
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

        case IDM_SHOWLOG: {
            bLogVisible = !bLogVisible;
            ShowWindow(hWndLog, bLogVisible ? SW_SHOW : SW_HIDE);
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_STATE;
            mii.fState = bLogVisible ? MFS_CHECKED : MFS_UNCHECKED;
            SetMenuItemInfoW(hFileMenu, IDM_SHOWLOG, FALSE, &mii);
            break;
        }

        case IDM_ABOUT:
            MessageBoxW(hWnd, 
                L"d2m3u GUI for Windows\n"
                L"prod. fujimite\n\n"
                L"Generates an m3u playlist from a local or web directory.\n\n"
                L"This software uses curl, under the curl license.\n"
                L"This software uses libraries from the FFmpeg project under the LGPLv2.1"
                ,
                L"About", MB_OK);
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        break;

    case WM_DESTROY:
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
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowW(L"MainWindow", L"d2m3u",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        100, 100, 525, 400,
        NULL, NULL, hInst, NULL);

    ShowWindow(hWnd, nShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}