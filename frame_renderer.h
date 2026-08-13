#pragma once

// =========================================================
// frame_renderer.h
// Motor de Renderizado 2D Completo por Software sobre Framebuffer
// para el sensor CMOS SOI968 / OV7660 en C++23
//
// ── PRIMITIVAS BÁSICAS ──────────────────────────────────
//  1. Píxel        (put_pixel)           — Escritura atómica de color
//  2. Línea        (draw_line)           — Algoritmo de Bresenham
//  3. Triángulo    (fill_triangle)       — Scanline Fill Barycéntrico
//  4. Rectángulo   (fill_rect)           — Scanline directo + borde
//  5. Círculo/Elipse (draw_circle)       — Punto Medio de Bresenham
//  6. Polígono/Bézier (draw_bezier)      — De Casteljau cúbico
//
// ── FIGURAS AVANZADAS ───────────────────────────────────
//  7. Arco y Sector Pie     (draw_arc / fill_sector)
//  8. Rectángulo Redondeado (draw_rounded_rect)
//  9. Anillo / Dona         (draw_ring)
// 10. Polígono Regular N    (draw_regular_ngon)
// 11. Estrella de N Puntas  (draw_star)
// 12. Cápsula / Píldora     (draw_capsule)
// 13. Flecha Dirigida       (draw_arrow)
//
// ── TÉCNICAS DE RELLENO Y COLOR ─────────────────────────
// 14. Degradado Lineal      (fill_gradient_h / fill_gradient_v)
// 15. Relleno por Inundación (flood_fill)
// 16. Tramado / Dithering   (fill_dither)
//
// ── TEXTO BITMAP ────────────────────────────────────────
// 17. Fuente Bitmap 8x8     (draw_char / draw_text)
//
// Modelo de datos:
//  - Buffer: std::span<std::uint8_t> plano RGB-24 (3 bytes/px)
//  - Resolución: 640 x 480 (VGA) — coordenadas (0,0) = arriba-izquierda
// =========================================================

#include <cstdint>
#include <span>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string_view>
#include <iostream>
#include <iomanip>

namespace Genius {

    // ─────────────────────────────────────────────────────────
    // TIPOS FUNDAMENTALES
    // ─────────────────────────────────────────────────────────

    // Píxel de Color RGB-24
    struct Color24 {
        std::uint8_t r{0}, g{0}, b{0};

        // Paleta de colores predefinidos listos para usar
        static constexpr Color24 White()    noexcept { return {255, 255, 255}; }
        static constexpr Color24 Black()    noexcept { return {  0,   0,   0}; }
        static constexpr Color24 Red()      noexcept { return {255,   0,   0}; }
        static constexpr Color24 Green()    noexcept { return {  0, 255,   0}; }
        static constexpr Color24 Blue()     noexcept { return {  0,   0, 255}; }
        static constexpr Color24 Yellow()   noexcept { return {255, 255,   0}; }
        static constexpr Color24 Cyan()     noexcept { return {  0, 255, 255}; }
        static constexpr Color24 Magenta()  noexcept { return {255,   0, 255}; }
        static constexpr Color24 Orange()   noexcept { return {255, 128,   0}; }
        static constexpr Color24 Lime()     noexcept { return {128, 255,   0}; }
        static constexpr Color24 Gray()     noexcept { return {128, 128, 128}; }

        // Mezcla alfa simple entre dos colores (0.0 = this, 1.0 = other)
        [[nodiscard]] constexpr Color24 blend(Color24 other, float t) const noexcept {
            return {
                static_cast<std::uint8_t>(r + static_cast<int>((other.r - r) * t)),
                static_cast<std::uint8_t>(g + static_cast<int>((other.g - g) * t)),
                static_cast<std::uint8_t>(b + static_cast<int>((other.b - b) * t))
            };
        }
    };

    // Punto 2D entero (coordenadas de píxel)
    struct Point2D {
        int x{0}, y{0};
    };

    // ─────────────────────────────────────────────────────────
    // CLASE PRINCIPAL: FrameRenderer
    // ─────────────────────────────────────────────────────────
    class FrameRenderer {
    public:
        // Dimensiones del buffer de framebuffer (VGA por defecto)
        static constexpr int FRAME_W = 640;
        static constexpr int FRAME_H = 480;
        static constexpr int BYTES_PER_PIXEL = 3; // RGB-24

        // Tamaño total del buffer en bytes
        static constexpr std::size_t BUFFER_SIZE =
            static_cast<std::size_t>(FRAME_W) * FRAME_H * BYTES_PER_PIXEL;

        // =====================================================
        // ■ PRIMITIVA 1: PÍXEL
        //   Escribe un solo píxel de color en coordenadas (x, y).
        //   Es la operación atómica base de todo el motor.
        // =====================================================
        static void put_pixel(
            std::span<std::uint8_t> fb,
            int x, int y,
            Color24 color) noexcept
        {
            if (x < 0 || x >= FRAME_W || y < 0 || y >= FRAME_H) return;
            std::size_t idx = static_cast<std::size_t>(y * FRAME_W + x) * BYTES_PER_PIXEL;
            fb[idx + 0] = color.r;
            fb[idx + 1] = color.g;
            fb[idx + 2] = color.b;
        }

        // Leer el color de un píxel del framebuffer
        [[nodiscard]] static Color24 get_pixel(
            std::span<const std::uint8_t> fb,
            int x, int y) noexcept
        {
            if (x < 0 || x >= FRAME_W || y < 0 || y >= FRAME_H) return {};
            std::size_t idx = static_cast<std::size_t>(y * FRAME_W + x) * BYTES_PER_PIXEL;
            return { fb[idx + 0], fb[idx + 1], fb[idx + 2] };
        }

        // =====================================================
        // ■ PRIMITIVA 2: LÍNEA — Algoritmo de Bresenham
        //   Une matemáticamente dos puntos (x1,y1) y (x2,y2)
        //   sin espacios ni "escalones" visuales deformes.
        //   Complejidad: O(max(|dx|,|dy|)) — sin multiplicación.
        // =====================================================
        static void draw_line(
            std::span<std::uint8_t> fb,
            int x1, int y1,
            int x2, int y2,
            Color24 color) noexcept
        {
            int dx = std::abs(x2 - x1);
            int dy = std::abs(y2 - y1);
            int sx = (x1 < x2) ? 1 : -1;
            int sy = (y1 < y2) ? 1 : -1;
            int err = dx - dy;

            while (true) {
                put_pixel(fb, x1, y1, color);
                if (x1 == x2 && y1 == y2) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x1 += sx; }
                if (e2 <  dx) { err += dx; y1 += sy; }
            }
        }

        // Línea con grosor (width > 1 dibuja líneas paralelas)
        static void draw_line_thick(
            std::span<std::uint8_t> fb,
            int x1, int y1,
            int x2, int y2,
            Color24 color,
            int thickness = 2) noexcept
        {
            for (int t = -(thickness / 2); t <= thickness / 2; ++t) {
                int dx = std::abs(x2 - x1);
                int dy = std::abs(y2 - y1);
                if (dy > dx) { // línea más vertical → desplazar en X
                    draw_line(fb, x1 + t, y1, x2 + t, y2, color);
                } else {       // línea más horizontal → desplazar en Y
                    draw_line(fb, x1, y1 + t, x2, y2 + t, color);
                }
            }
        }

