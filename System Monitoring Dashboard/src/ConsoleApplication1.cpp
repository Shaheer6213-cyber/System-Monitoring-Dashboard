#include <windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <vector>
#include <gdiplus.h> 
#include <sapi.h> 
#include <cmath>

// ============================================================================
// 1. AUTO-CONFIG PRAGMAS
// ============================================================================
// These directives tell the linker which libraries to include, 
// avoiding the need to manually add them in project settings.
#pragma comment(lib, "gdiplus.lib") // Required for GDI+ Graphics
#pragma comment(lib, "sapi.lib")    // Required for Speech API (Voice alerts)
#pragma comment(lib, "user32.lib")  // Standard Windows UI library
#pragma comment(lib, "gdi32.lib")   // Standard Windows Graphics library
#pragma comment(lib, "kernel32.lib")// Core Windows system functions
#pragma comment(lib, "ole32.lib")   // Required for COM initialization (SAPI)
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:WinMainCRTStartup")

using namespace Gdiplus;

#define ID_TIMER 1 // Unique identifier for our UI update timer

// ============================================================================
// 2. CONFIGURATION SETTINGS
// ============================================================================
const double CPU_LIMIT = 80.0;     // Percentage threshold for CPU alert
const double RAM_LIMIT = 75.0;     // Percentage threshold for RAM alert
const double DISK_LIMIT = 90.0;    // Percentage threshold for Disk alert
const int ALERT_COOLDOWN = 10000;  // Minimum time (ms) between voice alerts
const double SMOOTHING_FACTOR = 0.15; // Controls the "fluidity" of meter updates

// ============================================================================
// 3. DATA STRUCTURES
// ============================================================================
// Represents a single background element (Matrix binary or falling ember)
struct Particle {
    float x, y, size, speed;
    int opacity;
    int type; // 0=Circle, 1=Circle, 2=Binary Text
    std::wstring binaryVal;
    float drift; // Horizontal movement
};

// Represents a node in the "Neural Network" background animation
struct DataNode {
    float x, y;
    float targetX, targetY; // Destination for drifting motion
    float pulse; // Used for the size-throbbing effect
    int colorTheme; // Randomly assigned color (Cyan, Pink, or Yellow)
};

// Global containers for animation and system state
std::vector<Particle> bgParticles;
std::vector<DataNode> cyberNodes;
POINT mousePos = { 0, 0 };

struct SysData {
    double cpu, ram, disk, uptime;
    std::string status;
    std::string suggestion;
} currentData;

ULONG_PTR gdiplusToken;     // Handle for GDI+ engine
DWORD lastAlertTime = 0;    // Tracks the last time the voice alert spoke
float borderOffset = 0.0f;  // Global timer variable for marquees
float gridScroll = 0.0f;    // Controls background grid movement
float pulseVal = 0.0f;      // General pulse sine-wave accumulator

// ============================================================================
// 4. AUDIO SYSTEM (Professional Rhythmic Alert)
// ============================================================================
// This function triggers a professional beep sequence followed by text-to-speech
void Speak(std::wstring text) {
    // Professional rhythmic beep sequence (Cyber-alarm style)
    Beep(1200, 100);
    Sleep(60);
    Beep(1200, 100);

    // Initialize SAPI Voice
    ISpVoice* pVoice = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&pVoice))) {
        pVoice->SetVolume(100);
        // SPF_ASYNC allows the program to keep running while the computer speaks
        pVoice->Speak(text.c_str(), SPF_ASYNC, NULL);
        pVoice->Release();
    }
}

