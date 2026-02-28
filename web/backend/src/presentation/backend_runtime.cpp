#include "presentation/backend_runtime.h"

#include <stdexcept>

namespace chromaprint3d::backend {
namespace {

std::shared_ptr<ServerFacade> g_facade;

} // namespace

void InitBackendRuntime(std::shared_ptr<ServerFacade> facade) { g_facade = std::move(facade); }

ServerFacade& GetBackendFacade() {
    if (!g_facade) { throw std::runtime_error("Backend runtime is not initialized"); }
    return *g_facade;
}

} // namespace chromaprint3d::backend
