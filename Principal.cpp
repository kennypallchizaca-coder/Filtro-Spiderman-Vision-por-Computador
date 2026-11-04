#include "Principal.hpp"
#include "Mediciones.hpp"
#include "Interfaz.hpp"
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/viz.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

using namespace std;
using namespace cv;
using Reloj = chrono::steady_clock;

double milisegundos(Reloj::time_point inicio) {
    return chrono::duration<double, milli>(Reloj::now() - inicio).count();
}

bool prepararOpenCL(const string& seleccion, string& dispositivo) {
    dispositivo = "OpenCL no disponible";
    try {
        if (!ocl::haveOpenCL())
            return false;
        // El selector explícito evita medir otra GPU distinta de la solicitada.
        ocl::Context contexto = ocl::Context::create(seleccion);
        if (contexto.empty() || contexto.ndevices() == 0)
            return false;
        ocl::OpenCLExecutionContext::create(contexto, contexto.device(0)).bind();
        ocl::setUseOpenCL(true);
        const ocl::Device& gpu = ocl::Device::getDefault();
        if (!ocl::useOpenCL() || !(gpu.type() & ocl::Device::TYPE_GPU))
            return false;
        dispositivo = gpu.name();
        cout << "OpenCL GPU: " << dispositivo << "\nFabricante: " << gpu.vendorName()
             << "\nControlador: " << gpu.driverVersion() << '\n';
        return true;
    } catch (const cv::Exception& error) {
        cerr << "No se pudo seleccionar OpenCL: " << error.what() << '\n';
        return false;
    }
}

struct Tiempos {
    // -1 identifica una ruta no ejecutada, en vez de presentarla como cero milisegundos.
    double cpu = -1, gpu = -1, subida = 0, bajada = 0;
    double procesoGPU() const {
        return subida + gpu + bajada;
    }
};

template <class Imagen>
const Imagen& escogerEtapa(const Efecto<Imagen>& efecto, int etapa) {
    switch (etapa) {
        case 1:
            return efecto.gris;
        case 2:
            return efecto.imgGauss;
        case 3:
            return efecto.filtroPropioAbs;
        case 4:
            return efecto.binaria;
        default:
            return efecto.comic;
    }
}

Tiempos procesarFrame(const Mat& frame, const Parametros& p, int modo, int etapa, Efecto<Mat>& cpu,
                      Efecto<UMat>& gpu, UMat& frameGPU, Mat& resultado) {
    Tiempos tiempos;
    if (modo != 1) {
        ocl::setUseOpenCL(false);
        auto inicio = Reloj::now();
        cpu.procesar(frame, p);
        tiempos.cpu = milisegundos(inicio);
        if (modo == 0)
            resultado = escogerEtapa(cpu, etapa);
    }
    if (modo != 0) {
        ocl::setUseOpenCL(true);
        if (!ocl::useOpenCL())
            throw runtime_error("OpenCL dejo de estar disponible.");
        // OpenCL es asíncrono: finish espera el trabajo, no solo su envío a la cola.
        ocl::finish();
        auto inicio = Reloj::now();
        // Medimos las transferencias aparte para distinguir filtrado y costo total.
        frame.copyTo(frameGPU);
        ocl::finish();
        tiempos.subida = milisegundos(inicio);
        inicio = Reloj::now();
        gpu.procesar(frameGPU, p);
        ocl::finish();
        tiempos.gpu = milisegundos(inicio);
        inicio = Reloj::now();
        escogerEtapa(gpu, etapa).copyTo(resultado);
        ocl::finish();
        tiempos.bajada = milisegundos(inicio);
    }
    return tiempos;
}

bool esCamara(const string& fuente) {
    return !fuente.empty() && all_of(fuente.begin(), fuente.end(),
                                     [](unsigned char c) { return c >= '0' && c <= '9'; });
}

void abrirFuente(VideoCapture& video, const string& fuente, Size tamano) {
    if (esCamara(fuente)) {
        int indice = stoi(fuente);
        video.open(indice, CAP_DSHOW);
        if (!video.isOpened())
            video.open(indice);
        video.set(CAP_PROP_FRAME_WIDTH, tamano.width);
        video.set(CAP_PROP_FRAME_HEIGHT, tamano.height);
    } else {
        video.open(fuente);
    }
    if (!video.isOpened())
        throw runtime_error("No se pudo abrir la fuente: " + fuente);
}