// ============================================================================
// 5. SYSTEM DATA LOGIC
// ============================================================================
// Calculates CPU usage by comparing System Idle Time vs Total Time
double getCPUUsage() {
    static FILETIME pI = { 0 }, pK = { 0 }, pU = { 0 };
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return 0.0;

    // Helper to convert FILETIME structure to 64-bit integer
    auto To64 = [](FILETIME ft) {
        ULARGE_INTEGER ul;
        ul.LowPart = ft.dwLowDateTime;
        ul.HighPart = ft.dwHighDateTime;
        return ul.QuadPart;
        };

    unsigned __int64 i_64 = To64(i), k_64 = To64(k), u_64 = To64(u);
    unsigned __int64 pi_64 = To64(pI), pk_64 = To64(pK), pu_64 = To64(pU);
    unsigned __int64 diffK = k_64 - pk_64;
    unsigned __int64 diffU = u_64 - pu_64;
    unsigned __int64 diffI = i_64 - pi_64;
    unsigned __int64 total = diffK + diffU;
    pI = i; pK = k; pU = u;

    if (total == 0) return 0.0;
    double rawUsage = (double)(total - diffI) * 100.0 / total;

    // Smooth the data so the meter doesn't flicker or jump too fast
    static double smoothedUsage = 0.0;
    smoothedUsage = smoothedUsage + (rawUsage - smoothedUsage) * SMOOTHING_FACTOR;
    return (smoothedUsage < 0) ? 0 : (smoothedUsage > 100 ? 100 : smoothedUsage);
}

// Retrieves current percentage of Physical RAM used
double getRAMUsage() {
    MEMORYSTATUSEX m; m.dwLength = sizeof(m); GlobalMemoryStatusEx(&m);
    return (double)m.dwMemoryLoad;
}

// Retrieves disk space usage specifically for the C: drive
double getDiskUsage() {
    ULARGE_INTEGER f, t, free;
    if (!GetDiskFreeSpaceExW(L"C:\\", &f, &t, &free)) return 0.0;
    if (t.QuadPart == 0) return 0.0;
    return (1.0 - (double)free.QuadPart / (double)t.QuadPart) * 100.0;
}

// Spawns the particles and background nodes once when the program starts
void InitParticles(int w, int h) {
    if (!bgParticles.empty()) return;
    for (int i = 0; i < 200; i++) {
        Particle p;
        p.x = (float)(rand() % w); p.y = (float)(rand() % h);
        p.size = (float)(rand() % 4 + 1);
        p.speed = (float)(rand() % 25 + 10) / 5.0f;
        p.opacity = rand() % 180 + 70;
        p.type = rand() % 3;
        p.binaryVal = (rand() % 2 == 0) ? L"0" : L"1";
        p.drift = (float)((rand() % 120) - 60) / 100.0f;
        bgParticles.push_back(p);
    }
    for (int i = 0; i < 30; i++) {
        DataNode dn;
        dn.x = (float)(rand() % w); dn.y = (float)(rand() % h);
        dn.targetX = (float)(rand() % w); dn.targetY = (float)(rand() % h);
        dn.pulse = (float)(rand() % 100) / 10.0f;
        dn.colorTheme = rand() % 3;
        cyberNodes.push_back(dn);
    }
}

