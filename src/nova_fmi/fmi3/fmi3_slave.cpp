
#include "fmi3_slave.hpp"

#include <fmi4c.h>

#include <cstdarg>
#include <memory>

namespace
{

const char* fmi3StatusToString(fmi3Status status) {
    switch (status) {
        case fmi3OK:      return "OK";
        case fmi3Warning: return "Warning";
        case fmi3Discard: return "Discard";
        case fmi3Error:   return "Error";
        case fmi3Fatal:   return "Fatal";
        default:          return "Unknown";
    }
}

void loggerFmi3(fmi3InstanceEnvironment c,
    fmi3Status status,
    fmi3String /*category*/,
    fmi3String message)
{

    const auto slave = static_cast<nova_fmi::fmi3_slave*>(c);

    std::ostringstream ss;
    ss << "[" << slave->instanceName << "] " << fmi3StatusToString(status) << " " << message << "\n";

    nova::log::debug(ss.str());
}

} // namespace

namespace nova_fmi
{

typedef void* (*fmi3InstantiateCoSimulationTYPE)(
    fmi3String instanceName,
    fmi3String instantiationToken,
    fmi3String resourcePath,
    fmi3Boolean visible,
    fmi3Boolean loggingOn,
    fmi3Boolean eventModeUsed,
    fmi3Boolean earlyReturnAllowed,
    const fmi3ValueReference requiredIntermediateVariables[],
    size_t nRequiredIntermediateVariables,
    fmi3InstanceEnvironment instanceEnvironment,
    fmi3LogMessageCallback logMessage,
    fmi3IntermediateUpdateCallback intermediateUpdate);

typedef void (*fmi3FreeInstanceTYPE)(void*);

fmi3_slave::fmi3_slave(
    const std::shared_ptr<fmicontext>& ctx,
    const std::string& instanceName,
    model_description md,
    bool fmiLogging)
    : slave(instanceName)
    , ctx_(ctx)
    , md_(std::move(md))
{
    auto fmiInstantiateCoSimulation = ctx_->get_dll()->get_function<fmi3InstantiateCoSimulationTYPE>("fmi3InstantiateCoSimulation");
    
    std::string resourceLocation = "file:///" + (ctx_->get_unzipped_path() / "resources").string();

    void* raw_component = fmiInstantiateCoSimulation(
        instanceName.c_str(),
        fmi3_instantiationToken(ctx_->get()),
        resourceLocation.c_str(),
        fmi3False,
        fmiLogging ? fmi3True : fmi3False,
        fmi3False,
        fmi3False,
        nullptr,
        0,
        this,
        loggerFmi3,
        nullptr);

    if (!raw_component) {
        throw std::runtime_error(std::string("Failed to instantiate fmi3 slave!"));
    }

    instance_ = std::shared_ptr<void>(raw_component, [dll = ctx_->get_dll()](void* p) {
        if (p) {
            auto freeInstance = dll->get_function<fmi3FreeInstanceTYPE>("fmi3FreeInstance");
            freeInstance(p);
        }
    });

    fmi4c_handle_.component = raw_component;
    fmi4c_handle_.fmu = ctx_->get();
    component_ = &fmi4c_handle_;
}

const model_description& fmi3_slave::get_model_description() const
{
    return md_;
}

bool fmi3_slave::enter_initialization_mode(double start_time, double stop_time, double tolerance)
{
    auto enterInit = ctx_->get_dll()->get_function<fmi3EnterInitializationMode_t>("fmi3EnterInitializationMode");
    const auto status = enterInit(reinterpret_cast<fmi3Component*>(instance_.get()),
        tolerance > 0,
        tolerance,
        start_time,
        stop_time > 0,
        stop_time);
    return status == fmi3OK;
}

bool fmi3_slave::exit_initialization_mode()
{
    auto exitInit = ctx_->get_dll()->get_function<fmi3ExitInitializationMode_t>("fmi3ExitInitializationMode");
    const auto status = exitInit(reinterpret_cast<fmi3Component*>(instance_.get()));
    return status == fmi3OK;
}

bool fmi3_slave::step(double current_time, double step_size)
{
    bool eventEncountered{};
    bool terminateSimulation{};
    bool earlyReturn{};
    double lastTime{};
    fmi3Boolean eventEncountered_fmi = fmi3False;
    fmi3Boolean terminateSimulation_fmi = fmi3False;
    fmi3Boolean earlyReturn_fmi = fmi3False;
    
    auto doStep = ctx_->get_dll()->get_function<fmi3DoStep_t>("fmi3DoStep");
    const auto status = doStep(
        reinterpret_cast<fmi3Component*>(instance_.get()),
        current_time,
        step_size,
        fmi3True,
        &eventEncountered_fmi,
        &terminateSimulation_fmi,
        &earlyReturn_fmi,
        &lastTime);
    return (status == fmi3OK || status == fmi3Warning) && !earlyReturn_fmi && !terminateSimulation_fmi;
}

bool fmi3_slave::terminate()
{
    auto terminateFunc = ctx_->get_dll()->get_function<fmi3Terminate_t>("fmi3Terminate");
    const auto status = terminateFunc(reinterpret_cast<fmi3Component*>(instance_.get()));
    return status == fmi3OK;
}

bool fmi3_slave::reset()
{
    auto resetFunc = ctx_->get_dll()->get_function<fmi3Reset_t>("fmi3Reset");
    const auto status = resetFunc(reinterpret_cast<fmi3Component*>(instance_.get()));
    return status == fmi3OK;
}

bool fmi3_slave::get_integer(const std::vector<value_ref>& vr, std::vector<int32_t>& values)
{
    fmi3Status status;
    const auto ref = fmi3_getVariableByValueReference(ctx_->get(), vr.front());
    switch (fmi3_getVariableDataType(ref)) {
        case fmi3DataTypeInt8: {
            auto getInt8 = ctx_->get_dll()->get_function<fmi3GetInt8_t>("fmi3GetInt8");
            status = getInt8(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), reinterpret_cast<fmi3Int8*>(values.data()), values.size());
            break;
        }
        case fmi3DataTypeInt16: {
            auto getInt16 = ctx_->get_dll()->get_function<fmi3GetInt16_t>("fmi3GetInt16");
            status = getInt16(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), reinterpret_cast<fmi3Int16*>(values.data()), values.size());
            break;
        }
        case fmi3DataTypeInt32: {
            auto getInt32 = ctx_->get_dll()->get_function<fmi3GetInt32_t>("fmi3GetInt32");
            status = getInt32(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), values.data(), values.size());
            break;
        }
        case fmi3DataTypeInt64: {
            auto getInt64 = ctx_->get_dll()->get_function<fmi3GetInt64_t>("fmi3GetInt64");
            status = getInt64(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), reinterpret_cast<fmi3Int64*>(values.data()), values.size());
            break;
        }
        default:
            return false;
    }

    return status == fmi3OK;
}