bool leerFrame(VideoCapture& video, Mat& frame, Size tamano, bool repetir) {
    if (!video.read(frame) || frame.empty()) {
        if (!repetir)
            return false;
        if (!video.set(CAP_PROP_POS_FRAMES, 0))
            return false;
        if (!video.read(frame) || frame.empty())
            return false;
    }
    if (frame.size() != tamano)
        resize(frame, frame, tamano, 0, 0, INTER_AREA);
    return true;
}

void crearCarpetaPadre(const string& ruta) {
    auto padre = filesystem::path(ruta).parent_path();
    if (!padre.empty())
        filesystem::create_directories(padre);
}

ofstream abrirCSV(const string& ruta, bool vivo) {
    crearCarpetaPadre(ruta);
    bool cabecera = !vivo || !filesystem::exists(ruta) || filesystem::file_size(ruta) == 0;
    ofstream archivo(ruta, vivo ? ios::app : ios::out);
    if (!archivo)
        throw runtime_error("No se pudo crear: " + ruta);
    if (cabecera) {
        archivo << "frame,modo,fuente,ancho,alto,etapa,kernel,sigma,peso_propio,diagonal,umbral,"
                   "radio_puntos,niveles,saturacion,brillo,neon,desplazamiento,clahe,"
                   "ms_subida,ms_filtro,ms_bajada,ms_proceso,ms_ciclo,"
                   "fps_proceso,ram_mib,cpu_pct,gpu_motor_pct,opencl,dispositivo\n";
    }
    cout << (vivo ? "Registro en vivo: " : "Benchmark sin visualizacion: ") << ruta << '\n';
    return archivo;
}

string campoCSV(const string& texto) {
    string salida = "\"";
    for (char caracter : texto) {
        salida += caracter;
        if (caracter == '"')
            salida += '"';
    }
    return salida + '"';
}

void escribirFila(ofstream& csv, int frame, const string& modo, const string& fuente, Size tamano,
                  int etapa, const Parametros& p, const Tiempos& t, double ciclo,
                  const MuestraRecursos& recursos, const string& dispositivo) {
    bool esGPU = modo == "OpenCL";
    double filtro = esGPU ? t.gpu : t.cpu;
    double total = esGPU ? t.procesoGPU() : t.cpu;
    csv << fixed << setprecision(6) << frame << ',' << modo << ',' << campoCSV(fuente) << ','
        << tamano.width << ',' << tamano.height << ',' << etapa << ',' << p.tamGauss << ','
        << p.sigma << ',' << p.pesoPropio << ',' << p.diagonal << ',' << p.umbral << ','
        << p.radioPuntos << ',' << p.niveles << ',' << p.saturacion << ',' << p.brillo << ','
        << p.neon << ',' << p.desplazamiento << ',' << p.contraste << ',' << (esGPU ? t.subida : 0)
        << ',' << filtro << ',' << (esGPU ? t.bajada : 0) << ',' << total << ',' << ciclo << ','
        << (total > 0 ? 1000 / total : 0) << ',' << recursos.ramMiB << ',';
    if (recursos.cpuDisponible)
        csv << recursos.cpuPorcentaje;
    csv << ',';
    if (recursos.gpuDisponible)
        csv << recursos.gpuMotorPorcentaje;
    csv << ',' << esGPU << ',' << campoCSV(esGPU ? dispositivo : "CPU Mat") << '\n';
    if (!csv)
        throw runtime_error("Error al escribir las mediciones.");
}

void capturarClip(VideoCapture& video, Size tamano, int cantidad, const string& ruta) {
    if (filesystem::exists(ruta))
        throw runtime_error(
            "El video ya existe. Elija otra ruta para conservar las pruebas anteriores.");
    crearCarpetaPadre(ruta);
    VideoWriter grabacion(ruta, VideoWriter::fourcc('M', 'J', 'P', 'G'), 30, tamano);
    if (!grabacion.isOpened())
        throw runtime_error("No se pudo crear el video de prueba.");
    Mat frame;
    for (int i = 0; i < cantidad; ++i) {
        if (!leerFrame(video, frame, tamano, false))
            throw runtime_error("La fuente termino al grabar.");
        grabacion.write(frame);
    }
    cout << "Video original de prueba: " << ruta << " (" << cantidad << " fotogramas)\n";
}

