
#ifndef NOVA_FMI_FMI2_SLAVE_HPP
#define NOVA_FMI_FMI2_SLAVE_HPP

#include "components/util/temp_dir.hpp"

#include "nova_fmi/fmicontext.hpp"
#include "nova_fmi/slave.hpp"

#include <fmi4c.h>
#include <memory>

namespace nova_fmi
{

class fmi2_slave : public slave
{

public:
    fmi2_slave(
        const std::shared_ptr<fmicontext>& ctx,
        const std::string& instanceName,
        model_description md,
        bool fmiLogging);

    [[nodiscard]] const model_description& get_model_description() const override;

    bool enter_initialization_mode(double start_time, double stop_time, double tolerance) override;
    bool exit_initialization_mode() override;
    bool step(double current_time, double step_size) override;

    bool terminate() override;
    bool reset() override;
    void freeInstance() override;

    void* get_state() override;
    bool set_state(void* state) override;
    bool free_state(void* state) override;

    bool get_integer(const std::vector<value_ref>& vr, std::vector<int32_t>& values) override;
    bool get_real(const std::vector<value_ref>& vr, std::vector<double>& values) override;
    bool get_string(const std::vector<value_ref>& vr, std::vector<std::string>& values) override;
    bool get_boolean(const std::vector<value_ref>& vr, std::vector<bool>& values) override;
    bool get_vector(const std::vector<value_ref>& vr, std::vector<std::vector<double>>& values, fmi2Integer len) override;

    bool set_integer(const std::vector<value_ref>& vr, const std::vector<int32_t>& values) override;
    bool set_real(const std::vector<value_ref>& vr, const std::vector<double>& values) override;
    bool set_string(const std::vector<value_ref>& vr, const std::vector<std::string>& values) override;
    bool set_boolean(const std::vector<value_ref>& vr, const std::vector<bool>& values) override;
    bool set_vector(const std::vector<value_ref>& vr, const std::vector<std::vector<double>>& values) override;

    ~fmi2_slave() override;

private:
    std::shared_ptr<void> instance_;
    fmi2InstanceHandle fmi4c_handle_;
    fmi2InstanceHandle* component{nullptr};
    std::shared_ptr<fmicontext> ctx_;
    fmi2CallbackFunctions callbacks_;

    model_description md_;
};

} // namespace nova_fmi

#endif // NOVA_FMI_FMI2_SLAVE_HPP