bool fmi3_slave::get_real(const std::vector<value_ref>& vr, std::vector<double>& values)
{

    fmi3Status status;
    const auto ref = fmi3_getVariableByValueReference(ctx_->get(), vr.front());
    switch (fmi3_getVariableDataType(ref)) {
        case fmi3DataTypeFloat32: {
            auto getFloat32 = ctx_->get_dll()->get_function<fmi3GetFloat32_t>("fmi3GetFloat32");
            status = getFloat32(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), reinterpret_cast<fmi3Float32*>(values.data()), values.size());
            break;
        }
        case fmi3DataTypeFloat64: {
            auto getFloat64 = ctx_->get_dll()->get_function<fmi3GetFloat64_t>("fmi3GetFloat64");
            status = getFloat64(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), values.data(), values.size());
            break;
        }
        default:
            return false;
    }

    return status == fmi3OK;
}

bool fmi3_slave::get_string(const std::vector<value_ref>& vr, std::vector<std::string>& values)
{
    auto tmp = std::vector<fmi3String>(vr.size());
    auto getString = ctx_->get_dll()->get_function<fmi3GetString_t>("fmi3GetString");
    const auto status = getString(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), tmp.data(), tmp.size());
    for (auto i = 0; i < tmp.size(); i++) {
        values[i] = tmp[i];
    }
    return status == fmi3OK;
}

