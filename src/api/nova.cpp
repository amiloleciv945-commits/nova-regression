#define NOVA_EXPORT_DLL
#include "nova/api/nova.h"
#include "nova/components/structure/simulation_structure.hpp"
#include "nova/components/algorithm/fixed_step_algorithm.hpp"
#include "nova/components/algorithm/gauss_seidel_algorithm.hpp"
#include "nova/components/algorithm/strong_coupling_algorithm.hpp"
#include "nova/components/recorder/csv_recorder.hpp"
#include "nova/components/logger/logger.hpp"
#include "nova/api/lib_info.hpp"
#include "nova/components/util/chart_plotter.hpp"
#include "nova/components/ssp/ssp_loader.hpp"
#include "nova/engine/engine_scheduler.hpp"
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace nova;

struct nova_simulation_structure_t {
    std::unique_ptr<simulation_structure> ss;
};

struct nova_engine_t {
    std::unique_ptr<nova_engine> sim;
};

struct nova_parameter_set_t {
    std::map<variable_identifier, scalar_value> params;
};

struct nova_csv_recorder_t {
    std::shared_ptr<csv_recorder> writer;
};

struct nova_scheduler_t {
    std::unique_ptr<engine_scheduler> runner;
};

static thread_local std::string g_last_error_msg;

extern "C" NOVA_API nova_simulation_structure_t* nova_simulation_structure_create() {
    auto ss = new nova_simulation_structure_t();
    ss->ss = std::make_unique<simulation_structure>();
    return ss;
}

extern "C" NOVA_API nova_simulation_structure_t* nova_simulation_structure_load_ssp(const char* ssp_path) {
    try {
        auto ss_ptr = load_ssp(std::filesystem::path(ssp_path));
        if (!ss_ptr) {
            g_last_error_msg = "load_ssp returned nullptr";
            return nullptr;
        }
        auto ss = new nova_simulation_structure_t();
        ss->ss = std::move(ss_ptr);
        return ss;
    } catch (const std::exception& e) {
        log::err("Failed to load SSP: {}", e.what());
        g_last_error_msg = e.what();
        return nullptr;
    } catch (...) {
        g_last_error_msg = "Unknown error during SSP load";
        return nullptr;
    }
}

extern "C" NOVA_API void nova_simulation_structure_destroy(nova_simulation_structure_t* ss) {
    delete ss;
}

extern "C" NOVA_API bool nova_simulation_structure_add_model(nova_simulation_structure_t* ss, const char* name, const char* uri) {
    try {
        if (!ss || !ss->ss) {
            g_last_error_msg = "Simulation structure is null";
            return false;
        }
        if (!name || !uri) {
            g_last_error_msg = "Model name and URI are required";
            return false;
        }
        ss->ss->add_model(name, std::string(uri), std::nullopt);
        g_last_error_msg.clear();
        return true;
    } catch (const std::exception& e) {
        g_last_error_msg = e.what();
        return false;
    } catch (...) {
        g_last_error_msg = "Unknown error while adding model";
        return false;
    }
}

extern "C" NOVA_API void nova_simulation_structure_make_connection(nova_simulation_structure_t* ss, const char* src_inst, const char* src_var, const char* dst_inst, const char* dst_var, const char* type) {
    if (std::string(type) == "real")
        ss->ss->make_connection<double>({src_inst, src_var}, {dst_inst, dst_var});
    else if (std::string(type) == "int")
        ss->ss->make_connection<int>({src_inst, src_var}, {dst_inst, dst_var});
    else if (std::string(type) == "bool")
        ss->ss->make_connection<bool>({src_inst, src_var}, {dst_inst, dst_var});
    else if (std::string(type) == "string")
        ss->ss->make_connection<std::string>({src_inst, src_var}, {dst_inst, dst_var});
    else if (std::string(type) == "vector")
        ss->ss->make_connection<std::vector<double>>({src_inst, src_var}, {dst_inst, dst_var});
}

