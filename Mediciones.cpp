#include "Mediciones.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace {
bool tiempoCpu(std::uint64_t& total) {
    FILETIME creacion{}, salida{}, kernel{}, usuario{};
    if (!GetProcessTimes(GetCurrentProcess(), &creacion, &salida, &kernel, &usuario))
        return false;
    auto unidades = [](FILETIME tiempo) {
        return (std::uint64_t(tiempo.dwHighDateTime) << 32) | tiempo.dwLowDateTime;
    };
    total = unidades(kernel) + unidades(usuario);
    return true;
}
}  // namespace

struct RecursosProceso::Estado {
    PDH_HQUERY consulta = nullptr;
    PDH_HCOUNTER gpu = nullptr;
    std::chrono::steady_clock::time_point momento = std::chrono::steady_clock::now();
    std::uint64_t cpuAnterior = 0;
    bool cpuValido = false;
    DWORD procesadores = std::max(DWORD{1}, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    std::wstring prefijo = L"pid_" + std::to_wstring(GetCurrentProcessId()) + L"_";
    MuestraRecursos muestra;
};

double ramActualMiB() {
    // Working Set incluye bibliotecas y búferes residentes de todo el proceso.
    PROCESS_MEMORY_COUNTERS memoria{};
    memoria.cb = sizeof(memoria);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &memoria, sizeof(memoria)))
        return 0;
    return static_cast<double>(memoria.WorkingSetSize) / (1024 * 1024);
}

RecursosProceso::RecursosProceso() : estado_(std::make_unique<Estado>()) {
    auto& e = *estado_;
    e.muestra.ramMiB = ramActualMiB();
    e.cpuValido = tiempoCpu(e.cpuAnterior);
    if (PdhOpenQueryW(nullptr, 0, &e.consulta) != ERROR_SUCCESS)
        return;
    if (PdhAddEnglishCounterW(e.consulta, L"\\GPU Engine(*)\\Utilization Percentage", 0, &e.gpu) !=
        ERROR_SUCCESS) {
        PdhCloseQuery(e.consulta);
        e.consulta = nullptr;
        e.gpu = nullptr;
    } else {
        PdhCollectQueryData(e.consulta);
    }
}

RecursosProceso::~RecursosProceso() {
    if (estado_->consulta)
        PdhCloseQuery(estado_->consulta);
}

MuestraRecursos RecursosProceso::leer() {
    auto& e = *estado_;
    e.muestra.ramMiB = ramActualMiB();
    const auto ahora = std::chrono::steady_clock::now();
    const double segundos = std::chrono::duration<double>(ahora - e.momento).count();
    // Espaciar CPU/GPU un segundo reduce el costo de consultar los contadores.
    if (segundos < 1.0)
        return e.muestra;
    e.momento = ahora;
    std::uint64_t cpuActual = 0;
    const bool valido = tiempoCpu(cpuActual);
    e.muestra.cpuDisponible = valido && e.cpuValido && cpuActual >= e.cpuAnterior;
    // FILETIME usa 100 ns; dividir entre procesadores normaliza el porcentaje a 0..100.
    e.muestra.cpuPorcentaje =
        e.muestra.cpuDisponible
            ? std::clamp(100.0 * (cpuActual - e.cpuAnterior) / (1e7 * segundos * e.procesadores),
                         0.0, 100.0)
            : 0.0;
    e.cpuAnterior = cpuActual;
    e.cpuValido = valido;
    e.muestra.gpuDisponible = false;
    e.muestra.gpuMotorPorcentaje = 0;
    if (!e.gpu || PdhCollectQueryData(e.consulta) != ERROR_SUCCESS)
        return e.muestra;
    DWORD bytes = 0, cantidad = 0;
    if (PdhGetFormattedCounterArrayW(e.gpu, PDH_FMT_DOUBLE, &bytes, &cantidad, nullptr) !=
            static_cast<PDH_STATUS>(PDH_MORE_DATA) ||
        !bytes)
        return e.muestra;
    std::vector<unsigned char> buffer(bytes);
    auto* motores = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(e.gpu, PDH_FMT_DOUBLE, &bytes, &cantidad, motores) !=
        ERROR_SUCCESS)
        return e.muestra;
    for (DWORD i = 0; i < cantidad; ++i) {
        const auto& motor = motores[i];
        // El PID evita atribuir al proyecto la actividad GPU de otros programas.
        if (!motor.szName || std::wcsncmp(motor.szName, e.prefijo.c_str(), e.prefijo.size()) != 0)
            continue;
        const auto& valor = motor.FmtValue;
        if ((valor.CStatus == PDH_CSTATUS_VALID_DATA || valor.CStatus == PDH_CSTATUS_NEW_DATA) &&
            std::isfinite(valor.doubleValue)) {
            e.muestra.gpuDisponible = true;
            // Sumar motores puede superar 100%; mostramos el motor más ocupado del proceso.
            e.muestra.gpuMotorPorcentaje =
                std::max(e.muestra.gpuMotorPorcentaje, std::clamp(valor.doubleValue, 0.0, 100.0));
        }
    }
    return e.muestra;
}
