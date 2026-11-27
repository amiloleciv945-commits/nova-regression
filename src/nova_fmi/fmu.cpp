
#include "fmi1/fmi1_fmu.hpp"
#include "fmi2/fmi2_fmu.hpp"
#include "fmi3/fmi3_fmu.hpp"
#include "components/util/temp_dir.hpp"
#include "components/util/unzipper.hpp"

#include <fmi4c.h>
#include <stdexcept>

std::unique_ptr<nova_fmi::fmu> nova_fmi::loadFmu(const std::filesystem::path& fmuPath, bool fmiLogging)
{
    if (!exists(fmuPath)) {
        throw std::runtime_error("No such FMU file: " + absolute(fmuPath).string());
    }

    const std::string fmuFile = fmuPath.string();
    const std::string fmuName = fmuPath.stem().string();
    auto temp = std::make_unique<nova::temp_dir>(fmuName);

    if (!nova::unzip(fmuPath, temp->path())) {
        throw std::runtime_error("Failed to unzip FMU '" + fmuFile + "'");
    }

    const char* loadError = nullptr;
    std::unique_ptr<fmuHandle, decltype(&fmi4c_freeFmu)> fmuCtx(
        fmi4c_loadUnzippedFmuWithError(fmuFile.c_str(), temp->path().string().c_str(), &loadError), fmi4c_freeFmu);
    if (!fmuCtx) {
        throw std::runtime_error("FMU '" + fmuFile + "': " + loadError);
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
    const auto version = fmi4c_getFmiVersion(fmuCtx.get());
    switch (version) {
        case fmiVersion1:
            modelIdentifier = fmi1_getModelIdentifier(fmuCtx.get());
            break;
        case fmiVersion2:
            modelIdentifier = fmi2cs_getModelIdentifier(fmuCtx.get());
            break;
        case fmiVersion3:
            modelIdentifier = fmi3cs_getModelIdentifier(fmuCtx.get());
            break;
        default:
            throw std::runtime_error("Unsupported FMI version in FMU '" + fmuFile + "'");
    }

    if (!modelIdentifier || modelIdentifier[0] == '\0') {
        throw std::runtime_error("Missing co-simulation model identifier in FMU '" + fmuFile + "'");
    }
#ifdef _WIN32
    const auto dllPath = binDir / (std::string(modelIdentifier) + ".dll");
#else
    const auto dllPath = binDir / (std::string(modelIdentifier) + ".so");
#endif

    if (!std::filesystem::is_regular_file(dllPath)) {
        const auto library = dllPath.lexically_relative(temp->path());
        throw std::runtime_error("FMU '" + fmuFile + "' is missing the model library '" + library.generic_string() + "'");
    }

    auto loader = std::make_shared<nova_fmi::dll_loader>(dllPath.string());
    auto handle = std::make_unique<fmicontext>(fmuCtx.get(), std::move(temp), std::move(loader));
    fmuCtx.release();

    switch (version) {
        case fmiVersion1:
            return std::make_unique<fmi1_fmu>(std::move(handle), fmiLogging);
        case fmiVersion2:
            return std::make_unique<fmi2_fmu>(std::move(handle), fmiLogging);
        case fmiVersion3:
            return std::make_unique<fmi3_fmu>(std::move(handle), fmiLogging);
        default:
            throw std::runtime_error("Unsupported FMI version in FMU '" + fmuFile + "'");
    }
}