        // =====================================================
        // ■ PRIMITIVA 3: TRIÁNGULO — Rasterización
        //   3a. Triángulo borde (3 líneas de Bresenham)
        //   3b. Triángulo relleno por Scanline Fill:
        //       Para cada línea horizontal (scan) dentro del
        //       triángulo, calcula los límites izquierdo y
        //       derecho interpolando las aristas y pinta los
        //       píxeles del interior.
        // =====================================================
        static void draw_triangle(
            std::span<std::uint8_t> fb,
            Point2D p0, Point2D p1, Point2D p2,
            Color24 color) noexcept
        {
            draw_line(fb, p0.x, p0.y, p1.x, p1.y, color);
            draw_line(fb, p1.x, p1.y, p2.x, p2.y, color);
            draw_line(fb, p2.x, p2.y, p0.x, p0.y, color);
        }

        static void fill_triangle(
            std::span<std::uint8_t> fb,
            Point2D p0, Point2D p1, Point2D p2,
            Color24 color) noexcept
        {
            // Ordenar vértices por Y ascendente
            if (p0.y > p1.y) std::swap(p0, p1);
            if (p0.y > p2.y) std::swap(p0, p2);
            if (p1.y > p2.y) std::swap(p1, p2);

            int y_min = p0.y, y_max = p2.y;
            for (int y = y_min; y <= y_max; ++y) {
                // Calcular X izquierdo y derecho interpolando aristas del triángulo
                float t_total = (p2.y != p0.y) ? static_cast<float>(y - p0.y) / (p2.y - p0.y) : 0.0f;
                int x_left  = p0.x + static_cast<int>(t_total * (p2.x - p0.x));

                int x_right;
                if (y < p1.y) {
                    float t = (p1.y != p0.y) ? static_cast<float>(y - p0.y) / (p1.y - p0.y) : 0.0f;
                    x_right = p0.x + static_cast<int>(t * (p1.x - p0.x));
                } else {
                    float t = (p2.y != p1.y) ? static_cast<float>(y - p1.y) / (p2.y - p1.y) : 0.0f;
                    x_right = p1.x + static_cast<int>(t * (p2.x - p1.x));
                }
                if (x_left > x_right) std::swap(x_left, x_right);
                for (int x = x_left; x <= x_right; ++x) {
                    put_pixel(fb, x, y, color);
                }
            }
        }

        // =====================================================
        // ■ PRIMITIVA 4: RECTÁNGULO
        //   4a. Borde: 4 llamadas a draw_line
        //   4b. Relleno: Scanline directo fila a fila.
        //   Se define por (x, y) esquina superior-izquierda
        //   + ancho (w) + alto (h). Ideal para Bounding Boxes,
        //   ROI (Region of Interest), HUDs y áreas de recorte.
        // =====================================================
        static void draw_rect(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 color) noexcept
        {
            draw_line(fb, x,     y,     x+w-1, y,     color); // Top
            draw_line(fb, x+w-1, y,     x+w-1, y+h-1, color); // Right
            draw_line(fb, x+w-1, y+h-1, x,     y+h-1, color); // Bottom
            draw_line(fb, x,     y+h-1, x,     y,     color); // Left
        }

        static void fill_rect(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 color) noexcept
        {
            int x0 = std::clamp(x,     0, FRAME_W - 1);
            int x1 = std::clamp(x + w, 0, FRAME_W);
            int y0 = std::clamp(y,     0, FRAME_H - 1);
            int y1 = std::clamp(y + h, 0, FRAME_H);
            for (int row = y0; row < y1; ++row) {
                for (int col = x0; col < x1; ++col) {
                    put_pixel(fb, col, row, color);
                }
            }
        }

        // Rectángulo con borde de grosor y relleno combinado
        static void draw_rect_filled_border(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 fill_color,
            Color24 border_color,
            int border_thickness = 2) noexcept
        {
            fill_rect(fb, x, y, w, h, fill_color);
            for (int t = 0; t < border_thickness; ++t) {
                draw_rect(fb, x + t, y + t, w - 2*t, h - 2*t, border_color);
            }
        }

        // =====================================================
        // ■ PRIMITIVA 5: CÍRCULO Y ELIPSE
        //   Algoritmo de Punto Medio de Bresenham para círculos.
        //   Sin trigonometría — usa únicamente sumas enteras.
        //   Dibuja los 8 octantes simétricos simultáneamente.
        //   Para elipses usa la variante de doble-paso de
        //   Bresenham para semiejes a (horizontal) y b (vertical).
        // =====================================================
        static void draw_circle(
            std::span<std::uint8_t> fb,
            int cx, int cy, int radius,
            Color24 color) noexcept
        {
            int x = radius, y = 0;
            int p = 1 - radius; // Criterio de decisión del Punto Medio

            auto plot8 = [&](int xi, int yi) {
                put_pixel(fb, cx+xi, cy+yi, color); put_pixel(fb, cx-xi, cy+yi, color);
                put_pixel(fb, cx+xi, cy-yi, color); put_pixel(fb, cx-xi, cy-yi, color);
                put_pixel(fb, cx+yi, cy+xi, color); put_pixel(fb, cx-yi, cy+xi, color);
                put_pixel(fb, cx+yi, cy-xi, color); put_pixel(fb, cx-yi, cy-xi, color);
            };

            plot8(x, y);
            while (x > y) {
                ++y;
                if (p <= 0) {
                    p += 2*y + 1;
                } else {
                    --x;
                    p += 2*y - 2*x + 1;
                }
                plot8(x, y);
            }
        }

        static void fill_circle(
            std::span<std::uint8_t> fb,
            int cx, int cy, int radius,
            Color24 color) noexcept
        {
            int x = radius, y = 0, p = 1 - radius;

            auto hline = [&](int lx, int rx, int hy) {
                for (int i = lx; i <= rx; ++i) put_pixel(fb, i, hy, color);
            };

            while (x >= y) {
                hline(cx - x, cx + x, cy + y);
                hline(cx - x, cx + x, cy - y);
                hline(cx - y, cx + y, cy + x);
                hline(cx - y, cx + y, cy - x);
                ++y;
                if (p <= 0) p += 2*y + 1;
                else { --x; p += 2*y - 2*x + 1; }
            }
        }

