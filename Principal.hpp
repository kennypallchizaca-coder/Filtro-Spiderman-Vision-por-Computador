#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <vector>

struct Parametros {
    int umbral = 40;
    int tamGauss = 5;
    double sigma = 1.73;
    double pesoPropio = 0.3;
    double diagonal = 1.0;
    int niveles = 6;
    int radioPuntos = 2;
    double saturacion = 1.25;
    double brillo = 1.05;
    double neon = 1.15;
    int desplazamiento = 1;
    double contraste = 2.0;

    void validar() const {
        // Las comparaciones de rango también rechazan NaN antes de entrar a OpenCV.
        if (umbral < 0 || umbral > 255)
            throw std::invalid_argument("Umbral: 0..255");
        if (tamGauss < 3 || tamGauss > 51 || tamGauss % 2 == 0)
            throw std::invalid_argument("Tamano Gaussiano: impar entre 3 y 51");
        if (!(sigma >= 0.1 && sigma <= 10))
            throw std::invalid_argument("Sigma: 0.1..10");
        if (!(pesoPropio >= 0 && pesoPropio <= 1))
            throw std::invalid_argument("Peso del filtro propio: 0..1");
        if (!(diagonal >= 0 && diagonal <= 3))
            throw std::invalid_argument("Diagonal: 0..3");
        if (niveles < 2 || niveles > 16)
            throw std::invalid_argument("Niveles: 2..16");
        if (radioPuntos < 0 || radioPuntos > 3)
            throw std::invalid_argument("Radio: 0..3");
        if (!(saturacion >= 0 && saturacion <= 3))
            throw std::invalid_argument("Saturacion: 0..3");
        if (!(brillo >= 0 && brillo <= 3))
            throw std::invalid_argument("Brillo: 0..3");
        if (!(neon >= 0 && neon <= 3))
            throw std::invalid_argument("Neon: 0..3");
        if (desplazamiento < 0 || desplazamiento > 3)
            throw std::invalid_argument("Desplazamiento: 0..3");
        if (!(contraste >= 0.1 && contraste <= 8))
            throw std::invalid_argument("Contraste CLAHE: 0.1..8");
    }
};

inline cv::Mat crearKernel(double diagonal) {
    if (!(diagonal >= 0 && diagonal <= 3))
        throw std::invalid_argument("Diagonal: 0..3");
    float a = static_cast<float>(diagonal);
    // La suma cero cancela regiones uniformes; a regula los dos vecinos diagonales.
    // Su simetría de 180 grados hace coincidir aquí correlación y convolución.
    return cv::Mat_<float>({3, 3}, {-a, -1, 0, -1, 4 + 2 * a, -1, 0, -1, -a}) / (4 + 2 * a);
}

// Compilar la misma secuencia para Mat y UMat mantiene comparables ambas rutas.
template <class Imagen>
struct Efecto {
    Imagen comic, mediana, tabla, gris, imgGauss, gx, gy, gxpos, gypos, gradiente;
    Imagen binaria, hsv, lab;
    std::vector<Imagen> canales = std::vector<Imagen>(3);
    std::vector<Imagen> canalesGlich = std::vector<Imagen>(3);
    std::vector<Imagen> canalesLab = std::vector<Imagen>(3);
    Imagen filtroPropio, filtroPropioAbs;
    cv::Mat kernelPropio, moverazul, moverRojo;