extern "C" NOVA_API void nova_simulation_structure_make_real_connection_mod(nova_simulation_structure_t* ss, const char* src, const char* dst, nova_real_modifier_t modifier) {
    std::function<double(const double&)> mod_fn;
    if (modifier) mod_fn = modifier;
    ss->ss->make_connection<double>(src, dst, modifier ? std::make_optional(mod_fn) : std::nullopt);
}

extern "C" NOVA_API void nova_simulation_structure_make_real_connection(nova_simulation_structure_t* ss, const char* src, const char* dst) {
    nova_simulation_structure_make_real_connection_mod(ss, src, dst, nullptr);
}

extern "C" NOVA_API void nova_simulation_structure_make_int_connection(nova_simulation_structure_t* ss, const char* src, const char* dst) {
    ss->ss->make_connection<int>(src, dst);
}

extern "C" NOVA_API void nova_simulation_structure_make_bool_connection(nova_simulation_structure_t* ss, const char* src, const char* dst) {
    ss->ss->make_connection<bool>(src, dst);
}

extern "C" NOVA_API void nova_simulation_structure_make_string_connection(nova_simulation_structure_t* ss, const char* src, const char* dst) {
    ss->ss->make_connection<std::string>(src, dst);
}

extern "C" NOVA_API void nova_simulation_structure_make_vector_connection(nova_simulation_structure_t* ss, const char* src, const char* dst) {
    ss->ss->make_connection<std::vector<double>>(src, dst);
}

extern "C" NOVA_API nova_parameter_set_t* nova_parameter_set_create() {
    return new nova_parameter_set_t();
}

extern "C" NOVA_API void nova_parameter_set_add_real(nova_parameter_set_t* pps, const char* name, double value) {
    pps->params[{name}] = value;
}

extern "C" NOVA_API void nova_parameter_set_add_int(nova_parameter_set_t* pps, const char* name, int value) {
    pps->params[{name}] = value;
}

extern "C" NOVA_API void nova_parameter_set_add_bool(nova_parameter_set_t* pps, const char* name, bool value) {
    pps->params[{name}] = value;
}

extern "C" NOVA_API void nova_parameter_set_add_string(nova_parameter_set_t* pps, const char* name, const char* value) {
    pps->params[{name}] = std::string(value);
}

extern "C" NOVA_API void nova_parameter_set_destroy(nova_parameter_set_t* pps) {
    delete pps;
}

extern "C" NOVA_API void nova_simulation_structure_add_parameter_set(nova_simulation_structure_t* ss, const char* name, nova_parameter_set_t* pps) {
    ss->ss->add_parameter_set(std::string(name), pps->params);
}

static std::unique_ptr<algorithm> make_algorithm(double step_size, const nova_algorithm_config_t* config) {
    if (!config) {
        return std::make_unique<fixed_step_algorithm>(step_size);
    }
    if (!std::isfinite(step_size) || step_size <= 0.0) {
        throw std::invalid_argument("Step size must be finite and positive");
    }

    switch (config->type) {
    case NOVA_ALGORITHM_FIXED_STEP:
        return std::make_unique<fixed_step_algorithm>(step_size, config->parallel);
    case NOVA_ALGORITHM_GAUSS_SEIDEL: {
        if (!config->order || config->order_size == 0) {
            throw std::invalid_argument("Gauss-Seidel requires an instance order");
        }
        std::vector<std::string> order;
        order.reserve(config->order_size);
        for (std::size_t i = 0; i < config->order_size; ++i) {
            if (!config->order[i]) {
                throw std::invalid_argument("Gauss-Seidel instance names must not be null");
            }
            order.emplace_back(config->order[i]);
        }
        return std::make_unique<gauss_seidel_algorithm>(step_size, std::move(order));
    }
    case NOVA_ALGORITHM_STRONG_COUPLING:
        if (config->output_holder_type < 0 || config->output_holder_type > 3
            || config->input_holder_type < 0 || config->input_holder_type > 3) {
            throw std::invalid_argument("Strong coupling holder types must be between 0 and 3");
        }
        return std::make_unique<strong_coupling_algorithm>(step_size, config->max_iterations,
            config->parallel, config->output_holder_type, config->input_holder_type);
    default:
        throw std::invalid_argument("Unknown Nova algorithm type");
    }
}