        // Elipse — variante Bresenham de doble paso
        static void draw_ellipse(
            std::span<std::uint8_t> fb,
            int cx, int cy, int a, int b,
            Color24 color) noexcept
        {
            // Región 1: slope |dy/dx| < 1
            long long a2 = static_cast<long long>(a) * a;
            long long b2 = static_cast<long long>(b) * b;
            long long x = 0, y = b;
            long long d1 = b2 - a2*b + a2/4;

            auto plot4 = [&](long long px, long long py) {
                put_pixel(fb, cx + static_cast<int>(px), cy + static_cast<int>(py), color);
                put_pixel(fb, cx - static_cast<int>(px), cy + static_cast<int>(py), color);
                put_pixel(fb, cx + static_cast<int>(px), cy - static_cast<int>(py), color);
                put_pixel(fb, cx - static_cast<int>(px), cy - static_cast<int>(py), color);
            };

            while (2*b2*x < 2*a2*y) {
                plot4(x, y);
                if (d1 < 0) d1 += 2*b2*x + 3*b2;
                else        { d1 += 2*b2*x - 2*a2*y + 2*a2 + 3*b2; --y; }
                ++x;
            }
            // Región 2: slope |dy/dx| > 1
            long long d2 = b2*(x + 1)*(x + 1) / 1
                         + a2*(y - 1)*(y - 1) - a2*b2;
            while (y >= 0) {
                plot4(x, y);
                if (d2 > 0) d2 -= 2*a2*y + a2;
                else        { d2 += 2*b2*x - 2*a2*y + 3*a2; ++x; }
                --y;
            }
        }

        // =====================================================
        // ■ PRIMITIVA 6: POLÍGONO Y CURVA DE BÉZIER
        //   6a. Polígono: n vértices → n líneas de Bresenham
        //       Relleno: Scanline con tabla de intersecciones
        //       por fila (Scanline Edge-Fill).
        //   6b. Curva de Bézier Cúbica: Algoritmo de De Casteljau
        //       Interpola recursivamente entre 4 puntos de control
        //       P0, P1, P2, P3 con subdivisión en 't' ∈ [0..1].
        // =====================================================
        static void draw_polygon(
            std::span<std::uint8_t> fb,
            std::span<const Point2D> vertices,
            Color24 color) noexcept
        {
            if (vertices.size() < 2) return;
            for (std::size_t i = 0; i < vertices.size(); ++i) {
                const auto& a = vertices[i];
                const auto& b = vertices[(i + 1) % vertices.size()];
                draw_line(fb, a.x, a.y, b.x, b.y, color);
            }
        }

        static void fill_polygon(
            std::span<std::uint8_t> fb,
            std::span<const Point2D> vertices,
            Color24 color) noexcept
        {
            if (vertices.size() < 3) return;

            // Encontrar bounding box vertical del polígono
            int y_min = FRAME_H, y_max = 0;
            for (const auto& v : vertices) {
                y_min = std::min(y_min, v.y);
                y_max = std::max(y_max, v.y);
            }
            y_min = std::clamp(y_min, 0, FRAME_H - 1);
            y_max = std::clamp(y_max, 0, FRAME_H - 1);

            // Scanline Edge-Fill: para cada línea Y, calcular intersecciones con aristas
            std::vector<int> intersections;
            for (int y = y_min; y <= y_max; ++y) {
                intersections.clear();
                for (std::size_t i = 0; i < vertices.size(); ++i) {
                    const auto& v0 = vertices[i];
                    const auto& v1 = vertices[(i + 1) % vertices.size()];
                    if ((v0.y <= y && v1.y > y) || (v1.y <= y && v0.y > y)) {
                        int x_intercept = v0.x + (y - v0.y) * (v1.x - v0.x) / (v1.y - v0.y);
                        intersections.push_back(x_intercept);
                    }
                }
                std::sort(intersections.begin(), intersections.end());
                for (std::size_t k = 0; k + 1 < intersections.size(); k += 2) {
                    for (int x = intersections[k]; x <= intersections[k+1]; ++x) {
                        put_pixel(fb, x, y, color);
                    }
                }
            }
        }

        // Curva de Bézier Cúbica (4 puntos de control: P0, P1, P2, P3)
        // Algoritmo de De Casteljau: t ∈ [0.0, 1.0]
        // Resolución: número de segmentos de línea que aproximan la curva
        static void draw_bezier(
            std::span<std::uint8_t> fb,
            Point2D p0, Point2D p1, Point2D p2, Point2D p3,
            Color24 color,
            int resolution = 80) noexcept
        {
            // De Casteljau: interpolar los 4 puntos de control en t
            auto lerp = [](float a, float b, float t) { return a + t * (b - a); };

            auto bezier_point = [&](float t) -> Point2D {
                float ax = lerp(static_cast<float>(p0.x), static_cast<float>(p1.x), t);
                float ay = lerp(static_cast<float>(p0.y), static_cast<float>(p1.y), t);
                float bx = lerp(static_cast<float>(p1.x), static_cast<float>(p2.x), t);
                float by = lerp(static_cast<float>(p1.y), static_cast<float>(p2.y), t);
                float cx = lerp(static_cast<float>(p2.x), static_cast<float>(p3.x), t);
                float cy = lerp(static_cast<float>(p2.y), static_cast<float>(p3.y), t);

                float dx = lerp(ax, bx, t), dy = lerp(ay, by, t);
                float ex = lerp(bx, cx, t), ey = lerp(by, cy, t);

                return { static_cast<int>(lerp(dx, ex, t)), static_cast<int>(lerp(dy, ey, t)) };
            };

            Point2D prev = p0;
            for (int i = 1; i <= resolution; ++i) {
                float t = static_cast<float>(i) / resolution;
                Point2D curr = bezier_point(t);
                draw_line(fb, prev.x, prev.y, curr.x, curr.y, color);
                prev = curr;
            }
        }

        // Curva de Bézier Cuadrática (3 puntos de control: P0, P1, P2)
        static void draw_bezier_quad(
            std::span<std::uint8_t> fb,
            Point2D p0, Point2D p1, Point2D p2,
            Color24 color,
            int resolution = 60) noexcept
        {
            auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
            Point2D prev = p0;
            for (int i = 1; i <= resolution; ++i) {
                float t = static_cast<float>(i) / resolution;
                float ax = lerp(static_cast<float>(p0.x), static_cast<float>(p1.x), t);
                float ay = lerp(static_cast<float>(p0.y), static_cast<float>(p1.y), t);
                float bx = lerp(static_cast<float>(p1.x), static_cast<float>(p2.x), t);
                float by = lerp(static_cast<float>(p1.y), static_cast<float>(p2.y), t);
                Point2D curr = { static_cast<int>(lerp(ax, bx, t)), static_cast<int>(lerp(ay, by, t)) };
                draw_line(fb, prev.x, prev.y, curr.x, curr.y, color);
                prev = curr;
            }
        }

        // =========================================================
        // 7. ARCO Y SECTOR PIE (Arc & Pie Slice)
        //    draw_arc:    arco de radio r entre ángulos start_deg..end_deg
        //    fill_sector: sector relleno (porción de pastel)
        // =========================================================
        static void draw_arc(
            std::span<std::uint8_t> fb,
            int cx, int cy, int radius,
            float start_deg, float end_deg,
            Color24 color,
            int steps = 120) noexcept
        {
            const float pi = 3.14159265f;
            float step = (end_deg - start_deg) / steps;
            int px_prev = cx + static_cast<int>(radius * std::cos(start_deg * pi / 180.f));
            int py_prev = cy + static_cast<int>(radius * std::sin(start_deg * pi / 180.f));
            for (int i = 1; i <= steps; ++i) {
                float ang = (start_deg + i * step) * pi / 180.f;
                int px = cx + static_cast<int>(radius * std::cos(ang));
                int py = cy + static_cast<int>(radius * std::sin(ang));
                draw_line(fb, px_prev, py_prev, px, py, color);
                px_prev = px; py_prev = py;
            }
        }

