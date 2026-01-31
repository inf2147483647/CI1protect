#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <string>
#include <vector>

// 添加DPI相关头文件
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")

#pragma comment(lib, "shell32.lib")

// 全局变量
HINSTANCE g_hInstance = NULL;
NOTIFYICONDATA g_nid = {};
HWND g_hWnd = NULL;
HANDLE g_hMutex = NULL;
const TCHAR* g_szAppName = TEXT("ClassIsland Monitor");
const TCHAR* g_szWindowClass = TEXT("ClassIslandMonitorClass");
// 【修改核心】：防多开互斥体标识名改为 CI1protect
const TCHAR* g_szMutexName = TEXT("Global\\CI1protect");

// 监控的进程改为 classisland.exe（不区分大小写，原有逻辑已支持）
const TCHAR* g_szMonitorProcess = TEXT("classisland.exe");
// 移除原有的固定绝对路径，改为动态获取同目录路径

// 要显示的窗口文本
const TCHAR* g_szWindowText = TEXT("复兴汉服，与子偕行！");
// 字体名称
const TCHAR* g_szFontName = TEXT("HarmonyOS Sans SC");
// 字号
const int g_nFontSize = 50;

// 函数声明
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
BOOL CreateTrayIcon(HWND hWnd);
void RemoveTrayIcon();
BOOL IsProcessRunning(const TCHAR* processName);
BOOL StartTargetProcess();
BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam);
DWORD WINAPI MonitorThread(LPVOID lpParam);
void ExitProgram();
// 新增函数声明：获取当前程序同目录下的 classisland.exe 完整路径
BOOL GetTargetProcessPath(TCHAR* szOutPath, DWORD dwOutPathSize);

// 设置DPI感知
void SetDPIAwareness()
{
    // 尝试使用每监视器DPI感知
    HMODULE hShcore = LoadLibrary(TEXT("Shcore.dll"));
    if (hShcore)
    {
        typedef HRESULT(WINAPI* SetProcessDpiAwarenessProc)(PROCESS_DPI_AWARENESS);
        SetProcessDpiAwarenessProc pSetProcessDpiAwareness =
            (SetProcessDpiAwarenessProc)GetProcAddress(hShcore, "SetProcessDpiAwareness");

        if (pSetProcessDpiAwareness)
        {
            // 设置为每监视器DPI感知
            pSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
        }
        FreeLibrary(hShcore);
    }
    else
    {
        // 回退到系统DPI感知
        SetProcessDPIAware();
    }
}

// 检查是否已有实例运行
BOOL CheckSingleInstance()
{
    g_hMutex = CreateMutex(NULL, FALSE, g_szMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // 如果已存在实例，激活它并退出
        HWND hExistingWnd = FindWindow(g_szWindowClass, g_szAppName);
        if (hExistingWnd)
        {
            ShowWindow(hExistingWnd, SW_RESTORE);
            SetForegroundWindow(hExistingWnd);
        }
        return FALSE;
    }
    return TRUE;
}

// 查找窗口的回调函数
BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
    DWORD dwProcessId;
    GetWindowThreadProcessId(hWnd, &dwProcessId);

    if (dwProcessId == (DWORD)lParam)
    {
        TCHAR szClassName[256];
        GetClassName(hWnd, szClassName, 256);

        // 如果窗口类名不是"ConsoleWindowClass"（排除控制台窗口）
        if (_tcsicmp(szClassName, TEXT("ConsoleWindowClass")) != 0)
        {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            return FALSE;
        }
    }
    return TRUE;
}

