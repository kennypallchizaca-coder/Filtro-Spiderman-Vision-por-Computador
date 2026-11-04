#pragma once

#include "Principal.hpp"
#include <opencv2/viz.hpp>
#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>

using namespace std;
using namespace cv;

inline string numero(double valor, int decimales = 2) {
    ostringstream texto;
    texto << fixed << setprecision(decimales) << valor;
    return texto.str();
}

struct EstadoInterfaz {
    Parametros parametros;
    // Modo 0: CPU; 1: OpenCL; 2: ambos sobre el mismo fotograma.
    int modo = 2, etapa = 0, seleccionado = 0;
    bool gpuDisponible = false, salir = false, registrar = false, reiniciarVista = false;
    Rect panel;
    int altoVentana = 900;
    array<Rect, 12> menos, mas;
};

inline void ajustarParametro(Parametros& p, int indice, int direccion) {
    switch (indice) {
        case 0:
            p.umbral = clamp(p.umbral + direccion * 5, 0, 255);
            break;
        case 1:
            // El paso de dos conserva el tamaño impar que exige el filtro Gaussiano.
            p.tamGauss = clamp(p.tamGauss + direccion * 2, 3, 51);
            break;
        case 2:
            p.sigma = clamp(p.sigma + direccion * 0.25, 0.1, 10.0);
            break;
        case 3:
            p.pesoPropio = clamp(p.pesoPropio + direccion * 0.05, 0.0, 1.0);
            break;
        case 4:
            p.diagonal = clamp(p.diagonal + direccion * 0.25, 0.0, 3.0);
            break;
        case 5:
            p.radioPuntos = clamp(p.radioPuntos + direccion, 0, 3);
            break;
        case 6:
            p.niveles = clamp(p.niveles + direccion, 2, 16);
            break;
        case 7:
            p.saturacion = clamp(p.saturacion + direccion * 0.05, 0.0, 3.0);
            break;
        case 8:
            p.brillo = clamp(p.brillo + direccion * 0.05, 0.0, 3.0);
            break;
        case 9:
            p.neon = clamp(p.neon + direccion * 0.05, 0.0, 3.0);
            break;
        case 10:
            p.desplazamiento = clamp(p.desplazamiento + direccion, 0, 3);
            break;
        case 11:
            p.contraste = clamp(p.contraste + direccion * 0.25, 0.1, 8.0);
            break;
    }
}

inline void eventoTeclado(const viz::KeyboardEvent& evento, void* datos) {
    // Ignorar KEY_UP evita aplicar dos cambios por una sola pulsación.
    if (evento.action != viz::KeyboardEvent::KEY_DOWN)
        return;
    auto& estado = *static_cast<EstadoInterfaz*>(datos);
    if (evento.code == 27 || evento.code == 'q')
        estado.salir = true;
    if (evento.code == ' ')
        estado.modo = estado.gpuDisponible ? (estado.modo + 1) % 3 : 0;
    if (evento.code == '\t')
        estado.etapa = (estado.etapa + 1) % 5;
    if (evento.code == 'b' || evento.code == 'B')
        estado.registrar = !estado.registrar;
    if (evento.code == 'r' || evento.code == 'R')
        estado.reiniciarVista = true;
    if (evento.symbol == "Up")
        estado.seleccionado = (estado.seleccionado + 11) % 12;
    if (evento.symbol == "Down")
        estado.seleccionado = (estado.seleccionado + 1) % 12;
    if (evento.symbol == "Left")
        ajustarParametro(estado.parametros, estado.seleccionado, -1);
    if (evento.symbol == "Right")
        ajustarParametro(estado.parametros, estado.seleccionado, 1);
}

inline void eventoRaton(const viz::MouseEvent& evento, void* datos) {
    if (evento.type != viz::MouseEvent::MouseButtonPress ||
        evento.button != viz::MouseEvent::LeftButton)
        return;
    auto& estado = *static_cast<EstadoInterfaz*>(datos);
    // VTK cuenta Y desde abajo; las coordenadas del panel empiezan arriba.
    Point punto =
        Point(evento.pointer.x, estado.altoVentana - 1 - evento.pointer.y) - estado.panel.tl();
    for (int i = 0; i < 12; ++i) {
        if (estado.menos[i].contains(punto) || estado.mas[i].contains(punto)) {
            estado.seleccionado = i;
            ajustarParametro(estado.parametros, i, estado.mas[i].contains(punto) ? 1 : -1);
            return;
        }
    }
}

inline Mat dibujarControles(EstadoInterfaz& estado, Size ventana) {
    estado.altoVentana = ventana.height;
    int ancho = max(600, ventana.width - 40);
    Mat lienzo(224, ancho, CV_8UC3, Scalar(30, 30, 30));
    const Parametros& p = estado.parametros;
    array<string, 12> etiquetas = {
        "Umbral: " + to_string(p.umbral),
        "Gauss: " + to_string(p.tamGauss) + " x " + to_string(p.tamGauss),
        "Sigma: " + numero(p.sigma),
        "Peso propio: " + numero(p.pesoPropio * 100, 0) + "%",
        "Diagonal: " + numero(p.diagonal),
        "Radio puntos: " + to_string(p.radioPuntos),
        "Niveles: " + to_string(p.niveles),
        "Saturacion: " + numero(p.saturacion),
        "Brillo: " + numero(p.brillo),
        "Neon: " + numero(p.neon),
        "Glitch: " + to_string(p.desplazamiento) + " px",
        "CLAHE: " + numero(p.contraste)};
    for (int i = 0; i < 12; ++i) {
        int x = (i / 6) * ancho / 2;
        int y = 9 + (i % 6) * 31;
        Scalar color = i == estado.seleccionado ? Scalar(100, 220, 255) : Scalar(235, 235, 235);
        putText(lienzo, etiquetas[i], Point(x + 12, y + 21), FONT_HERSHEY_SIMPLEX, 0.52, color, 1,
                LINE_AA);
        estado.menos[i] = Rect(x + ancho / 2 - 94, y, 32, 26);
        estado.mas[i] = Rect(x + ancho / 2 - 50, y, 32, 26);
        for (const auto& boton : {estado.menos[i], estado.mas[i]})
            rectangle(lienzo, boton, color, 1);
        putText(lienzo, "-", estado.menos[i].tl() + Point(10, 20), FONT_HERSHEY_SIMPLEX, 0.65,
                color, 1, LINE_AA);
        putText(lienzo, "+", estado.mas[i].tl() + Point(7, 20), FONT_HERSHEY_SIMPLEX, 0.65, color,
                1, LINE_AA);
    }
    putText(lienzo, "Flechas: elegir/ajustar | Arrastrar imagen: rotar 3D | R: restaurar vista",
            Point(12, 213), FONT_HERSHEY_SIMPLEX, 0.48, Scalar(200, 200, 200), 1, LINE_AA);
    estado.panel = Rect(20, max(0, ventana.height - lienzo.rows - 12), ancho, lienzo.rows);
    return lienzo;
}