int benchmark(VideoCapture& video, const string& fuente, const string& ruta, int cantidad,
              Size tamano, int modo, const Parametros& p, const string& dispositivo) {
    Efecto<Mat> cpu;
    Efecto<UMat> gpu;
    UMat frameGPU;
    Mat frame, resultado;
    RecursosProceso recursos;
    ofstream csv = abrirCSV(ruta, false);
    // Calentar excluye la compilación inicial de kernels y prepara los contadores de recursos.
    auto inicioCalentamiento = Reloj::now();
    for (int i = 0; i < 15 || milisegundos(inicioCalentamiento) < 1100; ++i) {
        if (!leerFrame(video, frame, tamano, true))
            throw runtime_error("No hay fotogramas para calentar.");
        procesarFrame(frame, p, modo, 0, cpu, gpu, frameGPU, resultado);
    }
    // Ambos procesos deben empezar a medir desde el mismo fotograma del video fijo.
    if (!video.set(CAP_PROP_POS_FRAMES, 0))
        throw runtime_error("El video no permite volver al inicio para comparar.");
    vector<double> filtros, totales;
    double ramMaxima = 0;
    for (int i = 0; i < cantidad; ++i) {
        auto inicio = Reloj::now();
        if (!leerFrame(video, frame, tamano, true))
            throw runtime_error("No hay fotogramas para medir.");
        Tiempos t = procesarFrame(frame, p, modo, 0, cpu, gpu, frameGPU, resultado);
        double ciclo = milisegundos(inicio);
        MuestraRecursos muestra = recursos.leer();
        escribirFila(csv, i, modo == 0 ? "CPU" : "OpenCL", fuente, tamano, 0, p, t, ciclo, muestra,
                     dispositivo);
        filtros.push_back(modo == 0 ? t.cpu : t.gpu);
        totales.push_back(modo == 0 ? t.cpu : t.procesoGPU());
        ramMaxima = max(ramMaxima, muestra.ramMiB);
    }
    double media = accumulate(filtros.begin(), filtros.end(), 0.0) / filtros.size();
    sort(filtros.begin(), filtros.end());
    double mediana = filtros.size() % 2
                         ? filtros[filtros.size() / 2]
                         : (filtros[filtros.size() / 2 - 1] + filtros[filtros.size() / 2]) / 2;
    double total = accumulate(totales.begin(), totales.end(), 0.0) / totales.size();
    cout << "Muestras: " << cantidad << "; filtro medio: " << numero(media)
         << " ms; mediana: " << numero(mediana) << " ms; proceso medio: " << numero(total)
         << " ms; FPS de proceso: " << numero(1000 / total)
         << "; RAM maxima proceso: " << numero(ramMaxima) << " MiB\n";
    return 0;
}