// 检查进程是否在运行
BOOL IsProcessRunning(const TCHAR* processName)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return FALSE;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe32))
    {
        CloseHandle(hSnapshot);
        return FALSE;
    }

    BOOL bFound = FALSE;
    do
    {
        if (_tcsicmp(pe32.szExeFile, processName) == 0)
        {
            bFound = TRUE;
            break;
        }
    } while (Process32Next(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    return bFound;
}

// 新增函数：获取当前程序同目录下的 classisland.exe 完整路径
// szOutPath：输出缓冲区，用于存放拼接后的完整路径
// dwOutPathSize：输出缓冲区大小（建议至少 MAX_PATH）
BOOL GetTargetProcessPath(TCHAR* szOutPath, DWORD dwOutPathSize)
{
    if (szOutPath == NULL || dwOutPathSize == 0)
        return FALSE;

    // 1. 获取当前程序的完整路径（包含自身文件名）
    TCHAR szSelfPath[MAX_PATH] = { 0 };
    if (!GetModuleFileName(NULL, szSelfPath, MAX_PATH))
        return FALSE;

    // 2. 截取目录部分（去掉自身文件名，保留到最后一个反斜杠）
    TCHAR* pLastBackslash = _tcsrchr(szSelfPath, TEXT('\\'));
    if (pLastBackslash == NULL)
        return FALSE;

    // 3. 截断字符串，只保留目录
    *pLastBackslash = TEXT('\0');

    // 4. 拼接 classisland.exe，生成目标进程完整路径
    _sntprintf_s(szOutPath, dwOutPathSize, _TRUNCATE, TEXT("%s\\%s"), szSelfPath, TEXT("classisland.exe"));

    return TRUE;
}

// 启动目标进程（修改为使用动态获取的同目录路径）
BOOL StartTargetProcess()
{
    // 动态获取同目录下的 classisland.exe 路径
    TCHAR szTargetPath[MAX_PATH] = { 0 };
    if (!GetTargetProcessPath(szTargetPath, MAX_PATH))
    {
        MessageBox(NULL, TEXT("无法获取目标进程路径！"), TEXT("错误"), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    STARTUPINFO si = { sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi;

    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;

    // 创建启动命令行（包裹双引号，处理路径中的空格）
    TCHAR cmdLine[MAX_PATH] = { 0 };
    _tcscpy_s(cmdLine, MAX_PATH, TEXT("\""));
    _tcscat_s(cmdLine, MAX_PATH, szTargetPath);
    _tcscat_s(cmdLine, MAX_PATH, TEXT("\""));

    // 设置启动目录（从目标路径中截取目录部分）
    TCHAR startupDir[MAX_PATH] = { 0 };
    _tcscpy_s(startupDir, MAX_PATH, szTargetPath);
    TCHAR* lastBackslash = _tcsrchr(startupDir, TEXT('\\'));
    if (lastBackslash)
    {
        *lastBackslash = TEXT('\0');
    }

    // 使用动态获取的路径启动进程
    if (CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, startupDir, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        // 给进程一点时间启动，然后尝试将其窗口置前
        Sleep(1000);
        EnumWindows(EnumWindowsProc, (LPARAM)pi.dwProcessId);

        return TRUE;
    }
    else
    {
        // 如果启动失败，显示错误信息（包含动态获取的路径）
        TCHAR errorMsg[512] = { 0 };
        DWORD errorCode = GetLastError();
        _stprintf_s(errorMsg, TEXT("无法启动进程: %s\n错误代码: %d"), szTargetPath, errorCode);
        MessageBox(NULL, errorMsg, TEXT("错误"), MB_OK | MB_ICONERROR);
        return FALSE;
    }
}

// 监控线程（逻辑不变，仅监控的进程名已修改）
DWORD WINAPI MonitorThread(LPVOID lpParam)
{
    BOOL bFirstCheck = TRUE;

    while (TRUE)
    {
        // 等待5秒后开始检查
        if (bFirstCheck)
        {
            Sleep(5000);
            bFirstCheck = FALSE;
        }

        // 检查目标进程是否在运行
        if (!IsProcessRunning(g_szMonitorProcess))
        {
            // 如果进程不在运行，启动它
            if (!StartTargetProcess())
            {
                // 如果启动失败，尝试再启动一次
                Sleep(3000);
                StartTargetProcess();
            }
        }

        // 每30秒检查一次
        Sleep(30000);

        // 检查是否需要退出线程
        if (WaitForSingleObject((HANDLE)lpParam, 0) == WAIT_OBJECT_0)
            break;
    }

    return 0;
}

// 创建托盘图标
BOOL CreateTrayIcon(HWND hWnd)
{
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_USER + 1;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    _tcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(TCHAR), g_szAppName);

    return Shell_NotifyIcon(NIM_ADD, &g_nid);
}

// 移除托盘图标
void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

// 创建托盘菜单
void ShowTrayMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (hMenu)
    {
        InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, 1001, TEXT("退出"));

        SetForegroundWindow(hWnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
        DestroyMenu(hMenu);
    }
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateTrayIcon(hWnd);
        break;

        // 拦截WM_CLOSE消息（窗口关闭按钮触发）
    case WM_CLOSE:
        // 隐藏窗口，不销毁，保留托盘和监控线程
        ShowWindow(hWnd, SW_HIDE);
        // 返回0，阻止默认的DestroyWindow调用
        return 0;

        // WM_PAINT消息 - 绘制窗口文本
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps); // 开始绘制，获取设备上下文

        // 1. 获取窗口客户区大小（用于文本居中）
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // 2. 创建指定字体（HarmonyOS Sans SC，50号字）
        // LOGPIXELSY：获取屏幕垂直方向DPI，用于转换字号为设备无关单位
        int nFontHeight = -MulDiv(g_nFontSize, GetDeviceCaps(hdc, LOGPIXELSY), 36);
        HFONT hFont = CreateFont(
            nFontHeight,                // 字体高度（负数表示使用字号，正数表示像素高度）
            0,                          // 字体宽度（0表示自动适配高度）
            0,                          // 字符旋转角度
            0,                          // 字符倾斜角度
            FW_NORMAL,                  // 字体粗细（FW_NORMAL=常规）
            FALSE,                      // 是否斜体
            FALSE,                      // 是否下划线
            FALSE,                      // 是否删除线
            DEFAULT_CHARSET,            // 字符集（默认，支持中文）
            OUT_DEFAULT_PRECIS,         // 输出精度
            CLIP_DEFAULT_PRECIS,        // 裁剪精度
            DEFAULT_QUALITY,            // 输出质量
            DEFAULT_PITCH | FF_DONTCARE,// 字体间距和家族
            g_szFontName                // 字体名称
        );

        // 3. 选择字体到设备上下文，并保存旧字体
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        // 4. 设置文本颜色（可选，这里设为黑色，可改为RGB(255,0,0)等自定义颜色）
        SetTextColor(hdc, RGB(0, 0, 0));
        // 设置文本背景透明（可选，避免文本背后有白色矩形）
        SetBkMode(hdc, TRANSPARENT);

        // 5. 绘制文本（居中显示）
        DrawText(
            hdc,                // 设备上下文句柄
            g_szWindowText,     // 要显示的文本
            -1,                 // 文本长度（-1表示自动计算以\0结尾的字符串长度）
            &rcClient,          // 绘制区域（客户区整个矩形）
            DT_CENTER | DT_VCENTER | DT_SINGLELINE  // 绘制格式：水平居中+垂直居中+单行显示
        );

        // 6. 恢复旧字体，释放创建的字体资源
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);

        // 7. 结束绘制
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_USER + 1:
        if (lParam == WM_RBUTTONUP)
        {
            ShowTrayMenu(hWnd);
        }
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            // 双击托盘图标显示窗口（恢复隐藏的窗口）
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) // 退出菜单项
        {
            // 只有点击退出，才销毁窗口，触发真正退出
            DestroyWindow(hWnd);
        }
        break;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 退出程序
