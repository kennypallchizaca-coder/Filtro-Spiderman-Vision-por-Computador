#pragma once

#include <memory>

struct MuestraRecursos {
    double ramMiB = 0;
    double cpuPorcentaje = 0;
    double gpuMotorPorcentaje = 0;
    bool gpuDisponible = false;
    bool cpuDisponible = false;
};

double ramActualMiB();

class RecursosProceso {
   public:
    RecursosProceso();
    ~RecursosProceso();
    RecursosProceso(const RecursosProceso&) = delete;
    RecursosProceso& operator=(const RecursosProceso&) = delete;
    MuestraRecursos leer();

   private:
    struct Estado;
    std::unique_ptr<Estado> estado_;
};