int visualizar(VideoCapture& video, const string& fuente, Size tamano, EstadoInterfaz& estado,
               const string& dispositivo, bool smoke, int limite, const string& rutaCaptura) {
    Mat frame, resultado;
    if (!leerFrame(video, frame, tamano, false))
        throw runtime_error("El primer fotograma esta vacio.");
    Efecto<Mat> cpu;
    Efecto<UMat> gpu;
    UMat frameGPU;
    RecursosProceso recursos;
    viz::Viz3d visor("Spider-Verse | Original y efecto | VTK 3D");
    visor.setWindowSize(Size(1280, 900));
    visor.setBackgroundColor(viz::Color(22, 22, 22));
    // Los ejes OpenCV, con Y hacia abajo, mantienen las texturas verticales y alineadas.
    auto poseInicial = Affine3d(Matx33d::eye(), Vec3d(0, 0, -5.6));
    visor.setViewerPose(poseInicial);
    double anchoPlano = 1.85;
    double altoPlano = anchoPlano * tamano.height / tamano.width;
    viz::WImage3D original(frame, Size2d(anchoPlano, altoPlano), Vec3d(-0.98, -0.15, 0),
                           Vec3d(0, 0, 1), Vec3d(0, 1, 0));
    viz::WImage3D efecto(frame, Size2d(anchoPlano, altoPlano), Vec3d(0.98, -0.15, 0),
                         Vec3d(0, 0, 1), Vec3d(0, 1, 0));
    visor.showWidget("Original", original);
    visor.showWidget("Efecto", efecto);
    visor.registerKeyboardCallback(eventoTeclado, &estado);
    visor.registerMouseCallback(eventoRaton, &estado);
    array<string, 5> etapas = {"Efecto final", "Gris CLAHE + mediana", "Gaussiano", "Kernel propio",
                               "Mascara de contornos"};
    array<string, 3> modos = {"CPU Mat", "OpenCL UMat", "Comparar CPU / OpenCL"};
    ofstream registro;
    int indice = 0;
    double fps = 0;
    while (!visor.wasStopped() && !estado.salir) {
        auto inicio = Reloj::now();
        if (indice > 0 && !leerFrame(video, frame, tamano, false)) {
            if (esCamara(fuente))
                throw runtime_error("La camara dejo de entregar fotogramas.");
            cout << "Fin de la fuente de video.\n";
            break;
        }
        // spinOnce atiende cambios del usuario; el CSV debe conservar los parámetros ya usados.
        const Parametros parametrosFrame = estado.parametros;
        const int etapaFrame = estado.etapa;
        Tiempos t = procesarFrame(frame, parametrosFrame, estado.modo, estado.etapa, cpu, gpu,
                                  frameGPU, resultado);
        original.setImage(frame);
        efecto.setImage(resultado);
        MuestraRecursos muestra = recursos.leer();
        Size ventana = visor.getWindowSize();
        if (ventana.width < 650 || ventana.height < 500) {
            visor.setWindowSize(Size(max(650, ventana.width), max(500, ventana.height)));
            ventana = visor.getWindowSize();
        }
        Mat lienzo = dibujarControles(estado, ventana);
        Rect posicionVTK(estado.panel.x, ventana.height - estado.panel.y - estado.panel.height,
                         estado.panel.width, estado.panel.height);
        visor.showWidget("Controles", viz::WImageOverlay(lienzo, posicionVTK));
        string mediciones = "CPU: " + (t.cpu < 0 ? string("--") : numero(t.cpu)) +
                            " ms | OpenCL: " + (t.gpu < 0 ? string("--") : numero(t.gpu)) +
                            " ms | FPS ventana: " + numero(fps, 1);
        if (t.gpu >= 0)
            mediciones += " | Subida/bajada: " + numero(t.subida) + "/" + numero(t.bajada) + " ms";
        string recursosTexto =
            "RAM proceso: " + numero(muestra.ramMiB, 1) + " MiB | CPU proceso: " +
            (muestra.cpuDisponible ? numero(muestra.cpuPorcentaje, 1) + "%" : "sin muestra") +
            " | GPU proceso (motor max): " +
            (muestra.gpuDisponible ? numero(muestra.gpuMotorPorcentaje, 1) + "%" : "sin muestra");
        visor.showWidget("Titulo",
                         viz::WText("SPIDER-VERSE | " + etapas[estado.etapa],
                                    Point(20, ventana.height - 38), 19, viz::Color::white()));
        visor.showWidget("Tiempos", viz::WText(mediciones, Point(20, ventana.height - 65), 15,
                                               viz::Color::white()));
        visor.showWidget("Recursos", viz::WText(recursosTexto, Point(20, ventana.height - 89), 15,
                                                viz::Color::white()));
        visor.showWidget(
            "Modo", viz::WText(modos[estado.modo] + " | " + dispositivo,
                               Point(20, ventana.height - 113), 15, viz::Color(100, 220, 255)));
        visor.showWidget("Ayuda",
                         viz::WText("Espacio: modo | Tab: etapa | B: registrar CSV | Esc: salir" +
                                        string(estado.registrar ? " | GRABANDO" : ""),
                                    Point(20, ventana.height - 137), 15, viz::Color::white()));
        visor.showWidget("EtiquetaOriginal", viz::WText("ORIGINAL", Point(65, ventana.height - 180),
                                                        17, viz::Color::white()));
        visor.showWidget("EtiquetaEfecto",
                         viz::WText("EFECTO", Point(ventana.width / 2 + 20, ventana.height - 180),
                                    17, viz::Color::white()));
        if (estado.reiniciarVista) {
            visor.setViewerPose(poseInicial);
            estado.reiniciarVista = false;
        }
        visor.spinOnce(1, true);
        double ciclo = milisegundos(inicio);
        double actual = 1000 / max(ciclo, 0.001);
        fps = indice == 0 ? actual : fps * 0.9 + actual * 0.1;
        if (estado.registrar) {
            if (!registro.is_open())
                registro = abrirCSV("resultados/mediciones_en_vivo.csv", true);
            if (t.cpu >= 0)
                escribirFila(registro, indice, "CPU", fuente, tamano, etapaFrame, parametrosFrame,
                             t, ciclo, muestra, dispositivo);
            if (t.gpu >= 0)
                escribirFila(registro, indice, "OpenCL", fuente, tamano, etapaFrame,
                             parametrosFrame, t, ciclo, muestra, dispositivo);
        } else if (registro.is_open()) {
            registro.close();
        }
        ++indice;
        if (smoke && indice >= limite) {
            crearCarpetaPadre(rutaCaptura);
            visor.saveScreenshot(rutaCaptura);
            cout << "VTK: " << indice << " fotogramas de " << fuente << "; captura: " << rutaCaptura
                 << '\n';
            break;
        }
    }
    if (smoke && indice < limite)
        throw runtime_error("La prueba VTK termino antes del limite solicitado.");
    visor.close();
    return 0;
}