void ExitProgram()
{
    if (g_hWnd)
        DestroyWindow(g_hWnd);
}

// 主函数
int APIENTRY _tWinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR    lpCmdLine,
    int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 设置DPI感知
    SetDPIAwareness();

    // 检查是否已有实例运行
    if (!CheckSingleInstance())
        return 0;

    g_hInstance = hInstance;

    // 注册窗口类
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // 窗口背景色（白色）
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = g_szWindowClass;
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wcex))
        return 1;

    // 创建窗口（宽800，高400，避免文本显示不全）
    g_hWnd = CreateWindow(
        g_szWindowClass,
        g_szAppName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0,
        3600, 600,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd)
        return 1;

    // 注释掉这行：取消程序启动时显示窗口（核心修改）
    // ShowWindow(g_hWnd, nCmdShow);
    // UpdateWindow() 可保留，不影响隐藏状态，后续显示窗口时文本绘制更顺畅
    UpdateWindow(g_hWnd);

    // 创建退出事件，用于监控线程
    HANDLE hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // 创建监控线程
    HANDLE hMonitorThread = CreateThread(NULL, 0, MonitorThread, hExitEvent, 0, NULL);

    // 检查目标进程是否已在运行，如果没有则启动它
    if (!IsProcessRunning(g_szMonitorProcess))
    {
        StartTargetProcess();
    }

    // 主消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 设置退出事件，等待监控线程结束
    SetEvent(hExitEvent);
    WaitForSingleObject(hMonitorThread, 5000);

    // 清理资源
    CloseHandle(hMonitorThread);
    CloseHandle(hExitEvent);

    if (g_hMutex)
    {
        CloseHandle(g_hMutex);
        g_hMutex = NULL;
    }

    return (int)msg.wParam;
}