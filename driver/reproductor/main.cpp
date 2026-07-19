// main.cpp
// Punto de entrada del Reproductor de video
// Genius VideoCam Look 317

#include <windows.h>
#include "Reproductor.h"

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    if (!Reproductor::Iniciar(hInstance)) {
        MessageBoxA(nullptr,
            "No se pudo crear la ventana del reproductor.",
            "Error", MB_ICONERROR);
        return 1;
    }

    Reproductor::Ejecutar();
    return 0;
}
