
#include "fmi1_slave.hpp"

#include "nova/components/logger/logger.hpp"

#include <fmi4c.h>

#include <cstdarg>
#include <iostream>
#include <sstream>
#include <utility>

namespace
{

const char* fmi1StatusToString(fmi1Status status)
{
    switch (status) {
        case fmi1OK: return "OK";
        case fmi1Warning: return "Warning";
        case fmi1Discard: return "Discard";
        case fmi1Error: return "Error";
        case fmi1Fatal: return "Fatal";
        case fmi1Pending: return "Pending";
        default: return "Unknown";
    }
}

void fmilogger(fmi1Component* /*c*/, fmi1String instanceName, fmi1Status status, fmi1String /*category*/, fmi1String message, ...)
{
    va_list args;
    va_start(args, message);
    char formatted[1024];
    vsnprintf(formatted, sizeof(formatted), message, args);
    va_end(args);

    std::ostringstream ss;
    ss << "[" << instanceName << "] " << fmi1StatusToString(status) << " " << formatted << "\n";

    nova::log::debug(ss.str());
}

void noopfmilogger(fmi1Component*, fmi1String, fmi1Status, fmi1String, fmi1String, ...)
{
}

} // namespace


namespace nova_fmi
{

typedef void* (*fmi1InstantiateSlaveTYPE)(
    fmi1String instanceName, fmi1String fmuGUID, fmi1String fmuLocation, fmi1String mimeType,
    fmi1Real timeout, fmi1Boolean visible, fmi1Boolean interactive,
    fmi1CallbackFunctionsCoSimulation functions, fmi1Boolean loggingOn);

typedef void (*fmi1FreeSlaveInstanceTYPE)(void*);

fmi1_slave::fmi1_slave(
    const std::shared_ptr<fmicontext>& ctx,
    const std::string& instanceName,
    model_description md,
    bool fmiLogging)
    : slave(instanceName)
    , ctx_(ctx)
    , md_(std::move(md))
{
    callbacks_.logger = fmiLogging ? fmilogger : noopfmilogger;
    callbacks_.allocateMemory = std::calloc;
    callbacks_.freeMemory = std::free;
    callbacks_.stepFinished = nullptr;

    std::string prefix = md_.modelIdentifier + "_";
    auto fmiInstantiateSlave = ctx_->get_dll()->get_function<fmi1InstantiateSlaveTYPE>(prefix + "fmiInstantiateSlave");

    std::string resourceLocation = "file:///" + (ctx_->get_unzipped_path() / "resources").string();

    fmi1Component raw_component = fmiInstantiateSlave(
        instanceName.c_str(),
        fmi1_getGuid(ctx_->get()),
        resourceLocation.c_str(),
        "application/x-fmu-sharedlibrary",
        1000,
        fmi1False,
        fmi1False,
        callbacks_,
        fmiLogging ? fmi1True : fmi1False);

    if (!raw_component) {
        throw std::runtime_error(std::string("Failed to instantiate fmi1 slave!"));
    }

    instance_ = std::shared_ptr<void>(raw_component, [dll = ctx_->get_dll(), prefix](void* p) {
        if (p) {
            auto freeInstance = dll->get_function<fmi1FreeSlaveInstanceTYPE>(prefix + "fmiFreeSlaveInstance");
            freeInstance(p);
        }
    });

    fmi4c_handle_.component = raw_component;
    fmi4c_handle_.fmu = ctx_->get();
    component_ = &fmi4c_handle_;
}

const model_description& fmi1_slave::get_model_description() const
{
    return md_;
}

bool fmi1_slave::enter_initialization_mode(double start_time, double stop_time, double /*tolerance*/)
{
    start_time_ = start_time;
    stop_time_ = stop_time;

    return true;
}

bool fmi1_slave::exit_initialization_mode()
{
    const fmi1Boolean stop_defined = (stop_time_ > 0) ? fmi1True : fmi1False;
    auto initSlave = ctx_->get_dll()->get_function<fmiInitializeSlave_t>(md_.modelIdentifier + "_fmiInitializeSlave");
    const auto status = initSlave(reinterpret_cast<fmi1Component*>(instance_.get()), start_time_, stop_defined, stop_time_);
    return status == fmi1OK;
}

bool fmi1_slave::step(double current_time, double step_size)
{
    auto doStep = ctx_->get_dll()->get_function<fmiDoStep_t>(md_.modelIdentifier + "_fmiDoStep");
    const auto status = doStep(reinterpret_cast<fmi1Component*>(instance_.get()), current_time, step_size, fmi1True);
    return status == fmi1OK || status == fmi1Warning;
}

bool fmi1_slave::terminate()
{
    auto terminateSlave = ctx_->get_dll()->get_function<fmiTerminateSlave_t>(md_.modelIdentifier + "_fmiTerminateSlave");
    const auto status = terminateSlave(reinterpret_cast<fmi1Component*>(instance_.get()));
    return status == fmi1OK;
}

bool fmi1_slave::reset()
{
    auto resetSlave = ctx_->get_dll()->get_function<fmiResetSlave_t>(md_.modelIdentifier + "_fmiResetSlave");
    const auto status = resetSlave(reinterpret_cast<fmi1Component*>(instance_.get()));
    return status == fmi1OK;
}

bool fmi1_slave::get_integer(const std::vector<value_ref>& vr, std::vector<int32_t>& values)
{
    auto getInteger = ctx_->get_dll()->get_function<fmiGetInteger_t>(md_.modelIdentifier + "_fmiGetInteger");
    const auto status = getInteger(reinterpret_cast<fmi1Component*>(instance_.get()), vr.data(), vr.size(), values.data());
    return status == fmi1OK;
}

bool fmi1_slave::get_real(const std::vector<value_ref>& vr, std::vector<double>& values)
{
    auto getReal = ctx_->get_dll()->get_function<fmiGetReal_t>(md_.modelIdentifier + "_fmiGetReal");
    const auto status = getReal(reinterpret_cast<fmi1Component*>(instance_.get()), vr.data(), vr.size(), values.data());
    return status == fmi1OK;
}

bool fmi1_slave::get_string(const std::vector<value_ref>& vr, std::vector<std::string>& values)
{
    auto tmp = std::vector<fmi1String>(vr.size());
    auto getString = ctx_->get_dll()->get_function<fmiGetString_t>(md_.modelIdentifier + "_fmiGetString");
    const auto status = getString(reinterpret_cast<fmi1Component*>(instance_.get()), vr.data(), vr.size(), tmp.data());
    for (auto i = 0; i < tmp.size(); i++) {
        values[i] = tmp[i];
    }
    return status == fmi1OK;
}

bool fmi1_slave::get_boolean(const std::vector<value_ref>& vr, std::vector<bool>& values)
{
    auto tmp = std::vector<fmi1Boolean>(vr.size());
    auto getBoolean = ctx_->get_dll()->get_function<fmiGetBoolean_t>(md_.modelIdentifier + "_fmiGetBoolean");
    const auto status = getBoolean(reinterpret_cast<fmi1Component*>(instance_.get()), vr.data(), vr.size(), tmp.data());
    for (auto i = 0; i < tmp.size(); i++) {
        values[i] = tmp[i] != 0;
    }
    return status == fmi1OK;
}

bool fmi1_slave::get_vector(const std::vector<value_ref>& vrs, std::vector<std::vector<double>>& values, int len) { return 0; }

bool fmi1_slave::set_integer(const std::vector<value_ref>& vr, const std::vector<int32_t>& values)
{
    auto setInteger = ctx_->get_dll()->get_function<fmiSetInteger_t>(md_.modelIdentifier + "_fmiSetInteger");
    const auto status = setInteger(reinterpret_cast<fmi1Component*>(instance_.get()), vr.data(), vr.size(), values.data());
    return status == fmi1OK;
}

bool fmi1_slave::set_real(const std::vector<value_ref>& vr, const std::vector<double>& values)
{
    auto setReal = ctx_->get_dll()->get_function<fmiSetReal_t>(md_.modelIdentifier + "_fmiSetReal");
    const auto status = setReal(reinterpret_cast<fmi1Component*>(instance_.get()), vr.data(), vr.size(), values.data());
    return status == fmi1OK;
}

bool fmi1_slave::set_string(const std::vector<value_ref>& vr, const std::vector<std::string>& values)
{
    std::vector<fmi1String> _values(vr.size());
    for (auto i = 0; i < vr.size(); i++) {
        _values[i] = values[i].c_str();
    }
    auto setString = ctx_->get_dll()->get_function<fmiSetString_t>(md_.modelIdentifier + "_fmiSetString");
    const auto status = setString(reinterpret_cast<fmi1Component*>(instance_.get()), vr.data(), vr.size(), _values.data());
    return status == fmi1OK;
}

bool fmi1_slave::set_boolean(const std::vector<value_ref>& vr, const std::vector<bool>& values)
{
    std::vector<fmi1Boolean> _values(vr.size());
    for (auto i = 0; i < vr.size(); i++) {
        _values[i] = values[i] ? fmi1True : fmi1False;
    }
    auto setBoolean = ctx_->get_dll()->get_function<fmiSetBoolean_t>(md_.modelIdentifier + "_fmiSetBoolean");
    const auto status = setBoolean(reinterpret_cast<fmi1Component*>(instance_.get()), vr.data(), vr.size(), _values.data());
    return status == fmi1OK;
}

bool fmi1_slave::set_vector(const std::vector<value_ref>& vrs, const std::vector<std::vector<double>>& values) { return 0; }

void fmi1_slave::freeInstance()
{
    if (instance_) {
        instance_.reset();
    }
}

fmi1_slave::~fmi1_slave()
{
    fmi1_slave::freeInstance();
}

} // namespace nova_fmi
