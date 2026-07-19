// Reproductor.cpp
// Shell del reproductor de video para Genius VideoCam Look 317
// Ventana Win32 nativa con UI premium. Video pendiente de integracion.

#include "Reproductor.h"
#include <gdiplus.h>
#include <iostream>
#include <cstring>
#pragma comment(lib, "gdiplus.lib")

// ---- Statics ---------------------------------------------------------------
HWND        Reproductor::s_hwnd        = nullptr;
HFONT       Reproductor::s_fuenteUI    = nullptr;
HFONT       Reproductor::s_fuenteEstado= nullptr;
bool        Reproductor::s_conectado   = false;
std::string Reproductor::s_estado      = "Desconectado";
BYTE*       Reproductor::s_frameBuffer = nullptr;
int         Reproductor::s_frameAncho  = 0;
int         Reproductor::s_frameAlto   = 0;
HBITMAP     Reproductor::s_hbmp        = nullptr;
PROCESS_INFORMATION Reproductor::s_procCamara = {};

// Tracking hover de botones
static RECT g_rcBtnConectar = {};
static RECT g_rcBtnDetener  = {};
static bool g_hoverConectar = false;
static bool g_hoverDetener  = false;

// ---- Publicos --------------------------------------------------------------
bool Reproductor::Iniciar(HINSTANCE hInstance)
{
    // Fuentes
    s_fuenteUI = CreateFontA(
        16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    s_fuenteEstado = CreateFontA(
        14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    // Registro de clase
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "GeniusReproductorClass";
    wc.hbrBackground = CreateSolidBrush(COLOR_BG_DARK);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    // Centrar en pantalla
    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    int x  = (cx - VENTANA_ANCHO) / 2;
    int y  = (cy - VENTANA_ALTO)  / 2;

    s_hwnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        "GeniusReproductorClass",
        "Genius VideoCam Look 317 - Reproductor",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        x, y,
        VENTANA_ANCHO, VENTANA_ALTO,
        nullptr, nullptr,
        hInstance, nullptr);

    if (!s_hwnd) return false;

    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);
    return true;
}