// The core update loop: Fetches data and updates animation positions
void UpdateSystemData(HWND hWnd) {
    // 1. Fetch Real-time metrics
    currentData.cpu = getCPUUsage();
    currentData.ram = getRAMUsage();
    currentData.disk = getDiskUsage();
    currentData.uptime = (double)GetTickCount64() / 1000.0 / 60.0 / 60.0;

    // 2. Increment animation timers
    borderOffset += 2.0f;
    gridScroll += 1.2f;
    pulseVal += 0.08f;
    if (gridScroll > 100.0f) gridScroll = 0.0f;

    // 3. Process Background Animations
    RECT rc; GetClientRect(hWnd, &rc);
    int w = rc.right - rc.left; int h = rc.bottom - rc.top;

    for (auto& p : bgParticles) {
        p.y += p.speed; p.x += p.drift;
        if (p.y > h + 20) { p.y = -20.0f; p.x = (float)(rand() % w); }
    }

    for (auto& dn : cyberNodes) {
        dn.pulse += 0.15f;
        dn.x += (dn.targetX - dn.x) * 0.008f; // Easing motion toward target
        dn.y += (dn.targetY - dn.y) * 0.008f;
        if (abs(dn.x - dn.targetX) < 2.0f) dn.targetX = (float)(rand() % w);
        if (abs(dn.y - dn.targetY) < 2.0f) dn.targetY = (float)(rand() % h);
    }

    // 4. Threshold Logic & Speech Alerts
    DWORD curTime = GetTickCount();
    std::wstring speechText = L"";

    if (currentData.cpu > CPU_LIMIT) {
        currentData.status = "CRITICAL: CPU SATURATION DETECTED";
        currentData.suggestion = "TERMINATE HIGH-THREAD SUBPROCESSES AND CHECK THERMAL PERFORMANCE";
        speechText = L"System Alert. CPU load has exceeded safe operating parameters.";
    }
    else if (currentData.ram > RAM_LIMIT) {
        currentData.status = "CRITICAL: MEMORY EXHAUSTION";
        currentData.suggestion = "TRIGGER MEMORY CLEANUP OR INCREASE VIRTUAL MEMORY (PAGING FILE SIZE)";
        speechText = L"System Alert. Physical memory availability is critically low.";
    }
    else if (currentData.disk > DISK_LIMIT) {
        currentData.status = "CRITICAL: DISK SPACE DEPLETION";
        currentData.suggestion = "PERMANENTLY DELETE TEMP FILES OR ARCHIVE DATA TO CLOUD STORAGE";
        speechText = L"System Alert. Primary storage volume has reached maximum capacity.";
    }

    else {
        currentData.status = "SYSTEM STATE: OPTIMAL";
        currentData.suggestion = "STATUS: CORE STABILITY MAINTAINED. NO INTERVENTION REQUIRED.";
    }

    // Ensure we don't spam the voice alert constantly
    if (speechText != L"" && (curTime - lastAlertTime > ALERT_COOLDOWN)) {
        Speak(speechText);
        lastAlertTime = curTime;
    }

    // Force the window to repaint
    InvalidateRect(hWnd, NULL, FALSE);
}

