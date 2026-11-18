
#include "fmi2_slave.hpp"

#include <fmi4c.h>

#include <cstdarg>
#include <iostream>
#include <memory>
#include <sstream>

namespace
{

const char* fmi2StatusToString(fmi2Status status)
{
    switch (status) {
        case fmi2OK: return "fmi2OK";
        case fmi2Warning: return "fmi2Warning";
        case fmi2Discard: return "fmi2Discard";
        case fmi2Error: return "fmi2Error";
        case fmi2Fatal: return "fmi2Fatal";
        case fmi2Pending: return "fmi2Pending";
        default: return "Unknown fmi2Status";
    }
}

void fmilogger(fmi2Component, fmi2String instanceName, fmi2Status status, fmi2String, fmi2String message, ...)
{
    va_list args;
    va_start(args, message);
    char formatted[1024];
    vsnprintf(formatted, sizeof(formatted), message, args);
    va_end(args);

    std::ostringstream ss;
    ss << "[" << instanceName << "] " << fmi2StatusToString(status) << " " << formatted << "\n";

    nova::log::debug(ss.str());
}

void noopfmilogger(fmi2Component, fmi2String, fmi2Status, fmi2String, fmi2String, ...)
{
}

} // namespace

namespace nova_fmi
{

typedef void* (*fmi2InstantiateTYPE)(fmi2String, fmi2Type, fmi2String, fmi2String,
                                     const fmi2CallbackFunctions*, fmi2Boolean, fmi2Boolean);
typedef void (*fmi2FreeInstanceTYPE)(void*);

fmi2_slave::fmi2_slave(
    const std::shared_ptr<fmicontext>& ctx,
    const std::string& instanceName,
    model_description md,
    bool fmiLogging)
    : slave(instanceName)
    , ctx_(ctx)
    , md_(std::move(md))
{
    callbacks_.logger = fmiLogging ? &fmilogger : &noopfmilogger;
    callbacks_.allocateMemory = std::calloc;
    callbacks_.freeMemory = std::free;
    callbacks_.stepFinished = nullptr;
    callbacks_.componentEnvironment = nullptr;

    auto fmi2Instantiate = ctx_->get_dll()->get_function<fmi2InstantiateTYPE>("fmi2Instantiate");
    
    std::string resourceLocation = "file:///" + (ctx_->get_unzipped_path() / "resources").string();

    fmi2Component raw_component = fmi2Instantiate(
        instanceName.c_str(),
        fmi2CoSimulation,
        fmi2_getGuid(ctx_->get()),
        resourceLocation.c_str(),
        &callbacks_,
        fmi2False,
        fmiLogging ? fmi2True : fmi2False);

    if (!raw_component) {
        throw std::runtime_error(std::string("Failed to instantiate fmi2 slave!"));
    }

    instance_ = std::shared_ptr<void>(raw_component, [dll = ctx_->get_dll()](void* p) {
        if (p) {
            auto freeInstance = dll->get_function<fmi2FreeInstanceTYPE>("fmi2FreeInstance");
            freeInstance(p);
        }
    });

    fmi4c_handle_.component = raw_component;
    fmi4c_handle_.fmu = ctx_->get();
    component = &fmi4c_handle_;
}

const model_description& fmi2_slave::get_model_description() const
{
    return md_;
}

bool fmi2_slave::enter_initialization_mode(double start_time, double stop_time, double tolerance)
{
    fmi2Boolean stop_defined = (stop_time > 0) ? fmi2True : fmi2False;
    fmi2Boolean tolerance_defined = (tolerance > 0) ? fmi2True : fmi2False;
    
    auto setupExperiment = ctx_->get_dll()->get_function<fmi2SetupExperiment_t>("fmi2SetupExperiment");
    const auto status1 = setupExperiment(reinterpret_cast<fmi2Component*>(instance_.get()), tolerance_defined, tolerance, start_time, stop_defined, stop_time);

    if (status1 != fmi2OK) {
        return false;
    }

    auto enterInit = ctx_->get_dll()->get_function<fmi2EnterInitializationMode_t>("fmi2EnterInitializationMode");
    const auto status2 = enterInit(reinterpret_cast<fmi2Component*>(instance_.get()));
    return status2 == fmi2OK;
}

bool fmi2_slave::exit_initialization_mode()
{
    auto exitInit = ctx_->get_dll()->get_function<fmi2ExitInitializationMode_t>("fmi2ExitInitializationMode");
    const auto status = exitInit(reinterpret_cast<fmi2Component*>(instance_.get()));
    return status == fmi2OK;
}

bool fmi2_slave::step(double current_time, double step_size)
{
    auto doStep = ctx_->get_dll()->get_function<fmi2DoStep_t>("fmi2DoStep");
    const auto status = doStep(reinterpret_cast<fmi2Component*>(instance_.get()), current_time, step_size, fmi2True);
    return status == fmi2OK || status == fmi2Warning;
}

bool fmi2_slave::terminate()
{
    auto terminateFunc = ctx_->get_dll()->get_function<fmi2Terminate_t>("fmi2Terminate");
    const auto status = terminateFunc(reinterpret_cast<fmi2Component*>(instance_.get()));
    return status == fmi2OK;
}

bool fmi2_slave::reset()
{
    auto resetFunc = ctx_->get_dll()->get_function<fmi2Reset_t>("fmi2Reset");
    const auto status = resetFunc(reinterpret_cast<fmi2Component*>(instance_.get()));
    return status == fmi2OK;
}

bool fmi2_slave::get_integer(const std::vector<value_ref>& vr, std::vector<int32_t>& values)
{
    auto getInt = ctx_->get_dll()->get_function<fmi2GetInteger_t>("fmi2GetInteger");
    const auto status = getInt(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), values.data());
    return status == fmi2OK;
}

