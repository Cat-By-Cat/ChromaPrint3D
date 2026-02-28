#pragma once

#include "application/server_facade.h"

#include <memory>

namespace chromaprint3d::backend {

void InitBackendRuntime(std::shared_ptr<ServerFacade> facade);
ServerFacade& GetBackendFacade();

} // namespace chromaprint3d::backend