bool fmi3_slave::get_boolean(const std::vector<value_ref>& vr, std::vector<bool>& values)
{
    fmi3Boolean* tmp = new fmi3Boolean[values.size()];
    auto getBoolean = ctx_->get_dll()->get_function<fmi3GetBoolean_t>("fmi3GetBoolean");
    const auto status = getBoolean(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), tmp, values.size());
    for (auto i = 0; i < vr.size(); i++) {
        values[i] = tmp[i] != 0;
    }
    delete[] tmp;
    return status == fmi3OK;
}
bool fmi3_slave::get_vector(const std::vector<value_ref>& vrs, std::vector<std::vector<double>>& values, int len) { return 0; }

bool fmi3_slave::set_integer(const std::vector<value_ref>& vr, const std::vector<int32_t>& values)
{
    fmi3Status status;
    const auto ref = fmi3_getVariableByValueReference(ctx_->get(), vr.front());
    switch (fmi3_getVariableDataType(ref)) {
        case fmi3DataTypeInt8: {
            std::vector<fmi3Int8> int8Values(values.size());
            std::ranges::transform(values, int8Values.begin(),
                [](int32_t val) { return static_cast<fmi3Int8>(val); });
            auto setInt8 = ctx_->get_dll()->get_function<fmi3SetInt8_t>("fmi3SetInt8");
            status = setInt8(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), int8Values.data(), values.size());
        } break;
        case fmi3DataTypeInt16: {
            std::vector<fmi3Int16> int16Values(values.size());
            std::ranges::transform(values, int16Values.begin(),
                [](int32_t val) { return static_cast<fmi3Int16>(val); });
            auto setInt16 = ctx_->get_dll()->get_function<fmi3SetInt16_t>("fmi3SetInt16");
            status = setInt16(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), int16Values.data(), values.size());
        } break;
        case fmi3DataTypeInt32: {
            auto setInt32 = ctx_->get_dll()->get_function<fmi3SetInt32_t>("fmi3SetInt32");
            status = setInt32(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), values.data(), values.size());
        } break;
        case fmi3DataTypeInt64: {
            std::vector<fmi3Int64> int64Values(values.size());
            std::ranges::transform(values, int64Values.begin(),
                [](int64_t val) { return static_cast<fmi3Int16>(val); });
            auto setInt64 = ctx_->get_dll()->get_function<fmi3SetInt64_t>("fmi3SetInt64");
            status = setInt64(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), int64Values.data(), values.size());
        } break;
        case fmi3DataTypeUInt8: {
            std::vector<fmi3UInt8> uInt8Values(values.size());
            std::ranges::transform(values, uInt8Values.begin(),
                [](int32_t val) { return static_cast<fmi3Int8>(val); });
            auto setUInt8 = ctx_->get_dll()->get_function<fmi3SetUInt8_t>("fmi3SetUInt8");
            status = setUInt8(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), uInt8Values.data(), values.size());
        } break;
        case fmi3DataTypeUInt16: {
            std::vector<fmi3UInt16> uInt16Values(values.size());
            std::ranges::transform(values, uInt16Values.begin(),
                [](int32_t val) { return static_cast<fmi3Int16>(val); });
            auto setUInt16 = ctx_->get_dll()->get_function<fmi3SetUInt16_t>("fmi3SetUInt16");
            status = setUInt16(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), uInt16Values.data(), values.size());
        } break;
        case fmi3DataTypeUInt32: {
            std::vector<fmi3UInt32> uInt32Values(values.size());
            std::ranges::transform(values, uInt32Values.begin(),
                [](int32_t val) { return static_cast<fmi3Int16>(val); });
            auto setUInt32 = ctx_->get_dll()->get_function<fmi3SetUInt32_t>("fmi3SetUInt32");
            status = setUInt32(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), uInt32Values.data(), values.size());
        } break;
        case fmi3DataTypeUInt64: {
            std::vector<fmi3UInt64> uInt64Values(values.size());
            std::ranges::transform(values, uInt64Values.begin(),
                [](int64_t val) { return static_cast<fmi3Int16>(val); });
            auto setUInt64 = ctx_->get_dll()->get_function<fmi3SetUInt64_t>("fmi3SetUInt64");
            status = setUInt64(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), uInt64Values.data(), values.size());
        } break;
        default:
            return false;
    }

    return status == fmi3OK;
}