// Handles all the GDI+ drawing commands
void PaintCyberGUI(HDC hdc, HWND hWnd) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    RECT rc; GetClientRect(hWnd, &rc);
    INT w = rc.right - rc.left; INT h = rc.bottom - rc.top;
    InitParticles(w, h);

    // 1. Draw Deep Blue Background
    SolidBrush baseBrush(Color(255, 5, 2, 15));
    g.FillRectangle(&baseBrush, 0, 0, (INT)w, (INT)h);

    // 2. Draw Scrolling Neon Grid
    Pen gridPen(Color(60, 255, 20, 147), 1.0f);
    float gridStep = 60.0f;
    for (float x = -gridScroll; x < w; x += gridStep) g.DrawLine(&gridPen, (REAL)x, 0.0f, (REAL)x, (REAL)h);
    for (float y = -gridScroll; y < h; y += gridStep) g.DrawLine(&gridPen, 0.0f, (REAL)y, (REAL)w, (REAL)y);

    // 3. Draw Network Connections (Cyber Nodes)
    for (size_t i = 0; i < cyberNodes.size(); i++) {
        for (size_t j = i + 1; j < cyberNodes.size(); j++) {
            float dist = sqrt(pow(cyberNodes[i].x - cyberNodes[j].x, 2) + pow(cyberNodes[i].y - cyberNodes[j].y, 2));
            if (dist < 200.0f) { // Only connect if nodes are close
                Color connColor;
                if (cyberNodes[i].colorTheme == 1) connColor = Color((int)(60.0f * (1.0f - dist / 200.0f)), 255, 20, 147);
                else if (cyberNodes[i].colorTheme == 2) connColor = Color((int)(60.0f * (1.0f - dist / 200.0f)), 255, 255, 0);
                else connColor = Color((int)(60.0f * (1.0f - dist / 200.0f)), 0, 255, 255);
                Pen connectionPen(connColor, 1.2f);
                g.DrawLine(&connectionPen, cyberNodes[i].x, cyberNodes[i].y, cyberNodes[j].x, cyberNodes[j].y);
            }
        }
        float pSize = 5.0f + sin(cyberNodes[i].pulse) * 3.0f;
        Color nodeColor;
        if (cyberNodes[i].colorTheme == 1) nodeColor = Color(220, 255, 20, 147);
        else if (cyberNodes[i].colorTheme == 2) nodeColor = Color(220, 255, 255, 0);
        else nodeColor = Color(220, 0, 255, 255);
        SolidBrush nodeB(nodeColor);
        g.FillEllipse(&nodeB, cyberNodes[i].x - pSize / 2, cyberNodes[i].y - pSize / 2, pSize, pSize);
    }

    // 4. Draw Falling Particles & Binary Rain
    FontFamily monoFamily(L"Consolas");
    Gdiplus::Font binaryFont(&monoFamily, 11, FontStyleBold, UnitPixel);
    for (auto& p : bgParticles) {
        Color rainColor;
        if (p.x < w / 3) rainColor = Color(p.opacity, 255, 20, 147);
        else if (p.x < 2 * w / 3) rainColor = Color(p.opacity, 255, 255, 0);
        else rainColor = Color(p.opacity, 0, 200, 150);
        SolidBrush bBrush(rainColor);
        if (p.type == 2) g.DrawString(p.binaryVal.c_str(), -1, &binaryFont, PointF(p.x, p.y), &bBrush);
        else g.FillEllipse(&bBrush, (REAL)p.x, (REAL)p.y, (REAL)p.size, (REAL)p.size);
    }

    // 5. Draw UI Static Elements (Titles & Marquees)
    FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font fontSmall(&fontFamily, 14, FontStyleBold, UnitPixel);
    Gdiplus::Font fontLarge(&fontFamily, 24, FontStyleBold, UnitPixel);
    Gdiplus::Font fontTitle(&fontFamily, 40, FontStyleBold, UnitPixel);
    Gdiplus::Font fontMarquee(&fontFamily, 26, FontStyleBold, UnitPixel);
    Gdiplus::Font fontSugMarquee(&fontFamily, 22, FontStyleBold, UnitPixel);

    Color neonCyan(255, 0, 255, 255);
    Color neonPink(255, 255, 20, 147);
    Color neonYellow(255, 255, 255, 0);
    Color warningRed(255, 255, 0, 50);

    // --- TOP MARQUEE ---
    std::wstring creditText = L"The Timer Adjusted by: Abdullah Akif";
    RectF mRect; g.MeasureString(creditText.c_str(), -1, &fontMarquee, PointF(0, 0), &mRect);
    float xPosTop = fmod(borderOffset * 1.2f, (float)w + mRect.Width) - mRect.Width;
    SolidBrush yellowB(neonYellow);
    g.DrawString(creditText.c_str(), -1, &fontMarquee, PointF(xPosTop, 20.0f), &yellowB);

    StringFormat centerFmt; centerFmt.SetAlignment(StringAlignmentCenter);
    SolidBrush pinkB(neonPink);
    g.DrawString(L"SYSTEM MONITORING DASHBOARD", -1, &fontTitle, RectF(0.0f, 80.0f, (REAL)w, 60.0f), &centerFmt, &pinkB);

    // 6. Draw Progress Meter Cards (CPU, RAM, DISK, UPTIME)
    auto drawCard = [&](std::wstring label, double val, float y, Color theme, bool isTime = false) {
        RectF cardRect(120.0f, y, (REAL)w - 240.0f, 80.0f);
        SolidBrush cardBg(Color(190, 5, 5, 20));
        g.FillRectangle(&cardBg, cardRect);
        Pen bPen(Color(180, theme.GetR(), theme.GetG(), theme.GetB()), 1.5f);
        g.DrawRectangle(&bPen, cardRect);
        SolidBrush whiteB(Color(255, 240, 245, 255));
        g.DrawString(label.c_str(), -1, &fontSmall, PointF(cardRect.X + 25.0f, cardRect.Y + 15.0f), &whiteB);

        std::wstringstream valStr;
        if (isTime) valStr << std::fixed << std::setprecision(2) << val << L" HRS";
        else valStr << std::fixed << std::setprecision(1) << val << L"%";
        SolidBrush valBrush(neonCyan);
        g.DrawString(valStr.str().c_str(), -1, &fontLarge, PointF(cardRect.X + 25.0f, cardRect.Y + 38.0f), &valBrush);

        // Progress Bar inside card
        float bX = cardRect.X + 200.0f; float bW = cardRect.Width - 230.0f;
        g.DrawRectangle(&bPen, bX, cardRect.Y + 48.0f, bW, 16.0f);
        float fillP = (isTime) ? (float)(min(val, 24.0) / 24.0) : (float)(val / 100.0);
        if (val > 80.0 && !isTime) {
            SolidBrush critB(warningRed);
            g.FillRectangle(&critB, (REAL)(bX + 3), (REAL)(cardRect.Y + 51.0f), (REAL)((bW - 6) * fillP), 10.0f);
        }
        else {
            SolidBrush fillB(theme);
            g.FillRectangle(&fillB, (REAL)(bX + 3), (REAL)(cardRect.Y + 51.0f), (REAL)((bW - 6) * fillP), 10.0f);
        }
        };

    drawCard(L"CPU LOAD ANALYSIS", currentData.cpu, 170.0f, neonCyan);
    drawCard(L"PHYSICAL MEMORY STATUS {RAM}", currentData.ram, 262.0f, neonYellow);
    drawCard(L"STORAGE THROUGHPUT {DISK}", currentData.disk, 354.0f, neonPink);
    drawCard(L"CORE UPTIME METRIC", currentData.uptime, 446.0f, Color(255, 150, 50, 255), true);

    // 7. Status Message and Bottom Suggestions
    std::wstring stat(currentData.status.begin(), currentData.status.end());
    SolidBrush statusB(currentData.status.find("OPTIMAL") != std::string::npos ? neonCyan : warningRed);
    g.DrawString(stat.c_str(), -1, &fontLarge, RectF(0, (REAL)h - 130, (REAL)w, 40), &centerFmt, &statusB);

    std::wstring fullSugText = L"SUGGESTION: " + std::wstring(currentData.suggestion.begin(), currentData.suggestion.end());
    RectF sRect; g.MeasureString(fullSugText.c_str(), -1, &fontSugMarquee, PointF(0, 0), &sRect);
    float sxPosBottom = fmod(borderOffset * 1.2f, (float)w + sRect.Width) - sRect.Width;
    g.DrawString(fullSugText.c_str(), -1, &fontSugMarquee, PointF(sxPosBottom, (REAL)h - 85.0f), &yellowB);
}