void Reproductor::Ejecutar()
{
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void Reproductor::EntregarFrame(const BYTE* pixeles, int ancho, int alto)
{
    // TODO: se llamara desde el modulo de captura cuando haya video
    if (s_frameBuffer) delete[] s_frameBuffer;
    s_frameAncho  = ancho;
    s_frameAlto   = alto;
    s_frameBuffer = new BYTE[ancho * alto * 3];
    memcpy(s_frameBuffer, pixeles, ancho * alto * 3);

    if (s_hwnd) {
        RECT rc;
        GetClientRect(s_hwnd, &rc);
        rc.bottom -= PANEL_CTRL_ALTO;
        InvalidateRect(s_hwnd, &rc, FALSE);
    }
}

bool Reproductor::LanzarCamara()
{
    // Ruta al ejecutable de encendido (junto al reproductor)
    char dir[MAX_PATH];
    GetModuleFileNameA(nullptr, dir, MAX_PATH);
    // Quitar nombre del exe actual y subir al padre encender-camara
    char* ultimo = strrchr(dir, '\\');
    if (ultimo) *ultimo = '\0'; // ahora dir = carpeta del reproductor

    // Subir un nivel (de reproductor/ a driver/) y bajar a encender-camara/
    char cmdLine[MAX_PATH];
    snprintf(cmdLine, MAX_PATH, "%s\\..\\encender-camara\\encender_camara.exe", dir);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // oculta la ventana de consola

    ZeroMemory(&s_procCamara, sizeof(s_procCamara));
    if (!CreateProcessA(
            nullptr,
            cmdLine,
            nullptr, nullptr,
            FALSE,
            CREATE_NO_WINDOW, // sin consola visible
            nullptr, nullptr,
            &si, &s_procCamara))
    {
        DWORD err = GetLastError();
        char msg[256];
        snprintf(msg, sizeof(msg),
            "No se pudo lanzar encender_camara.exe\n"
            "Ruta intentada: %s\n"
            "Error: %lu", cmdLine, err);
        MessageBoxA(s_hwnd, msg, "Error al encender camara", MB_ICONERROR);
        return false;
    }
    return true;
}

void Reproductor::DetenerCamara()
{
    if (s_procCamara.hProcess) {
        // Enviar tecla al proceso para que cierre limpiamente
        // (encender_camara.exe hace system("pause") = espera tecla)
        // Lo mas seguro: terminar el proceso directamente
        TerminateProcess(s_procCamara.hProcess, 0);
        WaitForSingleObject(s_procCamara.hProcess, 2000);
        CloseHandle(s_procCamara.hProcess);
        CloseHandle(s_procCamara.hThread);
        ZeroMemory(&s_procCamara, sizeof(s_procCamara));
    }
}

// ---- Window Proc -----------------------------------------------------------
LRESULT CALLBACK Reproductor::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        // Timer para animacion de estado (pulso del indicador)
        SetTimer(hwnd, ID_TIMER_REPRO, 800, nullptr);
        return 0;

    case WM_TIMER:
        if (wp == ID_TIMER_REPRO) {
            // Repintar solo el panel de controles (para el parpadeo de estado)
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.top = rc.bottom - PANEL_CTRL_ALTO;
            InvalidateRect(hwnd, &rc, FALSE);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Double buffer
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        HDC     hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm    = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HBITMAP hOld   = (HBITMAP)SelectObject(hdcMem, hbm);

        DibujarUI(hwnd, hdcMem);

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { LOWORD(lp), HIWORD(lp) };
        bool hc = PtInRect(&g_rcBtnConectar, pt);
        bool hd = PtInRect(&g_rcBtnDetener,  pt);
        if (hc != g_hoverConectar || hd != g_hoverDetener) {
            g_hoverConectar = hc;
            g_hoverDetener  = hd;
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.top = rc.bottom - PANEL_CTRL_ALTO;
            InvalidateRect(hwnd, &rc, FALSE);

            // Pedir actualizaciones de hover
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_hoverConectar = g_hoverDetener = false;
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.top = rc.bottom - PANEL_CTRL_ALTO;
            InvalidateRect(hwnd, &rc, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lp), HIWORD(lp) };

        if (PtInRect(&g_rcBtnConectar, pt) && !s_conectado) {
            if (LanzarCamara()) {
                s_conectado = true;
                s_estado    = "Conectado  |  Camara encendida";
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        else if (PtInRect(&g_rcBtnDetener, pt) && s_conectado) {
            DetenerCamara();
            s_conectado = false;
            s_estado    = "Desconectado";
            if (s_frameBuffer) {
                delete[] s_frameBuffer;
                s_frameBuffer = nullptr;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Evita flicker

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_REPRO);
        DetenerCamara(); // Apagar camara si esta corriendo
        if (s_fuenteUI)     { DeleteObject(s_fuenteUI);     s_fuenteUI     = nullptr; }
        if (s_fuenteEstado) { DeleteObject(s_fuenteEstado); s_fuenteEstado = nullptr; }
        if (s_frameBuffer)  { delete[] s_frameBuffer;       s_frameBuffer  = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ---- Dibujo ----------------------------------------------------------------
void Reproductor::DibujarUI(HWND hwnd, HDC hdc)
{
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    // Fondo general
    HBRUSH hbr = CreateSolidBrush(COLOR_BG_DARK);
    FillRect(hdc, &rcClient, hbr);
    DeleteObject(hbr);

    RECT rcVideo = rcClient;
    rcVideo.bottom -= PANEL_CTRL_ALTO;

    RECT rcPanel = rcClient;
    rcPanel.top = rcVideo.bottom;

    DibujarVideoArea(hwnd, hdc, rcVideo);
    DibujarPanelControles(hwnd, hdc, rcPanel);
}

void Reproductor::DibujarVideoArea(HWND hwnd, HDC hdc, const RECT& rc)
{
    // Fondo del area de video
    HBRUSH hbr = CreateSolidBrush(RGB(8, 8, 12));
    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);

    if (s_frameBuffer && s_frameAncho > 0 && s_frameAlto > 0) {
        // === Mostrar frame real cuando llegue video ===
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth       = s_frameAncho;
        bmi.bmiHeader.biHeight      = -s_frameAlto; // top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        int dstW = rc.right  - rc.left;
        int dstH = rc.bottom - rc.top;

        StretchDIBits(hdc,
            rc.left, rc.top, dstW, dstH,
            0, 0, s_frameAncho, s_frameAlto,
            s_frameBuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        // === Pantalla de espera elegante ===
        int cx = (rc.left + rc.right)  / 2;
        int cy = (rc.top  + rc.bottom) / 2;

        // Cruz de mira / icono camara
        HPEN hpen = CreatePen(PS_SOLID, 1, RGB(50, 50, 70));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hpen);

        // Lineas guia tenues
        MoveToEx(hdc, cx, rc.top, nullptr);
        LineTo(hdc, cx, rc.bottom);
        MoveToEx(hdc, rc.left, cy, nullptr);
        LineTo(hdc, rc.right, cy);

        // Circulo central
        DeleteObject(SelectObject(hdc, hpen));
        HPEN hpen2 = CreatePen(PS_SOLID, 2, RGB(70, 130, 200));
        SelectObject(hdc, hpen2);
        int r = 40;
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);

        // Icono de camara (triangulo play simplificado)
        HBRUSH hbrIcon = CreateSolidBrush(RGB(70, 130, 200));
        HBRUSH hOldBr  = (HBRUSH)SelectObject(hdc, hbrIcon);
        POINT tri[] = {
            { cx - 10, cy - 14 },
            { cx + 16, cy },
            { cx - 10, cy + 14 }
        };
        Polygon(hdc, tri, 3);
        SelectObject(hdc, hOldBr);
        DeleteObject(hbrIcon);

        SelectObject(hdc, hOldPen);
        DeleteObject(hpen);
        DeleteObject(hpen2);

        // Texto de espera
        SetBkMode(hdc, TRANSPARENT);
        HFONT hOldFont = (HFONT)SelectObject(hdc, s_fuenteEstado);

        const char* linea1 = "Sin seal de video";
        const char* linea2 = "Usa [Conectar] para iniciar la camara";

        SetTextColor(hdc, RGB(80, 90, 120));
        RECT rcTxt = { rc.left, cy + 55, rc.right, cy + 80 };
        DrawTextA(hdc, linea1, -1, &rcTxt, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        rcTxt.top += 22; rcTxt.bottom += 22;
        DrawTextA(hdc, linea2, -1, &rcTxt, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        SelectObject(hdc, hOldFont);
    }

    // Borde del area de video
    HPEN hpenBorder = CreatePen(PS_SOLID, 1, RGB(40, 40, 60));
    HPEN hOldPen    = (HPEN)SelectObject(hdc, hpenBorder);
    HBRUSH hNoBr    = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hOldBr   = (HBRUSH)SelectObject(hdc, hNoBr);
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBr);
    DeleteObject(hpenBorder);
}

void Reproductor::DibujarPanelControles(HWND hwnd, HDC hdc, const RECT& rc)
{
    // Fondo del panel
    HBRUSH hbr = CreateSolidBrush(COLOR_BG_PANEL);
    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);

    // Linea separadora superior con acento
    HPEN hpen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
    HPEN hOld = (HPEN)SelectObject(hdc, hpen);
    MoveToEx(hdc, rc.left, rc.top, nullptr);
    LineTo(hdc, rc.right, rc.top);
    SelectObject(hdc, hOld);
    DeleteObject(hpen);

    int margen  = 20;
    int btnY    = rc.top + (PANEL_CTRL_ALTO - BTN_ALTO) / 2;

    // Boton Conectar
    g_rcBtnConectar = { rc.left + margen, btnY,
                        rc.left + margen + BTN_ANCHO, btnY + BTN_ALTO };
    DibujarBoton(hdc, g_rcBtnConectar, "Conectar", !s_conectado, g_hoverConectar);

    // Boton Detener
    g_rcBtnDetener = { g_rcBtnConectar.right + 12, btnY,
                       g_rcBtnConectar.right + 12 + BTN_ANCHO, btnY + BTN_ALTO };
    DibujarBoton(hdc, g_rcBtnDetener, "Detener", s_conectado, g_hoverDetener);

    // Indicador de estado (circulo pulsante)
    static bool pulseBit = false;
    pulseBit = !pulseBit;

    int dotX = g_rcBtnDetener.right + 24;
    int dotY = btnY + BTN_ALTO / 2;
    int dotR = 7;
    COLORREF dotColor = s_conectado
        ? (pulseBit ? COLOR_STATUS_OK : RGB(40, 140, 70))
        : RGB(70, 70, 80);

    HBRUSH hbrDot  = CreateSolidBrush(dotColor);
    HPEN   hpenDot = CreatePen(PS_SOLID, 0, dotColor);
    HBRUSH hOldBr  = (HBRUSH)SelectObject(hdc, hbrDot);
    HPEN   hOldPen = (HPEN)SelectObject(hdc, hpenDot);
    Ellipse(hdc, dotX - dotR, dotY - dotR, dotX + dotR, dotY + dotR);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hbrDot);
    DeleteObject(hpenDot);

    // Texto de estado
    SetBkMode(hdc, TRANSPARENT);
    HFONT hOldFont = (HFONT)SelectObject(hdc, s_fuenteEstado);
    SetTextColor(hdc, COLOR_TEXT);
    RECT rcTxt = { dotX + dotR + 10, btnY, rc.right - margen, btnY + BTN_ALTO };
    DrawTextA(hdc, s_estado.c_str(), -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Version / info
    SetTextColor(hdc, RGB(60, 65, 85));
    RECT rcVer = { rc.left, rc.top + 2, rc.right - 8, rc.bottom };
    DrawTextA(hdc, "Genius VideoCam Look 317  |  SN9C103 + OV7630", -1, &rcVer,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldFont);
}

void Reproductor::DibujarBoton(HDC hdc, const RECT& rc,
                                const char* texto, bool activo, bool hover)
{
    COLORREF bg;
    if (!activo) {
        bg = RGB(35, 35, 45); // deshabilitado
    } else if (hover) {
        bg = COLOR_BTN_HOVER;
    } else {
        bg = COLOR_BTN;
    }

    // Fondo con bordes redondeados (usando RoundRect)
    HBRUSH hbr = CreateSolidBrush(bg);
    HPEN   hp  = CreatePen(PS_SOLID, 1,
                     activo ? (hover ? COLOR_ACCENT : RGB(60, 100, 160)) : RGB(40, 40, 50));
    HBRUSH hOldBr  = (HBRUSH)SelectObject(hdc, hbr);
    HPEN   hOldPen = (HPEN)SelectObject(hdc, hp);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hbr);
    DeleteObject(hp);

    // Texto del boton
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, activo ? (hover ? RGB(255,255,255) : COLOR_TEXT) : RGB(70, 70, 85));
    RECT rcTxt = rc;
    DrawTextA(hdc, texto, -1, &rcTxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