bool fmi2_slave::get_real(const std::vector<value_ref>& vr, std::vector<double>& values)
{
    auto getReal = ctx_->get_dll()->get_function<fmi2GetReal_t>("fmi2GetReal");
    const auto status = getReal(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), values.data());
    return status == fmi2OK;
}

bool fmi2_slave::get_string(const std::vector<value_ref>& vr, std::vector<std::string>& values)
{
    auto tmp = std::vector<fmi2String>(vr.size());
    auto getString = ctx_->get_dll()->get_function<fmi2GetString_t>("fmi2GetString");
    const auto status = getString(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), tmp.data());
    for (auto i = 0; i < tmp.size(); i++) {
        values[i] = tmp[i];
    }
    return status == fmi2OK;
}

bool fmi2_slave::get_boolean(const std::vector<value_ref>& vr, std::vector<bool>& values)
{
    auto tmp = std::vector<fmi2Boolean>(vr.size());
    auto getBool = ctx_->get_dll()->get_function<fmi2GetBoolean_t>("fmi2GetBoolean");
    const auto status = getBool(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), tmp.data());
    for (auto i = 0; i < tmp.size(); i++) {
        values[i] = tmp[i] != 0;
    }
    return status == fmi2OK;
}

bool fmi2_slave::get_vector(const std::vector<value_ref>& vr, std::vector<std::vector<double>>& values, fmi2Integer len)
{
    // Note: getVector might be a custom FMI4C extension, checking fmi2GetVector
    auto tmp = std::vector<fmi2Vector>(vr.size());
    auto getVec = ctx_->get_dll()->get_function<fmi2GetVector_t>("fmi2GetVector");
    const auto status = getVec(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), tmp.data());
    values.resize(vr.size());
    for (auto i = 0; i < tmp.size(); i++) {
        values[i] = std::vector<double>(tmp[i], tmp[i]+len);
    }
    return status == fmi2OK;
}

bool fmi2_slave::set_integer(const std::vector<value_ref>& vr, const std::vector<int>& values)
{
    auto setInt = ctx_->get_dll()->get_function<fmi2SetInteger_t>("fmi2SetInteger");
    const auto status = setInt(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), values.data());
    return status == fmi2OK;
}

bool fmi2_slave::set_real(const std::vector<value_ref>& vr, const std::vector<double>& values)
{
    auto setReal = ctx_->get_dll()->get_function<fmi2SetReal_t>("fmi2SetReal");
    const auto status = setReal(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), values.data());
    return status == fmi2OK;
}

bool fmi2_slave::set_string(const std::vector<value_ref>& vr, const std::vector<std::string>& values)
{
    std::vector<fmi2String> _values(vr.size());
    for (auto i = 0; i < vr.size(); i++) {
        _values[i] = values[i].c_str();
    }
    auto setStr = ctx_->get_dll()->get_function<fmi2SetString_t>("fmi2SetString");
    const auto status = setStr(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), _values.data());
    return status == fmi2OK;
}

bool fmi2_slave::set_boolean(const std::vector<value_ref>& vr, const std::vector<bool>& values)
{
    std::vector<fmi2Boolean> _values(vr.size());
    for (auto i = 0; i < vr.size(); i++) {
        _values[i] = values[i] ? fmi2True : fmi2False;
    }
    auto setBool = ctx_->get_dll()->get_function<fmi2SetBoolean_t>("fmi2SetBoolean");
    const auto status = setBool(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), _values.data());
    return status == fmi2OK;
}

bool fmi2_slave::set_vector(const std::vector<value_ref>& vr, const std::vector<std::vector<double>>& values)
{
    std::vector<fmi2Vector> _values(vr.size());
    std::vector<fmi2Integer> _length(vr.size());

    for (auto i = 0; i < vr.size(); i++) {
        _values[i] = values[i].data();
        _length[i] = static_cast<fmi2Integer>(values[i].size());
    }
    auto setVec = ctx_->get_dll()->get_function<fmi2SetVector_t>("fmi2SetVector");
    const auto status = setVec(reinterpret_cast<fmi2Component*>(instance_.get()), vr.data(), vr.size(), _values.data(),_length.data());
    return status == fmi2OK;
}

void fmi2_slave::freeInstance()
{
    if (instance_) {
        instance_.reset();
    }
}

void* fmi2_slave::get_state()
{

    if (!fmi2cs_getCanGetAndSetFMUState(ctx_->get())) {
        throw std::runtime_error("This instance cannot get and set FMU state: " + instanceName);
    }

    void* state = nullptr;
    auto getFMUstate = ctx_->get_dll()->get_function<fmi2GetFMUstate_t>("fmi2GetFMUstate");
    getFMUstate(reinterpret_cast<fmi2Component*>(instance_.get()), &state);

    return state;
}

bool fmi2_slave::set_state(void* state)
{
    auto setFMUstate = ctx_->get_dll()->get_function<fmi2SetFMUstate_t>("fmi2SetFMUstate");
    return setFMUstate(reinterpret_cast<fmi2Component*>(instance_.get()), state) == fmi2OK;
}

bool fmi2_slave::free_state(void* state)
{
    auto freeFMUstate = ctx_->get_dll()->get_function<fmi2FreeFMUstate_t>("fmi2FreeFMUstate");
    return freeFMUstate(reinterpret_cast<fmi2Component*>(instance_.get()), &state) == fmi2OK;
}

fmi2_slave::~fmi2_slave()
{
    fmi2_slave::freeInstance();
}

} // namespace nova_fmi
