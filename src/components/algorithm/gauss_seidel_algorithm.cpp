#include "nova/components/algorithm/gauss_seidel_algorithm.hpp"

#include <cmath>
#include <stdexcept>
#include <unordered_map>

using namespace nova;

class gauss_seidel_algorithm::impl
{
public:
    impl(double stepSize, const std::vector<std::string>& stepOrder)
        : stepSize_(stepSize)
        , orderedInstances_(stepOrder.size(), nullptr)
    {
        if (!std::isfinite(stepSize_) || stepSize_ <= 0.0) {
            throw std::invalid_argument("Gauss-Seidel step size must be finite and positive");
        }
        if (stepOrder.empty()) {
            throw std::invalid_argument("Gauss-Seidel requires an instance order");
        }
        for (std::size_t i = 0; i < stepOrder.size(); ++i) {
            const auto& name = stepOrder[i];
            if (name.empty() || !positions_.emplace(name, i).second) {
                throw std::invalid_argument("Invalid or duplicate Gauss-Seidel instance name: '" + name + "'");
            }
        }
    }

    void model_instance_added(model_instance* instance)
    {
        const auto position = positions_.find(instance->instanceName());
        if (position == positions_.end()) {
            throw std::invalid_argument("Instance '" + instance->instanceName() + "' is missing from the Gauss-Seidel order");
        }
        auto& slot = orderedInstances_[position->second];
        if (slot) {
            throw std::invalid_argument("Instance '" + instance->instanceName() + "' was already registered");
        }
        slot = instance;
    }

    double step(double currentTime, algorithm_connections* connections)
    {
        if (stepFailed_) {
            throw std::runtime_error("Reset the engine before retrying a failed Gauss-Seidel step");
        }
        const double nextTime = currentTime + stepSize_;
        if (!std::isfinite(nextTime) || nextTime <= currentTime) {
            throw std::runtime_error("Gauss-Seidel step does not advance to a finite time");
        }
        for (const auto& [name, position] : positions_) {
            if (!orderedInstances_[position]) {
                throw std::runtime_error("Gauss-Seidel instance '" + name + "' was not registered");
            }
        }

        // A partial sweep may have advanced some FMUs irreversibly.
        stepFailed_ = true;
        for (auto* instance : orderedInstances_) {
            if (connections) {
                connections->transfer_inputs(*instance);
            }
            auto& properties = instance->get_properties();
            properties.apply_sets();
            if (!instance->try_step(currentTime, stepSize_)) {
                throw std::runtime_error("Instance '" + instance->instanceName() + "' did not complete the Gauss-Seidel step");
            }
            properties.apply_gets();
        }
        stepFailed_ = false;
        return nextTime;
    }

    void reset()
    {
        stepFailed_ = false;
    }

private:
    double stepSize_;
    std::unordered_map<std::string, std::size_t> positions_;
    std::vector<model_instance*> orderedInstances_;
    bool stepFailed_ = false;
};

gauss_seidel_algorithm::gauss_seidel_algorithm(double stepSize, const std::vector<std::string>& stepOrder)
    : pimpl_(std::make_unique<impl>(stepSize, stepOrder))
{ }

void gauss_seidel_algorithm::model_instance_added(model_instance* instance)
{
    pimpl_->model_instance_added(instance);
}

double gauss_seidel_algorithm::step(double currentTime)
{
    return pimpl_->step(currentTime, nullptr);
}

double gauss_seidel_algorithm::step_with_connections(double currentTime, algorithm_connections& connections)
{
    return pimpl_->step(currentTime, &connections);
}

void gauss_seidel_algorithm::reset()
{
    pimpl_->reset();
}

gauss_seidel_algorithm::~gauss_seidel_algorithm() = default;
