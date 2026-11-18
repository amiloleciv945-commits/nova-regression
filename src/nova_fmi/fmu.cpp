
#include "fmi1/fmi1_fmu.hpp"
#include "fmi2/fmi2_fmu.hpp"
#include "fmi3/fmi3_fmu.hpp"
#include "components/util/temp_dir.hpp"
#include "components/util/unzipper.hpp"

#include "nova/components/logger/logger.hpp"

#include <fmi4c.h>
std::unique_ptr<nova_fmi::fmu> nova_fmi::loadFmu(const std::filesystem::path& fmuPath, bool fmiLogging)
{

    if (!exists(fmuPath)) {
        nova::log::err("No such file: {}", absolute(fmuPath).string());
        return nullptr;
    }

    const std::string fmuName = std::filesystem::path(fmuPath).stem().string();
    auto temp = std::make_unique<nova::temp_dir>(fmuName);

    if (!nova::unzip(fmuPath, temp->path())) {
        nova::log::err("Failed to unzip '{}' to tempdir '{}'!", fmuPath.string(), temp->path().string());
        return nullptr;
    }

    auto fmuCtx = fmi4c_loadUnzippedFmu(fmuPath.string().c_str(), temp->path().string().c_str());
    if (!fmuCtx) {
        nova::log::err("Failed to load '{}'!", fmuPath.string());
        return nullptr;
    }

    std::filesystem::path binDirOld, binDirFmi3, binDir;
#ifdef _WIN32
    binDirOld = temp->path() / "binaries" / "win64";
    binDirFmi3 = temp->path() / "binaries" / "x86_64-windows";
#else
    binDirOld = temp->path() / "binaries" / "linux64";
    binDirFmi3 = temp->path() / "binaries" / "x86_64-linux";
#endif

    if (std::filesystem::exists(binDirOld)) {
        binDir = binDirOld;
    } else if (std::filesystem::exists(binDirFmi3)) {
        binDir = binDirFmi3;
    } else {
        // Fallback or leave it empty to trigger the error below
        binDir = binDirOld;
    }

    const char* modelIdentifier = nullptr;
    switch (fmi4c_getFmiVersion(fmuCtx)) {
        case fmiVersion1:
            modelIdentifier = fmi1_getModelIdentifier(fmuCtx);
            break;
        case fmiVersion2:
            modelIdentifier = fmi2cs_getModelIdentifier(fmuCtx);
            break;
        case fmiVersion3:
            modelIdentifier = fmi3cs_getModelIdentifier(fmuCtx);
            break;
        default:
            break;
    }

    std::filesystem::path dllPath;
    if (modelIdentifier && modelIdentifier[0] != '\0') {
#ifdef _WIN32
        dllPath = binDir / (std::string(modelIdentifier) + ".dll");
#else
        dllPath = binDir / (std::string(modelIdentifier) + ".so");
#endif
    }

    if (dllPath.empty() || !std::filesystem::is_regular_file(dllPath)) {
        nova::log::err(
            "Failed to find FMU model library '{}' in {}!",
            modelIdentifier ? modelIdentifier : "",
            binDir.string());
        fmi4c_freeFmu(fmuCtx);
        return nullptr;
    }

    auto loader = std::make_shared<nova_fmi::dll_loader>(dllPath.string());
    auto handle = std::make_unique<fmicontext>(fmuCtx, std::move(temp), loader);

    switch (fmi4c_getFmiVersion(handle->get())) {
        case fmiVersion1:
            return std::make_unique<fmi1_fmu>(std::move(handle), fmiLogging);
        case fmiVersion2:
            return std::make_unique<fmi2_fmu>(std::move(handle), fmiLogging);
        case fmiVersion3:
            return std::make_unique<fmi3_fmu>(std::move(handle), fmiLogging);
        default:
            return nullptr;
    }
}