bool fmi3_slave::set_real(const std::vector<value_ref>& vr, const std::vector<double>& values)
{
    fmi3Status status;
    const auto ref = fmi3_getVariableByValueReference(ctx_->get(), vr.front());
    switch (fmi3_getVariableDataType(ref)) {
        case fmi3DataTypeFloat32: {
            std::vector<fmi3Float32> float32Values(values.size());
            std::ranges::transform(values, float32Values.begin(),
                [](double val) { return static_cast<fmi3Float32>(val); });
            auto setFloat32 = ctx_->get_dll()->get_function<fmi3SetFloat32_t>("fmi3SetFloat32");
            status = setFloat32(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), float32Values.data(), float32Values.size());
        } break;
        case fmi3DataTypeFloat64: {
            auto setFloat64 = ctx_->get_dll()->get_function<fmi3SetFloat64_t>("fmi3SetFloat64");
            status = setFloat64(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), values.data(), values.size());
        } break;
        default:
            return false;
    }
    return status == fmi3OK;
}

bool fmi3_slave::set_string(const std::vector<value_ref>& vr, const std::vector<std::string>& values)
{
    std::vector<fmi3String> _values(vr.size());
    for (auto i = 0; i < vr.size(); i++) {
        _values[i] = values[i].c_str();
    }
    auto setString = ctx_->get_dll()->get_function<fmi3SetString_t>("fmi3SetString");
    const auto status = setString(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), _values.data(), _values.size());
    return status == fmi3OK;
}

bool fmi3_slave::set_boolean(const std::vector<value_ref>& vr, const std::vector<bool>& values)
{
    fmi3Boolean* tmp = new fmi3Boolean[values.size()];
    for (auto i = 0; i < values.size(); i++) {
        tmp[i] = values[i] ? fmi3True : fmi3False;
    }
    auto setBoolean = ctx_->get_dll()->get_function<fmi3SetBoolean_t>("fmi3SetBoolean");
    const auto status = setBoolean(reinterpret_cast<fmi3Component*>(instance_.get()), vr.data(), vr.size(), tmp, values.size());
    delete[] tmp;
    return status == fmi3OK;
}
bool fmi3_slave::set_vector(const std::vector<value_ref>& vrs, const std::vector<std::vector<double>>& values) { return 0; }
void fmi3_slave::freeInstance()
{
    if (instance_) {
        instance_.reset();
    }
}

void* fmi3_slave::get_state()
{
    if (!fmi3cs_getCanGetAndSetFMUState(ctx_->get())) {
        throw std::runtime_error("This instance cannot get and set FMU state: " + instanceName);
    }
    void* state = nullptr;
    auto getFMUState = ctx_->get_dll()->get_function<fmi3GetFMUState_t>("fmi3GetFMUState");
    getFMUState(reinterpret_cast<fmi3Component*>(instance_.get()), &state);

    return state;
}

bool fmi3_slave::free_state(void* state)
{
    auto freeFMUState = ctx_->get_dll()->get_function<fmi3FreeFMUState_t>("fmi3FreeFMUState");
    return freeFMUState(reinterpret_cast<fmi3Component*>(instance_.get()), &state) == fmi3OK;
}

bool fmi3_slave::set_state(void* state)
{
    auto setFMUState = ctx_->get_dll()->get_function<fmi3SetFMUState_t>("fmi3SetFMUState");
    return setFMUState(reinterpret_cast<fmi3Component*>(instance_.get()), state) == fmi3OK;
}

fmi3_slave::~fmi3_slave()
{
    fmi3_slave::freeInstance();
}

} // namespace nova_fmi
