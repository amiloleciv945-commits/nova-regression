#include "nova/components/algorithm/gauss_seidel_algorithm.hpp"

#include <cmath>
#include <unordered_set>

namespace nova
{

gauss_seidel_algorithm::gauss_seidel_algorithm(double stepSize, std::vector<std::string> stepOrder)
    : stepSize_(stepSize)
    , stepOrder_(std::move(stepOrder))
{
    if (!std::isfinite(stepSize_) || stepSize_ <= 0.0) {
        throw std::invalid_argument("Gauss-Seidel step size must be finite and positive");
    }
    if (stepOrder_.empty()) {
        throw std::invalid_argument("Gauss-Seidel requires an instance order");
    }
    std::unordered_set<std::string> names;
    for (const auto& name : stepOrder_) {
        if (name.empty() || !names.insert(name).second) {
            throw std::invalid_argument("Invalid or duplicate Gauss-Seidel instance name: '" + name + "'");
        }
    }
}

void gauss_seidel_algorithm::model_instance_added(model_instance* instance)
{
    instances_.emplace(instance->instanceName(), instance);
    schedule_.clear();
}

double gauss_seidel_algorithm::step(double currentTime)
{
    if (stepFailed_) {
        throw std::runtime_error("Recreate the engine before retrying a failed Gauss-Seidel step");
    }
    const double nextTime = currentTime + stepSize_;
    if (!std::isfinite(nextTime) || nextTime <= currentTime) {
        throw std::runtime_error("Gauss-Seidel step does not advance to a finite time");
    }
    if (schedule_.empty()) {
        resolve_schedule();
    }

    // An exception may leave some models already advanced.
    stepFailed_ = true;
    for (const auto& entry : schedule_) {
        for (const auto& update : entry.inputs) {
            update();
        }
        auto& properties = entry.instance->get_properties();
        properties.apply_sets();
        entry.instance->step(currentTime, stepSize_);
        properties.apply_gets();
    }
    stepFailed_ = false;
    return nextTime;
}

void gauss_seidel_algorithm::resolve_schedule()
{
    for (const auto& [destination, inputs] : links_) {
        if (!instances_.contains(destination)) {
            throw std::invalid_argument("Unknown Gauss-Seidel connection destination: " + destination);
        }
    }

    std::vector<scheduled_instance> schedule;
    schedule.reserve(stepOrder_.size());
    for (const auto& name : stepOrder_) {
        const auto model = instances_.find(name);
        if (model == instances_.end()) {
            throw std::invalid_argument("Gauss-Seidel instance '" + name + "' was not registered");
        }
        auto& entry = schedule.emplace_back(scheduled_instance{model->second, {}});
        if (const auto links = links_.find(name); links != links_.end()) {
            for (const auto& bind : links->second) {
                entry.inputs.push_back(bind(instances_));
            }
        }
    }
    if (schedule.size() != instances_.size()) {
        throw std::invalid_argument("Gauss-Seidel execution order must list every registered instance");
    }
    schedule_ = std::move(schedule);
}

} // namespace nova