    const Imagen& procesar(const Imagen& frame, const Parametros& p) {
        p.validar();
        validarColor(frame);
        // Reutilizar coeficientes y tablas evita reconstruirlos en cada fotograma.
        if (p.diagonal != diagonalAnterior) {
            kernelPropio = crearKernel(p.diagonal);
            diagonalAnterior = p.diagonal;
        }
        if (p.contraste != contrasteAnterior) {
            clahe->setClipLimit(p.contraste);
            contrasteAnterior = p.contraste;
        }
        if (p.niveles != nivelesAnteriores) {
            cv::Mat tablaCPU(1, 256, CV_8UC1);
            for (int i = 0; i < 256; ++i)
                tablaCPU.ptr<uchar>()[i] = cv::saturate_cast<uchar>(
                    cvRound(i * (p.niveles - 1) / 255.0) * 255.0 / (p.niveles - 1));
            tablaCPU.copyTo(tabla);
            nivelesAnteriores = p.niveles;
        }

        // CLAHE se aplica a la luminosidad L para ajustar contraste conservando el color.
        cv::cvtColor(frame, lab, cv::COLOR_BGR2Lab);
        cv::split(lab, canalesLab);
        clahe->apply(canalesLab[0], canalesLab[0]);
        cv::merge(canalesLab, lab);
        cv::cvtColor(lab, comic, cv::COLOR_Lab2BGR);
        cv::medianBlur(comic, mediana, 3);
        cv::cvtColor(mediana, gris, cv::COLOR_BGR2GRAY);
        // Calculamos los contornos antes del halftone para no detectar bordes de sus puntos.
        cv::GaussianBlur(gris, imgGauss, cv::Size(p.tamGauss, p.tamGauss), p.sigma, p.sigma);
        // El tipo flotante conserva signos y decimales antes de obtener la magnitud.
        cv::filter2D(imgGauss, filtroPropio, CV_32F, kernelPropio);
        cv::convertScaleAbs(filtroPropio, filtroPropioAbs);
        cv::Sobel(imgGauss, gx, CV_16S, 1, 0, 3, 1, 0);
        cv::Sobel(imgGauss, gy, CV_16S, 0, 1, 3, 1, 0);
        cv::convertScaleAbs(gx, gxpos);
        cv::convertScaleAbs(gy, gypos);
        // El Ejemplo 6 combina las respuestas absolutas de Sobel mediante pesos.
        cv::addWeighted(gxpos, 0.5, gypos, 0.5, 0, gradiente);
        cv::addWeighted(gradiente, 1 - p.pesoPropio, filtroPropioAbs, p.pesoPropio, 0, gradiente);
        cv::threshold(gradiente, binaria, p.umbral, 255, cv::THRESH_BINARY);

        cv::LUT(mediana, tabla, comic);
        aplicarTrama(gris, comic, p.radioPuntos);
        comic.setTo(cv::Scalar::all(0), binaria);
        aplicarNeon(comic, p);
        aplicarGlitch(comic, p.desplazamiento);
        return comic;
    }

    void aplicarTrama(const Imagen& gris, Imagen& comic, int radioMaximo) {
        validarColor(comic);
        if (gris.empty() || gris.type() != CV_8UC1 || gris.size() != comic.size())
            throw std::invalid_argument("La trama requiere gris 8UC1 del mismo tamano que BGR");
        if (radioMaximo < 0 || radioMaximo > 3)
            throw std::invalid_argument("Radio: 0..3");
        if (radioMaximo == 0)
            return;

        if (tamanoTrama != gris.size() || radioAnterior != radioMaximo) {
            // R+1 deja fuera de la máscara los píxeles que ningún círculo puede cubrir.
            cv::Mat baldosa(8, 8, CV_8UC1, cv::Scalar(radioMaximo + 1)), mosaico;
            // La baldosa conserva la rasterizacion exacta de circle, incluso en radios pequenos.
            for (int radio = radioMaximo; radio > 0; --radio)
                cv::circle(baldosa, cv::Point(4, 4), radio, cv::Scalar(radio), cv::FILLED);
            cv::repeat(baldosa, (gris.rows + 7) / 8, (gris.cols + 7) / 8, mosaico);
            mosaico(cv::Rect(0, 0, gris.cols, gris.rows)).copyTo(radioMinimo);
            tamanoTrama = gris.size();
            radioAnterior = radioMaximo;
        }

        cv::Size celdas((gris.cols + 7) / 8, (gris.rows + 7) / 8);
        // Un centro fuera de la imagen recibe brillo 255 y por eso genera radio cero.
        cv::warpAffine(gris, brilloCeldas, muestreo, celdas,
                       cv::INTER_NEAREST | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT,
                       cv::Scalar(255));
        cv::resize(brilloCeldas, brilloExpandido, cv::Size(celdas.width * 8, celdas.height * 8), 0,
                   0, cv::INTER_NEAREST);
        // r = round((255 - brillo) * R / 255): las zonas oscuras reciben puntos mayores.
        brilloExpandido(cv::Rect(0, 0, gris.cols, gris.rows))
            .convertTo(radioCelda, CV_8U, -radioMaximo / 255.0, radioMaximo);
        cv::compare(radioCelda, radioMinimo, puntos, cv::CMP_GE);
        comic.setTo(cv::Scalar::all(0), puntos);
    }