// Handle Windows OS Messages (Timer, Paint, Destroy)
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        SetTimer(hWnd, ID_TIMER, 30, NULL); // Update every 30ms (~33 FPS)
        break;
    case WM_TIMER:
        UpdateSystemData(hWnd);
        break;
    case WM_PAINT: {
        // Double Buffering: Draw to a separate "device context" (mDC) to prevent flickering
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        HDC mDC = CreateCompatibleDC(hdc);
        RECT rc; GetClientRect(hWnd, &rc);
        HBITMAP mBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        SelectObject(mDC, mBM);

        PaintCyberGUI(mDC, hWnd); // Perform all drawing to the bitmap

        // Final "BitBlt" copy from memory back to the screen
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mDC, 0, 0, SRCCOPY);
        DeleteObject(mBM); DeleteDC(mDC);
        EndPaint(hWnd, &ps);
    } break;
    case WM_ERASEBKGND: return 1; // Prevent background erasing to stop flicker
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

// WinMain: Application Entry point
int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nC) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // Start COM for Voice API
    GdiplusStartupInput gsi; GdiplusStartup(&gdiplusToken, &gsi, NULL); // Start GDI+

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hI;
    wc.lpszClassName = L"CyberDashV13_Fix";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    // Create the Window (WS_EX_TOPMOST makes it float on top of other apps)
    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST, L"CyberDashV13_Fix", L"Cyber Security Monitoring Dashboard",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 780, NULL, NULL, hI, NULL);

    ShowWindow(hWnd, nC);
    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return 0;
}