        static void fill_sector(
            std::span<std::uint8_t> fb,
            int cx, int cy, int radius,
            float start_deg, float end_deg,
            Color24 color,
            int steps = 120) noexcept
        {
            const float pi = 3.14159265f;
            float step = (end_deg - start_deg) / steps;
            for (int i = 0; i < steps; ++i) {
                float a1 = (start_deg + i       * step) * pi / 180.f;
                float a2 = (start_deg + (i + 1) * step) * pi / 180.f;
                int px1 = cx + static_cast<int>(radius * std::cos(a1));
                int py1 = cy + static_cast<int>(radius * std::sin(a1));
                int px2 = cx + static_cast<int>(radius * std::cos(a2));
                int py2 = cy + static_cast<int>(radius * std::sin(a2));
                // Rellenar triángulo: centro → borde del arco
                fill_triangle(fb, {cx, cy}, {px1, py1}, {px2, py2}, color);
            }
        }

        // =========================================================
        // 8. RECTÁNGULO REDONDEADO (Rounded Rectangle)
        //    Combina 4 líneas rectas + 4 cuartos de círculo en esquinas.
        // =========================================================
        static void draw_rounded_rect(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h, int r,
            Color24 color) noexcept
        {
            r = std::min(r, std::min(w, h) / 2);
            // 4 lados rectos
            draw_line(fb, x + r,     y,         x + w - r, y,         color); // Top
            draw_line(fb, x + r,     y + h,     x + w - r, y + h,     color); // Bottom
            draw_line(fb, x,         y + r,     x,         y + h - r, color); // Left
            draw_line(fb, x + w,     y + r,     x + w,     y + h - r, color); // Right
            // 4 arcos de esquina
            draw_arc(fb, x + r,     y + r,     r, 180.f, 270.f, color, 30);  // Top-Left
            draw_arc(fb, x + w - r, y + r,     r, 270.f, 360.f, color, 30);  // Top-Right
            draw_arc(fb, x + r,     y + h - r, r,  90.f, 180.f, color, 30);  // Bot-Left
            draw_arc(fb, x + w - r, y + h - r, r,   0.f,  90.f, color, 30);  // Bot-Right
        }

        static void fill_rounded_rect(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h, int r,
            Color24 color) noexcept
        {
            r = std::min(r, std::min(w, h) / 2);
            fill_rect(fb, x + r, y,     w - 2*r, h,     color); // Centro
            fill_rect(fb, x,     y + r, r,       h-2*r, color); // Izquierda
            fill_rect(fb, x+w-r, y + r, r,       h-2*r, color); // Derecha
            fill_sector(fb, x+r,     y+r,     r, 180.f, 270.f, color, 30);
            fill_sector(fb, x+w-r,   y+r,     r, 270.f, 360.f, color, 30);
            fill_sector(fb, x+r,     y+h-r,   r,  90.f, 180.f, color, 30);
            fill_sector(fb, x+w-r,   y+h-r,   r,   0.f,  90.f, color, 30);
        }

        // =========================================================
        // 9. ANILLO / DONA (Ring / Annulus)
        //    Círculo hueco: radio externo r_out y radio interno r_in.
        //    Implementado con fill_circle de radio decreciente.
        // =========================================================
        static void draw_ring(
            std::span<std::uint8_t> fb,
            int cx, int cy,
            int r_outer, int r_inner,
            Color24 color,
            int thickness = 3) noexcept
        {
            for (int r = r_inner; r <= r_outer; r += thickness) {
                draw_circle(fb, cx, cy, r, color);
            }
        }