    void aplicarNeon(Imagen& comic, const Parametros& p) {
        p.validar();
        validarColor(comic);
        actualizarLUT(tablaSaturacion, p.saturacion, saturacionAnterior);
        actualizarLUT(tablaBrillo, p.brillo, brilloAnterior);
        actualizarLUT(tablaNeon, p.neon, neonAnterior);

        cv::cvtColor(comic, hsv, cv::COLOR_BGR2HSV);
        // Seleccionamos los tonos con la saturación original, antes de modificar S.
        cv::inRange(hsv, cv::Scalar(90, 51, 0), cv::Scalar(170, 255, 255), mascaraNeon);
        cv::split(hsv, canales);
        cv::LUT(canales[2], tablaNeon, valorNeon);
        valorNeon.copyTo(canales[2], mascaraNeon);
        cv::LUT(canales[1], tablaSaturacion, canales[1]);
        // Las dos LUT de V mantienen el redondeo y recorte de cada ajuste por separado.
        cv::LUT(canales[2], tablaBrillo, canales[2]);
        cv::merge(canales, hsv);
        cv::cvtColor(hsv, comic, cv::COLOR_HSV2BGR);
    }

    void aplicarGlitch(Imagen& comic, int desplazamiento) {
        validarColor(comic);
        if (desplazamiento < 0 || desplazamiento > 3)
            throw std::invalid_argument("Desplazamiento: 0..3");
        if (desplazamiento == 0)
            return;
        if (desplazamiento != desplazamientoAnterior) {
            moverazul = cv::Mat_<double>({2, 3}, {1, 0, double(desplazamiento), 0, 1, 0});
            moverRojo = cv::Mat_<double>({2, 3}, {1, 0, -double(desplazamiento), 0, 1, 0});
            desplazamientoAnterior = desplazamiento;
        }
        cv::split(comic, canales);
        cv::copyMakeBorder(canales[0], azulExtendido, 0, 0, desplazamiento, desplazamiento,
                           cv::BORDER_REPLICATE);
        cv::copyMakeBorder(canales[2], rojoExtendido, 0, 0, desplazamiento, desplazamiento,
                           cv::BORDER_REPLICATE);
        // BORDER_CONSTANT permite la ruta OpenCL; el margen replicado conserva los bordes.
        cv::warpAffine(azulExtendido, azulMovido, moverazul, azulExtendido.size(),
                       cv::INTER_NEAREST, cv::BORDER_CONSTANT);
        cv::warpAffine(rojoExtendido, rojoMovido, moverRojo, rojoExtendido.size(),
                       cv::INTER_NEAREST, cv::BORDER_CONSTANT);
        cv::Rect centro(desplazamiento, 0, comic.cols, comic.rows);
        azulMovido(centro).copyTo(canalesGlich[0]);
        canales[1].copyTo(canalesGlich[1]);
        rojoMovido(centro).copyTo(canalesGlich[2]);
        cv::merge(canalesGlich, comic);
    }

   private:
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    cv::Mat muestreo = cv::Mat_<double>({2, 3}, {8, 0, 4, 0, 8, 4});
    Imagen radioMinimo, brilloCeldas, brilloExpandido, radioCelda, puntos;
    Imagen tablaSaturacion, tablaBrillo, tablaNeon, mascaraNeon, valorNeon;
    Imagen azulExtendido, rojoExtendido, azulMovido, rojoMovido;
    cv::Size tamanoTrama;
    int nivelesAnteriores = -1, radioAnterior = -1, desplazamientoAnterior = -1;
    double diagonalAnterior = -1, contrasteAnterior = -1;
    double saturacionAnterior = -1, brilloAnterior = -1, neonAnterior = -1;

    static void validarColor(const Imagen& imagen) {
        if (imagen.empty() || imagen.type() != CV_8UC3)
            throw std::invalid_argument("Se requiere una imagen BGR 8UC3 no vacia");
    }

    void actualizarLUT(Imagen& destino, double factor, double& anterior) {
        if (factor == anterior)
            return;
        cv::Mat tablaCPU(1, 256, CV_8UC1);
        for (int i = 0; i < 256; ++i)
            tablaCPU.ptr<uchar>()[i] = cv::saturate_cast<uchar>(i * factor);
        tablaCPU.copyTo(destino);
        anterior = factor;
    }
};