extern "C" NOVA_API nova_engine_t* nova_engine_create(nova_simulation_structure_t* ss, double step_size) {
    return nova_engine_create_with_algorithm(ss, step_size, nullptr);
}

extern "C" NOVA_API nova_engine_t* nova_engine_create_with_algorithm(
    nova_simulation_structure_t* ss, double step_size, const nova_algorithm_config_t* config) {
    try {
        if (!ss || !ss->ss) {
            throw std::invalid_argument("Simulation structure is null");
        }
        auto selected = make_algorithm(step_size, config);
        auto* gauss_seidel = dynamic_cast<gauss_seidel_algorithm*>(selected.get());
        auto sim = ss->ss->load(std::move(selected));
        if (gauss_seidel) {
            gauss_seidel->validate_model_order();
        }
        auto* handle = new nova_engine_t{std::move(sim)};
        g_last_error_msg.clear();
        return handle;
    } catch (const std::exception& e) {
        log::err("Exception during nova_engine_create: {}", e.what());
        g_last_error_msg = e.what();
        return nullptr;
    } catch (...) {
        log::err("Unknown exception during nova_engine_create");
        g_last_error_msg = "Unknown exception";
        return nullptr;
    }
}

extern "C" NOVA_API nova_engine_t* nova_engine_create_from_ssp(const char* ssp_path, double step_size) {
    return nova_engine_create_from_ssp_with_algorithm(ssp_path, step_size, nullptr);
}

extern "C" NOVA_API nova_engine_t* nova_engine_create_from_ssp_with_algorithm(
    const char* ssp_path, double step_size, const nova_algorithm_config_t* config) {
    if (!ssp_path) {
        g_last_error_msg = "SSP path is null";
        return nullptr;
    }
    std::unique_ptr<nova_simulation_structure_t> ss(nova_simulation_structure_load_ssp(ssp_path));
    return ss ? nova_engine_create_with_algorithm(ss.get(), step_size, config) : nullptr;
}

extern "C" NOVA_API void nova_engine_destroy(nova_engine_t* sim) {
    delete sim;
}

extern "C" NOVA_API bool nova_engine_load_scenario(nova_engine_t* sim, const char* scenario_file) {
    try {
        sim->sim->load_scenario(std::filesystem::path(scenario_file));
        return true;
    } catch (...) {
        return false;
    }
}

template<class Operation>
static bool run_engine_operation(nova_engine_t* sim, Operation operation, const char* unknown_error) {
    try {
        if (!sim || !sim->sim) {
            throw std::invalid_argument("Simulation engine is null");
        }
        operation(*sim->sim);
        g_last_error_msg.clear();
        return true;
    } catch (const std::exception& e) {
        g_last_error_msg = e.what();
    } catch (...) {
        g_last_error_msg = unknown_error;
    }
    return false;
}

extern "C" NOVA_API bool nova_engine_init(nova_engine_t* sim, double start_time, const char* parameter_set) {
    return run_engine_operation(sim, [&](nova_engine& engine) {
        if (parameter_set && *parameter_set) {
            engine.init(start_time, std::string(parameter_set));
        } else {
            engine.init(start_time, std::nullopt);
        }
    }, "Unknown error during Nova initialization");
}

extern "C" NOVA_API double nova_engine_step(nova_engine_t* sim, unsigned int num_steps) {
    double time = std::numeric_limits<double>::quiet_NaN();
    run_engine_operation(sim, [&](nova_engine& engine) {
        time = engine.step(num_steps);
    }, "Unknown error during Nova step");
    return time;
}