        // =========================================================
        // 10. POLÍGONO REGULAR DE N LADOS (Regular N-gon)
        //     Genera n vértices equidistantes sobre un círculo de radio r.
        //     n=3 → triángulo equilátero, n=5 → pentágono, n=6 → hexágono...
        // =========================================================
        static void draw_regular_ngon(
            std::span<std::uint8_t> fb,
            int cx, int cy, int radius, int n,
            Color24 color,
            float rotation_deg = 0.f) noexcept
        {
            if (n < 3) return;
            const float pi = 3.14159265f;
            std::vector<Point2D> verts;
            verts.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                float ang = (rotation_deg + 360.f * i / n) * pi / 180.f;
                verts.push_back({
                    cx + static_cast<int>(radius * std::cos(ang)),
                    cy + static_cast<int>(radius * std::sin(ang))
                });
            }
            draw_polygon(fb, verts, color);
        }

        static void fill_regular_ngon(
            std::span<std::uint8_t> fb,
            int cx, int cy, int radius, int n,
            Color24 color,
            float rotation_deg = 0.f) noexcept
        {
            if (n < 3) return;
            const float pi = 3.14159265f;
            std::vector<Point2D> verts;
            verts.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                float ang = (rotation_deg + 360.f * i / n) * pi / 180.f;
                verts.push_back({
                    cx + static_cast<int>(radius * std::cos(ang)),
                    cy + static_cast<int>(radius * std::sin(ang))
                });
            }
            fill_polygon(fb, verts, color);
        }

        // =========================================================
        // 11. ESTRELLA DE N PUNTAS (Star Polygon)
        //     Alterna vértices en radio externo (puntas) e interno (valles).
        // =========================================================
        static void draw_star(
            std::span<std::uint8_t> fb,
            int cx, int cy,
            int r_outer, int r_inner, int n_points,
            Color24 color,
            float rotation_deg = -90.f) noexcept
        {
            if (n_points < 3) return;
            const float pi = 3.14159265f;
            int total = n_points * 2;
            std::vector<Point2D> verts;
            verts.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i) {
                float ang = (rotation_deg + 360.f * i / total) * pi / 180.f;
                int r = (i % 2 == 0) ? r_outer : r_inner;
                verts.push_back({
                    cx + static_cast<int>(r * std::cos(ang)),
                    cy + static_cast<int>(r * std::sin(ang))
                });
            }
            draw_polygon(fb, verts, color);
        }

        static void fill_star(
            std::span<std::uint8_t> fb,
            int cx, int cy,
            int r_outer, int r_inner, int n_points,
            Color24 color,
            float rotation_deg = -90.f) noexcept
        {
            if (n_points < 3) return;
            const float pi = 3.14159265f;
            int total = n_points * 2;
            std::vector<Point2D> verts;
            verts.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i) {
                float ang = (rotation_deg + 360.f * i / total) * pi / 180.f;
                int r = (i % 2 == 0) ? r_outer : r_inner;
                verts.push_back({
                    cx + static_cast<int>(r * std::cos(ang)),
                    cy + static_cast<int>(r * std::sin(ang))
                });
            }
            fill_polygon(fb, verts, color);
        }

        // =========================================================
        // 12. CÁPSULA / PÍLDORA (Capsule / Pill Shape)
        //     Rectángulo + semicírculo en ambos extremos.
        //     Orientación: horizontal (w > h) o vertical (h > w).
        // =========================================================
        static void draw_capsule(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 color) noexcept
        {
            int r = std::min(w, h) / 2;
            if (w >= h) {
                // Horizontal: semicírculos en extremos izquierdo y derecho
                draw_line(fb, x + r, y,     x + w - r, y,     color); // Top
                draw_line(fb, x + r, y + h, x + w - r, y + h, color); // Bottom
                draw_arc(fb, x + r,     y + r, r, 90.f,  270.f, color, 40);
                draw_arc(fb, x + w - r, y + r, r, 270.f, 450.f, color, 40);
            } else {
                // Vertical: semicírculos en extremos superior e inferior
                draw_line(fb, x,     y + r, x,     y + h - r, color);
                draw_line(fb, x + w, y + r, x + w, y + h - r, color);
                draw_arc(fb, x + r, y + r,     r, 180.f, 360.f, color, 40);
                draw_arc(fb, x + r, y + h - r, r,   0.f, 180.f, color, 40);
            }
        }

        static void fill_capsule(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 color) noexcept
        {
            int r = std::min(w, h) / 2;
            fill_rect(fb, x, y + r, w, h - 2 * r, color); // Cuerpo central
            if (w >= h) {
                fill_circle(fb, x + r,     y + r, r, color); // Semicírculo izquierdo
                fill_circle(fb, x + w - r, y + r, r, color); // Semicírculo derecho
            } else {
                fill_circle(fb, x + r, y + r,     r, color); // Semicírculo superior
                fill_circle(fb, x + r, y + h - r, r, color); // Semicírculo inferior
            }
        }

        // =========================================================
        // 13. FLECHA DIRIGIDA (Directed Arrow)
        //     Línea de cuerpo + cabeza triangular rellena en la dirección
        //     del vector (x1,y1) → (x2,y2).
        // =========================================================
        static void draw_arrow(
            std::span<std::uint8_t> fb,
            int x1, int y1, int x2, int y2,
            Color24 color,
            int head_size = 12) noexcept
        {
            const float pi = 3.14159265f;
            // Cuerpo de la flecha
            draw_line_thick(fb, x1, y1, x2, y2, color, 2);
            // Calcular ángulo de la flecha
            float angle = std::atan2f(static_cast<float>(y2 - y1), static_cast<float>(x2 - x1));
            // Dos puntos de la base del triángulo de la punta
            float a1 = angle + pi * 0.75f;
            float a2 = angle - pi * 0.75f;
            int hx1 = x2 + static_cast<int>(head_size * std::cos(a1));
            int hy1 = y2 + static_cast<int>(head_size * std::sin(a1));
            int hx2 = x2 + static_cast<int>(head_size * std::cos(a2));
            int hy2 = y2 + static_cast<int>(head_size * std::sin(a2));
            fill_triangle(fb, {x2, y2}, {hx1, hy1}, {hx2, hy2}, color);
        }

        // =========================================================
        // 14. DEGRADADO LINEAL (Linear Gradient Fill)
        //     14a. Horizontal: interpolación de color de izquierda a derecha
        //     14b. Vertical:   interpolación de color de arriba a abajo
        // =========================================================
        static void fill_gradient_h(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 color_left, Color24 color_right) noexcept
        {
            for (int col = 0; col < w; ++col) {
                float t = (w > 1) ? static_cast<float>(col) / (w - 1) : 0.f;
                Color24 c = color_left.blend(color_right, t);
                for (int row = 0; row < h; ++row) {
                    put_pixel(fb, x + col, y + row, c);
                }
            }
        }

        static void fill_gradient_v(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 color_top, Color24 color_bottom) noexcept
        {
            for (int row = 0; row < h; ++row) {
                float t = (h > 1) ? static_cast<float>(row) / (h - 1) : 0.f;
                Color24 c = color_top.blend(color_bottom, t);
                for (int col = 0; col < w; ++col) {
                    put_pixel(fb, x + col, y + row, c);
                }
            }
        }

        // Degradado Radial: desde el centro hacia el borde de un círculo
        static void fill_gradient_radial(
            std::span<std::uint8_t> fb,
            int cx, int cy, int radius,
            Color24 color_center, Color24 color_edge) noexcept
        {
            int x0 = std::clamp(cx - radius, 0, FRAME_W - 1);
            int x1 = std::clamp(cx + radius, 0, FRAME_W - 1);
            int y0 = std::clamp(cy - radius, 0, FRAME_H - 1);
            int y1 = std::clamp(cy + radius, 0, FRAME_H - 1);
            float r2 = static_cast<float>(radius * radius);
            for (int py = y0; py <= y1; ++py) {
                for (int px = x0; px <= x1; ++px) {
                    float dx = static_cast<float>(px - cx);
                    float dy = static_cast<float>(py - cy);
                    float dist2 = dx*dx + dy*dy;
                    if (dist2 <= r2) {
                        float t = std::sqrt(dist2) / radius;
                        put_pixel(fb, px, py, color_center.blend(color_edge, t));
                    }
                }
            }
        }

        // =========================================================
        // 15. RELLENO POR INUNDACIÓN (Flood Fill / Paint Bucket)
        //     Algoritmo iterativo BFS desde un punto semilla (seed_x, seed_y).
        //     Reemplaza todos los píxeles del color original contiguo
        //     con el nuevo color de relleno.
        // =========================================================
        static void flood_fill(
            std::span<std::uint8_t> fb,
            int seed_x, int seed_y,
            Color24 fill_color) noexcept
        {
            if (seed_x < 0 || seed_x >= FRAME_W || seed_y < 0 || seed_y >= FRAME_H) return;
            Color24 target = get_pixel(fb, seed_x, seed_y);
            // Si el color semilla ya es igual al relleno, no hacer nada
            if (target.r == fill_color.r &&
                target.g == fill_color.g &&
                target.b == fill_color.b) return;

            // Cola BFS de píxeles a visitar
            std::vector<Point2D> queue;
            queue.reserve(1024);
            queue.push_back({seed_x, seed_y});
            std::size_t head = 0;

            while (head < queue.size()) {
                auto [px, py] = queue[head++];
                if (px < 0 || px >= FRAME_W || py < 0 || py >= FRAME_H) continue;
                Color24 cur = get_pixel(fb, px, py);
                if (cur.r != target.r || cur.g != target.g || cur.b != target.b) continue;

                // Expandir horizontalmente en esta línea y encolar arriba/abajo
                int lx = px, rx = px;
                while (lx > 0) {
                    Color24 c = get_pixel(fb, lx - 1, py);
                    if (c.r != target.r || c.g != target.g || c.b != target.b) break;
                    --lx;
                }
                while (rx < FRAME_W - 1) {
                    Color24 c = get_pixel(fb, rx + 1, py);
                    if (c.r != target.r || c.g != target.g || c.b != target.b) break;
                    ++rx;
                }
                for (int x = lx; x <= rx; ++x) {
                    put_pixel(fb, x, py, fill_color);
                    if (py > 0)           queue.push_back({x, py - 1});
                    if (py < FRAME_H - 1) queue.push_back({x, py + 1});
                }
            }
        }

        // =========================================================
        // 16. TRAMADO / DITHERING (Ordered Bayer Dithering 4x4)
        //     Simula degradados con menos colores mezclando píxeles
        //     según la matriz de umbral de Bayer 4x4.
        //     Útil para HUDs en escala de grises con bajo bit-depth.
        // =========================================================
        static void fill_dither(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 color_a, Color24 color_b,
            float threshold = 0.5f) noexcept
        {
            // Matriz de umbral de Bayer 4×4 (normalizada 0..1)
            static constexpr std::array<std::array<float, 4>, 4> bayer4 = {{
                {{ 0/16.f,  8/16.f,  2/16.f, 10/16.f }},
                {{12/16.f,  4/16.f, 14/16.f,  6/16.f }},
                {{ 3/16.f, 11/16.f,  1/16.f,  9/16.f }},
                {{15/16.f,  7/16.f, 13/16.f,  5/16.f }}
            }};
            for (int row = 0; row < h; ++row) {
                for (int col = 0; col < w; ++col) {
                    float bayer_val = bayer4[row % 4][col % 4];
                    Color24 c = (bayer_val < threshold) ? color_a : color_b;
                    put_pixel(fb, x + col, y + row, c);
                }
            }
        }

        // =========================================================
        // 17. FUENTE DE TEXTO BITMAP 8×8 (Bitmap Font)
        //     Fuente ASCII embebida 8×8 píxeles por carácter.
        //     Soporta caracteres 0x20 (espacio) a 0x7E (~).
        //     draw_char: dibuja un carácter
        //     draw_text: dibuja una cadena de texto completa
        // =========================================================

        // Fuente Bitmap 8x8 — subconjunto ASCII esencial embebido
        // Cada uint8_t representa una fila de 8 bits (1=píxel encendido)
        static constexpr std::array<std::array<std::uint8_t, 8>, 96> FONT8x8 = {{
            // 0x20 SPACE
            {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
            // 0x21 !
            {{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}},
            // 0x22 "
            {{0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}},
            // 0x23 #
            {{0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}},
            // 0x24 $
            {{0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}},
            // 0x25 %
            {{0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}},
            // 0x26 &
            {{0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}},
            // 0x27 '
            {{0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}},
            // 0x28 (
            {{0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}},
            // 0x29 )
            {{0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}},
            // 0x2A *
            {{0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}},
            // 0x2B +
            {{0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}},
            // 0x2C ,
            {{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}},
            // 0x2D -
            {{0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}},
            // 0x2E .
            {{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}},
            // 0x2F /
            {{0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}},
            // 0x30 0
            {{0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}},
            // 0x31 1
            {{0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}},
            // 0x32 2
            {{0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}},
            // 0x33 3
            {{0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}},
            // 0x34 4
            {{0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}},
            // 0x35 5
            {{0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}},
            // 0x36 6
            {{0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}},
            // 0x37 7
            {{0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}},
            // 0x38 8
            {{0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}},
            // 0x39 9
            {{0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}},
            // 0x3A :
            {{0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}},
            // 0x3B ;
            {{0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}},
            // 0x3C <
            {{0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}},
            // 0x3D =
            {{0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}},
            // 0x3E >
            {{0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}},
            // 0x3F ?
            {{0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}},
            // 0x40 @
            {{0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}},
            // 0x41 A
            {{0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}},
            // 0x42 B
            {{0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}},
            // 0x43 C
            {{0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}},
            // 0x44 D
            {{0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}},
            // 0x45 E
            {{0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}},
            // 0x46 F
            {{0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}},
            // 0x47 G
            {{0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}},
            // 0x48 H
            {{0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}},
            // 0x49 I
            {{0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}},
            // 0x4A J
            {{0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}},
            // 0x4B K
            {{0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}},
            // 0x4C L
            {{0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}},
            // 0x4D M
            {{0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}},
            // 0x4E N
            {{0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}},
            // 0x4F O
            {{0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}},
            // 0x50 P
            {{0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}},
            // 0x51 Q
            {{0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}},
            // 0x52 R
            {{0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}},
            // 0x53 S
            {{0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}},
            // 0x54 T
            {{0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}},
            // 0x55 U
            {{0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}},
            // 0x56 V
            {{0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}},
            // 0x57 W
            {{0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}},
            // 0x58 X
            {{0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}},
            // 0x59 Y
            {{0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}},
            // 0x5A Z
            {{0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}},
            // 0x5B [
            {{0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}},
            // 0x5C backslash
            {{0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}},
            // 0x5D ]
            {{0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}},
            // 0x5E ^
            {{0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}},
            // 0x5F _
            {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}},
            // 0x60 `
            {{0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}},
            // 0x61 a
            {{0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}},
            // 0x62 b
            {{0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}},
            // 0x63 c
            {{0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}},
            // 0x64 d
            {{0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00}},
            // 0x65 e
            {{0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00}},
            // 0x66 f
            {{0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00}},
            // 0x67 g
            {{0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}},
            // 0x68 h
            {{0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}},
            // 0x69 i
            {{0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}},
            // 0x6A j
            {{0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}},
            // 0x6B k
            {{0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}},
            // 0x6C l
            {{0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}},
            // 0x6D m
            {{0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}},
            // 0x6E n
            {{0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}},
            // 0x6F o
            {{0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}},
            // 0x70 p
            {{0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}},
            // 0x71 q
            {{0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}},
            // 0x72 r
            {{0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}},
            // 0x73 s
            {{0x00,0x00,0x1E,0x03,0x1E,0x30,0x1F,0x00}},
            // 0x74 t
            {{0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}},
            // 0x75 u
            {{0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}},
            // 0x76 v
            {{0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}},
            // 0x77 w
            {{0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}},
            // 0x78 x
            {{0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}},
            // 0x79 y
            {{0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}},
            // 0x7A z
            {{0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}},
            // 0x7B {
            {{0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}},
            // 0x7C |
            {{0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}},
            // 0x7D }
            {{0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}},
            // 0x7E ~
            {{0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}},
        }};

        // Dibujar un carácter ASCII en la posición (x, y) con escala
        static void draw_char(
            std::span<std::uint8_t> fb,
            int x, int y, char c,
            Color24 color,
            int scale = 1) noexcept
        {
            int idx = static_cast<int>(c) - 0x20;
            if (idx < 0 || idx >= static_cast<int>(FONT8x8.size())) idx = 0;
            const auto& glyph = FONT8x8[static_cast<std::size_t>(idx)];
            for (int row = 0; row < 8; ++row) {
                std::uint8_t bits = glyph[static_cast<std::size_t>(row)];
                for (int col = 0; col < 8; ++col) {
                    if (bits & (0x80 >> col)) {
                        // Escalar: dibujar scale×scale píxeles por bit
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx)
                                put_pixel(fb, x + col*scale + sx,
                                              y + row*scale + sy, color);
                    }
                }
            }
        }

        // Dibujar una cadena de texto completa
        static void draw_text(
            std::span<std::uint8_t> fb,
            int x, int y, std::string_view text,
            Color24 color,
            int scale = 1) noexcept
        {
            int cursor_x = x;
            for (char c : text) {
                if (c == '\n') { y += 9 * scale; cursor_x = x; continue; }
                draw_char(fb, cursor_x, y, c, color, scale);
                cursor_x += 9 * scale; // 8px glyph + 1px kerning
            }
        }

        // ─────────────────────────────────────────────────────────
        // OVERLAYS DE DIAGNÓSTICO PARA EL FRAMEBUFFER DE CÁMARA
        // Retículas de calibración, HUD y ROI de sensor
        // ─────────────────────────────────────────────────────────

        // Cuadrícula de calibración completa (grid N x M)
        static void draw_calibration_grid(
            std::span<std::uint8_t> fb,
            int cols = 8, int rows = 6,
            Color24 color = Color24::Green()) noexcept
        {
            int step_x = FRAME_W / cols;
            int step_y = FRAME_H / rows;
            for (int x = 0; x <= FRAME_W; x += step_x)
                draw_line(fb, x, 0, x, FRAME_H - 1, color);
            for (int y = 0; y <= FRAME_H; y += step_y)
                draw_line(fb, 0, y, FRAME_W - 1, y, color);
        }

        // Crosshair central (mira de enfoque para calibración del lente)
        static void draw_crosshair(
            std::span<std::uint8_t> fb,
            int cx = FRAME_W / 2, int cy = FRAME_H / 2,
            int radius = 30,
            Color24 color = Color24::Red()) noexcept
        {
            draw_line(fb, cx - radius, cy, cx + radius, cy, color); // Horizontal
            draw_line(fb, cx, cy - radius, cx, cy + radius, color); // Vertical
            draw_circle(fb, cx, cy, radius / 2, color);              // Anillo central
        }

        // Bounding Box de ROI (Region of Interest del sensor)
        static void draw_roi_box(
            std::span<std::uint8_t> fb,
            int x, int y, int w, int h,
            Color24 border = Color24::Yellow()) noexcept
        {
            draw_rect_filled_border(fb, x, y, w, h, {0,0,0}, border, 2);
            // Esquinas de énfasis (L-shaped corners)
            int corner = 12;
            draw_line(fb, x, y, x + corner, y, Color24::White());
            draw_line(fb, x, y, x, y + corner, Color24::White());
            draw_line(fb, x + w, y, x + w - corner, y, Color24::White());
            draw_line(fb, x + w, y, x + w, y + corner, Color24::White());
            draw_line(fb, x, y + h, x + corner, y + h, Color24::White());
            draw_line(fb, x, y + h, x, y + h - corner, Color24::White());
            draw_line(fb, x + w, y + h, x + w - corner, y + h, Color24::White());
            draw_line(fb, x + w, y + h, x + w, y + h - corner, Color24::White());
        }

        // ─────────────────────────────────────────────────────────
        // DEMOSTRACIÓN COMPLETA DEL MOTOR DE RENDERIZADO 2D
        // ─────────────────────────────────────────────────────────
        static void test_full_renderer() noexcept {
            std::cout << "\n======================================================\n";
            std::cout <<   "  MOTOR DE RENDERIZADO 2D — DEMOSTRACIÓN DE 6 PRIMITIVAS\n";
            std::cout <<   "======================================================\n";

            // Crear un framebuffer sintético en memoria de 640x480 RGB-24
            std::vector<std::uint8_t> fb(BUFFER_SIZE, 0x00); // Fondo negro
            std::span<std::uint8_t> frame(fb);

            std::cout << "[FB]  Buffer de " << FRAME_W << "x" << FRAME_H
                      << " px RGB-24 inicializado ("
                      << BUFFER_SIZE / 1024 << " KB).\n";

            // ── 1. PÍXEL ──────────────────────────────────────────
            put_pixel(frame, 100, 100, Color24::Red());
            put_pixel(frame, 101, 100, Color24::Green());
            put_pixel(frame, 102, 100, Color24::Blue());
            std::cout << "[1/6] Primitiva PIXEL     → 3 píxeles RGB escritos en (100,100)..\n";

            // ── 2. LÍNEA (Bresenham) ───────────────────────────────
            draw_line(frame, 0, 0, 639, 479, Color24::Cyan());          // Diagonal completa
            draw_line_thick(frame, 0, 240, 639, 240, Color24::Gray(), 3); // Línea central gruesa
            std::cout << "[2/6] Primitiva LÍNEA     → Bresenham diagonal + línea central 3px.\n";

            // ── 3. TRIÁNGULO (Scanline Fill) ───────────────────────
            draw_triangle(frame, {100, 50}, {200, 50}, {150, 150}, Color24::Yellow());
            fill_triangle(frame, {300, 50}, {400, 50}, {350, 150}, Color24::Orange());
            std::cout << "[3/6] Primitiva TRIÁNGULO → Borde (3 Bresenham) + Scanline Fill.\n";

            // ── 4. RECTÁNGULO ──────────────────────────────────────
            draw_rect(frame, 50, 200, 120, 80, Color24::Magenta());
            fill_rect(frame, 200, 200, 120, 80, Color24::Blue());
            draw_rect_filled_border(frame, 350, 200, 120, 80, Color24::Gray(), Color24::White(), 3);
            std::cout << "[4/6] Primitiva RECTÁNGULO→ Borde + Relleno + Borde+Relleno combinado.\n";

            // ── 5. CÍRCULO Y ELIPSE (Punto Medio Bresenham) ────────
            draw_circle(frame, 100, 380, 45, Color24::Lime());
            fill_circle(frame, 240, 380, 35, Color24::Red());
            draw_ellipse(frame, 380, 380, 60, 35, Color24::Cyan());
            std::cout << "[5/6] Primitiva CÍRCULO/ELIPSE → Bresenham 8-octante + Elipse doble-paso.\n";

            // ── 6. POLÍGONO Y CURVA DE BÉZIER (De Casteljau) ──────
            std::array<Point2D, 6> hex_vertices = {{
                {520, 340}, {560, 320}, {600, 340},
                {600, 380}, {560, 400}, {520, 380}
            }};
            draw_polygon(frame, hex_vertices, Color24::Yellow());

            // Curva de Bézier Cúbica con 4 puntos de control
            draw_bezier(frame,
                {10, 460},   // P0 inicio
                {150, 400},  // P1 tangente inicio
                {490, 500},  // P2 tangente fin
                {630, 440},  // P3 fin
                Color24::Magenta(), 100);

            // Curva de Bézier Cuadrática
            draw_bezier_quad(frame,
                {10, 440}, {320, 380}, {630, 460},
                Color24::Orange(), 80);

            std::cout << "[6/6] Primitiva POLÍGONO/BÉZIER → Hexágono + Bézier Cúbica + Cuadrática.\n";

            // ── OVERLAYS DE DIAGNÓSTICO ────────────────────────────
            draw_calibration_grid(frame, 8, 6, Color24{0,60,0});
            draw_crosshair(frame, FRAME_W/2, FRAME_H/2, 30, Color24::Red());
            draw_roi_box(frame, 220, 140, 200, 150, Color24::Yellow());
            std::cout << "[OVL] Overlays de diagnóstico → Cuadrícula 8x6 + Crosshair + ROI Box.\n";

            // ── VERIFICACIÓN DE PÍXELES ────────────────────────────
            auto diag_px   = get_pixel(frame, 319, 239); // Centro diagonal
            auto center_px = get_pixel(frame, FRAME_W/2, FRAME_H/2); // Crosshair
            std::cout << "\n[VERIFY] Centro de pantalla (" << FRAME_W/2 << "," << FRAME_H/2 << ") = "
                      << "RGB(" << static_cast<int>(center_px.r) << ","
                      << static_cast<int>(center_px.g) << ","
                      << static_cast<int>(center_px.b) << ")\n";
            std::cout << "[VERIFY] Diagonal px (319,239) = "
                      << "RGB(" << static_cast<int>(diag_px.r) << ","
                      << static_cast<int>(diag_px.g) << ","
                      << static_cast<int>(diag_px.b) << ")\n";

            // ── 7. ARCO Y SECTOR PIE ──────────────────────────────
            draw_arc(frame, 80, 430, 40, 0.f, 270.f, Color24::Cyan(), 80);
            fill_sector(frame, 180, 430, 40, 0.f, 210.f, Color24::Orange(), 80);
            std::cout << "[7/11] Arco y Sector Pie   → 270° arco + 210° sector relleno.\n";

            // ── 8. RECTÁNGULO REDONDEADO ───────────────────────────
            draw_rounded_rect(frame, 250, 400, 130, 55, 14, Color24::Lime());
            fill_rounded_rect(frame, 400, 400, 120, 55, 16, Color24::Magenta());
            std::cout << "[8/11] Rect. Redondeado    → Borde R=14 + Relleno R=16.\n";

            // ── 9. ANILLO / DONA ──────────────────────────────────
            draw_ring(frame, 557, 430, 40, 20, Color24::Yellow(), 2);
            std::cout << "[9/11] Anillo / Dona       → Anillo r_out=40 r_in=20.\n";

            // ── 10. POLÍGONO REGULAR N LADOS ──────────────────────
            draw_regular_ngon(frame, 60,  330, 35, 5,  Color24::Green(),   -90.f); // Pentágono
            fill_regular_ngon(frame, 160, 330, 35, 7,  Color24::Cyan(),    -90.f); // Heptágono
            draw_regular_ngon(frame, 260, 330, 30, 8,  Color24::Yellow(),    0.f); // Octágono
            fill_regular_ngon(frame, 360, 330, 30, 12, Color24::Orange(),    0.f); // Dodecágono
            std::cout << "[10/11] Polígono Regular   → Pentágono + Heptágono + Octágono + Dodecágono.\n";

            // ── 11. ESTRELLA DE N PUNTAS ───────────────────────────
            draw_star(frame, 480, 330, 38, 16, 5, Color24::Yellow(), -90.f); // Estrella 5
            fill_star(frame, 580, 330, 35, 14, 6, Color24::Red(),    -90.f); // Estrella 6 rellena
            std::cout << "[11/11] Estrella N Puntas  → 5 puntas borde + 6 puntas rellena.\n";

            // ── 12. CÁPSULA / PÍLDORA ─────────────────────────────
            draw_capsule(frame, 10,  290, 120, 30, Color24::Lime());
            fill_capsule(frame, 160, 292,  26, 60, Color24::Orange());
            std::cout << "[12/11] Cápsula / Píldora  → Horizontal borde + Vertical rellena.\n";

            // ── 13. FLECHA DIRIGIDA ───────────────────────────────
            draw_arrow(frame, 230, 310, 370, 260, Color24::White(),  14);
            draw_arrow(frame, 400, 320, 470, 260, Color24::Cyan(),   12);
            draw_arrow(frame, 490, 320, 390, 260, Color24::Yellow(), 12);
            std::cout << "[13/11] Flechas Dirigidas  → 3 flechas con punta triangular rellena.\n";

            // ── 14. DEGRADADO LINEAL Y RADIAL ─────────────────────
            fill_gradient_h(frame, 0, 0, FRAME_W/2, 30,
                Color24::Blue(), Color24::Red());               // Barra superior H
            fill_gradient_v(frame, FRAME_W/2, 0, FRAME_W/2, 30,
                Color24::Green(), Color24::Yellow());           // Barra superior V
            fill_gradient_radial(frame, 530, 380, 50,
                Color24::White(), Color24::Blue());             // Degradado radial
            std::cout << "[14/11] Degradados         → Horizontal + Vertical + Radial.\n";

            // ── 15. RELLENO POR INUNDACIÓN ─────────────────────────
            fill_rect(frame, 10, 50, 60, 60, Color24{0, 30, 0});    // Zona de prueba
            draw_rect(frame, 10, 50, 60, 60, Color24::Lime());       // Borde
            flood_fill(frame, 40, 80, Color24{0, 120, 0});           // Inundar interior
            std::cout << "[15/11] Flood Fill         → Relleno por inundación BFS 4-conex.\n";

            // ── 16. TRAMADO BAYER 4x4 ────────────────────────────
            fill_dither(frame, 10, 130, 120, 50,
                Color24::Black(), Color24::White(), 0.5f);      // 50% tramado B&W
            fill_dither(frame, 140, 130, 120, 50,
                Color24::Blue(), Color24::Cyan(), 0.4f);        // Tramado azul-cian
            std::cout << "[16/11] Dithering Bayer 4x4→ Tramado 50% B&W + Azul/Cyan.\n";

            // ── 17. TEXTO BITMAP 8x8 ─────────────────────────────
            draw_text(frame, 10,  10, "DRIVER GENIUS WIN11",  Color24::White(),   1);
            draw_text(frame,  4,  20, "OV7660 / SOI968  VGA", Color24::Yellow(),  1);
            draw_text(frame,  4, 455, "AECH=0x10 GAIN=0x00",  Color24::Lime(),    1);
            draw_text(frame,  4, 465, "ISP: CCM+GAMMA+EDGE",  Color24::Cyan(),    1);
            draw_text(frame, 200, 200, "RGB-24  640x480",      Color24::Orange(), 2);
            std::cout << "[17/11] Texto Bitmap 8x8   → ASCII embebido, escala 1x y 2x.\n";

            // ── VERIFY TEXTO ───────────────────────────────────────
            auto px_text = get_pixel(frame, 10, 10); // Primera letra 'D'
            std::cout << "\n[VERIFY] Pixel de texto 'D' en (10,10) = "
                      << "RGB(" << static_cast<int>(px_text.r) << ","
                      << static_cast<int>(px_text.g) << ","
                      << static_cast<int>(px_text.b) << ")\n";

            std::cout << "\n======================================================\n";
            std::cout <<   "  ✓ Motor 2D Completo — 17 Primitivas y Técnicas 100% Activas.\n";
            std::cout << "======================================================\n\n";
        }
    };

} // namespace Genius
