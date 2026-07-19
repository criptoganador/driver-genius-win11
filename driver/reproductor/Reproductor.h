#pragma once
// Reproductor.h
// Ventana reproductora de video para Genius VideoCam Look 317
// Por ahora: solo UI. Sin video integrado aun.

#include <windows.h>
#include <string>

// Colores de la UI
#define COLOR_BG_DARK    RGB(18,  18,  22)
#define COLOR_BG_PANEL   RGB(28,  28,  36)
#define COLOR_ACCENT     RGB(80, 180, 255)
#define COLOR_BTN        RGB(45,  45,  60)
#define COLOR_BTN_HOVER  RGB(70, 150, 230)
#define COLOR_TEXT       RGB(220, 220, 230)
#define COLOR_STATUS_OK  RGB(80, 220, 120)
#define COLOR_STATUS_ERR RGB(240, 80,  80)

// IDs de controles
#define ID_BTN_CONECTAR   101
#define ID_BTN_DETENER    102
#define ID_TIMER_REPRO    201

// Dimensiones de la ventana
#define VENTANA_ANCHO     860
#define VENTANA_ALTO      600
#define PANEL_CTRL_ALTO   70
#define BTN_ANCHO         130
#define BTN_ALTO          36

class Reproductor {
public:
    static bool Iniciar(HINSTANCE hInstance);
    static void Ejecutar();

    // Llamar desde el hilo de video cuando llegue un frame nuevo
    // (Por ahora no hace nada, pero ya tiene el punto de conexion)
    static void EntregarFrame(const BYTE* pixeles, int ancho, int alto);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static void DibujarUI(HWND hwnd, HDC hdc);
    static void DibujarVideoArea(HWND hwnd, HDC hdc, const RECT& rc);
    static void DibujarPanelControles(HWND hwnd, HDC hdc, const RECT& rc);
    static void DibujarBoton(HDC hdc, const RECT& rc, const char* texto,
                             bool activo, bool hover);

    static HWND     s_hwnd;
    static HFONT    s_fuenteUI;
    static HFONT    s_fuenteEstado;
    static bool     s_conectado;
    static std::string s_estado;

    // Proceso hijo: encender_camara.exe
    static PROCESS_INFORMATION s_procCamara;
    static bool LanzarCamara();
    static void DetenerCamara();

    // Buffer del frame actual (cuando haya video)
    static BYTE*    s_frameBuffer;
    static int      s_frameAncho;
    static int      s_frameAlto;
    static HBITMAP  s_hbmp;
};