extern "C" NOVA_API void nova_engine_step_until(nova_engine_t* sim, double time_point) {
    run_engine_operation(sim, [&](nova_engine& engine) {
        engine.step_until(time_point);
    }, "Unknown error during Nova step");
}

extern "C" NOVA_API void nova_engine_step_for(nova_engine_t* sim, double duration) {
    run_engine_operation(sim, [&](nova_engine& engine) {
        engine.step_for(duration);
    }, "Unknown error during Nova step");
}

extern "C" NOVA_API void nova_engine_terminate(nova_engine_t* sim) {
    sim->sim->terminate();
}

extern "C" NOVA_API void nova_engine_reset(nova_engine_t* sim) {
    sim->sim->reset();
}

extern "C" NOVA_API bool nova_engine_get_real(nova_engine_t* sim, const char* instance, const char* variable, double* value) {
    auto prop = sim->sim->get_real_property({instance, variable});
    if (prop) {
        *value = prop->get_value();
        return true;
    }
    return false;
}

extern "C" NOVA_API bool nova_engine_set_real(nova_engine_t* sim, const char* instance, const char* variable, double value) {
    auto prop = sim->sim->get_real_property({instance, variable});
    if (prop) {
        prop->set_value(value);
        return true;
    }
    return false;
}

extern "C" NOVA_API bool nova_engine_get_integer(nova_engine_t* sim, const char* instance, const char* variable, int* value) {
    auto prop = sim->sim->get_int_property({instance, variable});
    if (prop) {
        *value = prop->get_value();
        return true;
    }
    return false;
}

extern "C" NOVA_API bool nova_engine_set_integer(nova_engine_t* sim, const char* instance, const char* variable, int value) {
    auto prop = sim->sim->get_int_property({instance, variable});
    if (prop) {
        prop->set_value(value);
        return true;
    }
    return false;
}

extern "C" NOVA_API bool nova_engine_get_bool(nova_engine_t* sim, const char* instance, const char* variable, bool* value) {
    auto prop = sim->sim->get_bool_property({instance, variable});
    if (prop) {
        *value = prop->get_value();
        return true;
    }
    return false;
}

extern "C" NOVA_API bool nova_engine_set_bool(nova_engine_t* sim, const char* instance, const char* variable, bool value) {
    auto prop = sim->sim->get_bool_property({instance, variable});
    if (prop) {
        prop->set_value(value);
        return true;
    }
    return false;
}

extern "C" NOVA_API bool nova_engine_get_string(nova_engine_t* sim, const char* instance, const char* variable, char* value, size_t value_size) {
    auto prop = sim->sim->get_string_property({instance, variable});
    if (prop) {
        const auto propValue = prop->get_value();
        if (propValue.size() >= value_size) {
            std::strncpy(value, propValue.c_str(), value_size - 1);
            value[value_size - 1] = '\0';
        } else {
            std::strcpy(value, propValue.c_str());
        }
        return true;
    }
    return false;
}

extern "C" NOVA_API bool nova_engine_set_string(nova_engine_t* sim, const char* instance, const char* variable, const char* value) {
    auto prop = sim->sim->get_string_property({instance, variable});
    if (prop) {
        prop->set_value(std::string(value));
        return true;
    }
    return false;
}

struct c_api_engine_observer : public engine_observer {
    nova_listener_config_t config;
    c_api_engine_observer(nova_listener_config_t cfg) : config(cfg) {}

    void pre_step(nova_engine& sim) override {
        if (config.preStepCallback) {
            nova_simulation_info_t info;
            info.time = sim.time();
            info.iterations = sim.iterations();
            config.preStepCallback(info);
        }
    }
    
    void post_step(nova_engine& sim) override {
        if (config.postStepCallback) {
            nova_simulation_info_t info;
            info.time = sim.time();
            info.iterations = sim.iterations();
            config.postStepCallback(info);
        }
    }
};