int main(int argc, char** argv) {
    try {
        const string claves =
            "{help h||Ayuda}{benchmark||Medir sin ventanas}{smoke||Prueba VTK con cierre "
            "automatico}"
            "{source|0|Camara numerica o archivo}{mode|both|cpu, gpu o both}"
            "{frames|120|Numero de muestras}{width|640|Ancho}{height|480|Alto}"
            "{output||CSV de benchmark o captura smoke}"
            "{capture||Grabar clip original MJPG para comparar los mismos fotogramas}"
            "{device|NVIDIA:GPU:RTX 4070|Seleccion OpenCL explicita}"
            "{kernel|5|Tamano impar Gaussiano, 3 a 51}{sigma|1.73|Sigma Gaussiano}"
            "{own-weight|0.3|Peso filtro propio, 0 a 1}{diagonal|1|Peso diagonal, 0 a 3}";
        CommandLineParser argumentos(argc, argv, claves);
        if (argumentos.has("help")) {
            argumentos.printMessage();
            cout << "Controles VTK: Espacio=modo, Tab=etapa, B=CSV, R=vista, Esc=salir.\n"
                    "Flechas arriba/abajo=parametro, izquierda/derecha=ajuste. Botones +/- con "
                    "raton.\n";
            return 0;
        }
        string fuente = argumentos.get<string>("source");
        string modoTexto = argumentos.get<string>("mode");
        string salida = argumentos.get<string>("output");
        Size tamano(argumentos.get<int>("width"), argumentos.get<int>("height"));
        int cantidad = argumentos.get<int>("frames");
        Parametros p;
        p.tamGauss = argumentos.get<int>("kernel");
        p.sigma = argumentos.get<double>("sigma");
        p.pesoPropio = argumentos.get<double>("own-weight");
        p.diagonal = argumentos.get<double>("diagonal");
        if (!argumentos.check()) {
            argumentos.printErrors();
            return 1;
        }
        if (cantidad < 1 || cantidad > 10000 || tamano.width < 8 || tamano.height < 8 ||
            tamano.width > 3840 || tamano.height > 2160)
            throw runtime_error("Dimensiones o cantidad invalidas.");
        p.validar();
        if (modoTexto != "cpu" && modoTexto != "gpu" && modoTexto != "both")
            throw runtime_error("mode debe ser cpu, gpu o both.");
        bool medir = argumentos.has("benchmark");
        if (medir && modoTexto == "both")
            throw runtime_error(
                "Para comparar RAM use procesos separados: --mode=cpu y --mode=gpu.");
        if (medir && esCamara(fuente))
            throw runtime_error(
                "Benchmark requiere un video fijo. Grabe primero con "
                "--capture=resultados/entrada.avi.");
        if (salida.empty())
            salida =
                medir ? "resultados/benchmark_" + modoTexto + ".csv" : "resultados/captura_vtk.png";
        if (!esCamara(fuente) && filesystem::exists(fuente) && filesystem::exists(salida) &&
            filesystem::equivalent(fuente, salida))
            throw runtime_error("La salida no puede ser el mismo archivo que el video de entrada.");
        VideoCapture video;
        abrirFuente(video, fuente, tamano);
        if (argumentos.has("capture")) {
            string destino = argumentos.get<string>("capture");
            if (destino.empty())
                throw runtime_error("capture necesita una ruta .avi.");
            capturarClip(video, tamano, cantidad, destino);
            return 0;
        }
        string dispositivo = "CPU Mat";
        bool gpuDisponible =
            modoTexto != "cpu" && prepararOpenCL(argumentos.get<string>("device"), dispositivo);
        if (modoTexto == "gpu" && !gpuDisponible)
            throw runtime_error(
                "No hay una GPU OpenCL que coincida con device; no se usara CPU como si fuera "
                "GPU.");
        int modo = modoTexto == "cpu" || !gpuDisponible ? 0 : (modoTexto == "gpu" ? 1 : 2);
        if (medir)
            return benchmark(video, fuente, salida, cantidad, tamano, modo, p, dispositivo);
        EstadoInterfaz estado;
        estado.parametros = p;
        estado.modo = modo;
        estado.gpuDisponible = gpuDisponible;
        return visualizar(video, fuente, tamano, estado, dispositivo, argumentos.has("smoke"),
                          cantidad, salida);
    } catch (const exception& error) {
        cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
