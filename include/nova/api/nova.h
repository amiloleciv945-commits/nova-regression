#ifndef NOVA_H
#define NOVA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef NOVA_EXPORT_DLL
#define NOVA_API __declspec(dllexport)
#elif defined(NOVA_IMPORT_DLL)
#define NOVA_API __declspec(dllimport)
#else
#define NOVA_API
#endif
#else
#define NOVA_API
#endif

typedef struct nova_engine_t nova_engine_t;
typedef struct nova_simulation_structure_t nova_simulation_structure_t;
typedef struct nova_parameter_set_t nova_parameter_set_t;
typedef struct nova_csv_recorder_t nova_csv_recorder_t;
typedef struct nova_scheduler_t nova_scheduler_t;

NOVA_API nova_simulation_structure_t* nova_simulation_structure_create();
NOVA_API nova_simulation_structure_t* nova_simulation_structure_load_ssp(const char* ssp_path);
NOVA_API void nova_simulation_structure_destroy(nova_simulation_structure_t* ss);
NOVA_API bool nova_simulation_structure_add_model(nova_simulation_structure_t* ss, const char* name, const char* uri);
NOVA_API void nova_simulation_structure_make_connection(nova_simulation_structure_t* ss, const char* src_inst, const char* src_var, const char* dst_inst, const char* dst_var, const char* type);
typedef double (*nova_real_modifier_t)(double);
NOVA_API void nova_simulation_structure_make_real_connection_mod(nova_simulation_structure_t* ss, const char* src, const char* dst, nova_real_modifier_t modifier);
NOVA_API void nova_simulation_structure_make_real_connection(nova_simulation_structure_t* ss, const char* src, const char* dst);
NOVA_API void nova_simulation_structure_make_int_connection(nova_simulation_structure_t* ss, const char* src, const char* dst);
NOVA_API void nova_simulation_structure_make_bool_connection(nova_simulation_structure_t* ss, const char* src, const char* dst);
NOVA_API void nova_simulation_structure_make_string_connection(nova_simulation_structure_t* ss, const char* src, const char* dst);
NOVA_API void nova_simulation_structure_make_vector_connection(nova_simulation_structure_t* ss, const char* src, const char* dst);

NOVA_API nova_parameter_set_t* nova_parameter_set_create();
NOVA_API void nova_parameter_set_add_real(nova_parameter_set_t* pps, const char* name, double value);
NOVA_API void nova_parameter_set_add_int(nova_parameter_set_t* pps, const char* name, int value);
NOVA_API void nova_parameter_set_add_bool(nova_parameter_set_t* pps, const char* name, bool value);
NOVA_API void nova_parameter_set_add_string(nova_parameter_set_t* pps, const char* name, const char* value);
NOVA_API void nova_parameter_set_destroy(nova_parameter_set_t* pps);
NOVA_API void nova_simulation_structure_add_parameter_set(nova_simulation_structure_t* ss, const char* name, nova_parameter_set_t* pps);

NOVA_API nova_engine_t* nova_engine_create(nova_simulation_structure_t* ss, double step_size);
NOVA_API nova_engine_t* nova_engine_create_from_ssp(const char* ssp_path, double step_size);
NOVA_API void nova_engine_destroy(nova_engine_t* sim);
NOVA_API bool nova_engine_load_scenario(nova_engine_t* sim, const char* scenario_file);
NOVA_API bool nova_engine_init(nova_engine_t* sim, double start_time, const char* parameter_set);
NOVA_API double nova_engine_step(nova_engine_t* sim, unsigned int num_steps);
NOVA_API void nova_engine_step_until(nova_engine_t* sim, double time_point);
NOVA_API void nova_engine_step_for(nova_engine_t* sim, double duration);
NOVA_API void nova_engine_terminate(nova_engine_t* sim);

NOVA_API nova_scheduler_t* nova_scheduler_create(nova_engine_t* sim);
NOVA_API void nova_scheduler_start(nova_scheduler_t* runner);
NOVA_API void nova_scheduler_stop(nova_scheduler_t* runner);
NOVA_API void nova_scheduler_set_real_time_factor(nova_scheduler_t* runner, double factor);
NOVA_API void nova_scheduler_destroy(nova_scheduler_t* runner);

typedef struct nova_simulation_info_t {
    double time;
    size_t iterations;
} nova_simulation_info_t;

typedef void (*nova_step_callback_t)(nova_simulation_info_t info);

typedef struct nova_listener_config_t {
    nova_step_callback_t preStepCallback;
    nova_step_callback_t postStepCallback;
} nova_listener_config_t;

NOVA_API void* nova_engine_listener_create(nova_listener_config_t config);
NOVA_API void nova_engine_remove_listener(nova_engine_t* sim, const char* name);

NOVA_API void nova_engine_add_csv_recorder(nova_engine_t* sim, const char* filename);
NOVA_API void* nova_csv_writer_create(const char* filename, const char* config_path);
NOVA_API bool nova_csv_writer_set_decimation_factor(void* writer, int decimationFactor);
NOVA_API void nova_engine_add_listener(nova_engine_t* sim, const char* name, void* listener);
NOVA_API void nova_plot_csv(const char* csv_path, const char* config_path);

NOVA_API void nova_engine_reset(nova_engine_t* sim);
// Returns the last error on the calling thread.
NOVA_API const char* nova_last_error_msg();

NOVA_API bool nova_engine_get_real(nova_engine_t* sim, const char* instance, const char* variable, double* value);
NOVA_API bool nova_engine_set_real(nova_engine_t* sim, const char* instance, const char* variable, double value);

NOVA_API bool nova_engine_get_integer(nova_engine_t* sim, const char* instance, const char* variable, int* value);
NOVA_API bool nova_engine_set_integer(nova_engine_t* sim, const char* instance, const char* variable, int value);

NOVA_API bool nova_engine_get_bool(nova_engine_t* sim, const char* instance, const char* variable, bool* value);
NOVA_API bool nova_engine_set_bool(nova_engine_t* sim, const char* instance, const char* variable, bool value);

NOVA_API bool nova_engine_get_string(nova_engine_t* sim, const char* instance, const char* variable, char* value, size_t value_size);
NOVA_API bool nova_engine_set_string(nova_engine_t* sim, const char* instance, const char* variable, const char* value);

typedef struct nova_version_t {
    int major;
    int minor;
    int patch;
} nova_version_t;

NOVA_API nova_version_t nova_library_version();
NOVA_API void nova_set_log_level(const char* level);

#ifdef __cplusplus
}
#endif

#endif // NOVA_H