extern "C" NOVA_API void* nova_engine_listener_create(nova_listener_config_t config) {
    auto obs = new std::shared_ptr<engine_observer>(std::make_shared<c_api_engine_observer>(config));
    return obs;
}

extern "C" NOVA_API void nova_engine_remove_listener(nova_engine_t* sim, const char* name) {
    sim->sim->remove_listener(std::string(name));
}

extern "C" NOVA_API void nova_engine_add_csv_recorder(nova_engine_t* sim, const char* filename) {
    sim->sim->add_listener("csv_recorder", std::make_unique<csv_recorder>(filename));
}

extern "C" NOVA_API const char* nova_last_error_msg() {
    return g_last_error_msg.c_str();
}

extern "C" NOVA_API void* nova_csv_writer_create(const char* filename, const char* config_path) {
    auto writer = std::make_shared<csv_recorder>(std::string(filename));
    if (config_path && std::string(config_path) != "") {
        try {
            writer->config().load(std::string(config_path));
        } catch(const std::exception& e) {
            log::err("Failed to load CSV config: {}", e.what());
            g_last_error_msg = e.what();
        }
    }
    return new std::shared_ptr<engine_observer>(writer);
}

extern "C" NOVA_API bool nova_csv_writer_set_decimation_factor(void* writer, int decimationFactor) {
    auto observer_ptr = static_cast<std::shared_ptr<engine_observer>*>(writer);
    if (observer_ptr && *observer_ptr) {
        auto csv_writer = std::dynamic_pointer_cast<csv_recorder>(*observer_ptr);
        if (csv_writer) {
            csv_writer->config().decimation_factor() = decimationFactor;
            return true;
        }
    }
    return false;
}

extern "C" NOVA_API void nova_engine_add_listener(nova_engine_t* sim, const char* name, void* listener) {
    auto ptr = static_cast<std::shared_ptr<engine_observer>*>(listener);
    sim->sim->add_listener(std::string(name), *ptr);
    delete ptr;
}

extern "C" NOVA_API void nova_plot_csv(const char* csv_path, const char* config_path) {
    try {
        nova::plot_csv(std::string(csv_path), std::string(config_path));
    } catch(const std::exception& e) {
        log::err("Failed to plot: {}", e.what());
    }
}

// Version info
extern "C" NOVA_API nova_version_t nova_library_version() {
    auto v = library_version();
    nova_version_t ret;
    ret.major = v.major;
    ret.minor = v.minor;
    ret.patch = v.patch;
    return ret;
}

extern "C" NOVA_API void nova_set_log_level(const char* level) {
    std::string l(level);
    log::level lvl = log::level::info;
    if (l == "trace") lvl = log::level::trace;
    else if (l == "debug") lvl = log::level::debug;
    else if (l == "info") lvl = log::level::info;
    else if (l == "warn") lvl = log::level::warn;
    else if (l == "err" || l == "error") lvl = log::level::err;
    else if (l == "off") lvl = log::level::off;
    
    log::set_logging_level(lvl);
}

extern "C" NOVA_API nova_scheduler_t* nova_scheduler_create(nova_engine_t* sim) {
    auto runner = new nova_scheduler_t();
    runner->runner = std::make_unique<engine_scheduler>(*sim->sim);
    return runner;
}

extern "C" NOVA_API void nova_scheduler_start(nova_scheduler_t* runner) {
    runner->runner->start();
}

extern "C" NOVA_API void nova_scheduler_stop(nova_scheduler_t* runner) {
    runner->runner->stop();
}

extern "C" NOVA_API void nova_scheduler_set_real_time_factor(nova_scheduler_t* runner, double factor) {
    runner->runner->set_real_time_factor(factor);
}

extern "C" NOVA_API void nova_scheduler_destroy(nova_scheduler_t* runner) {
    delete runner;
}
