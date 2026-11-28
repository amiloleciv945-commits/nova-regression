#pragma once

#include "nova/components/algorithm/algorithm.hpp"
#include "nova/engine/variable_identifier.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nova
{

// Fixed-step, non-iterative Gauss-Seidel. The algorithm owns the connections
// it exchanges during each step. Register those connections here, then pass
// the algorithm to simulation_structure::load. The initial outputs come
// from the FMUs after the engine has initialized them.
class gauss_seidel_algorithm : public algorithm
{
public:
    // stepOrder lists every instance once, in its execution order.
    gauss_seidel_algorithm(double stepSize, std::vector<std::string> stepOrder);

    template<class T>
    void make_connection(variable_identifier source, variable_identifier sink,
        std::function<T(const T&)> modifier = {})
    {
        auto& inputs = links_[sink.instanceName];
        inputs.emplace_back([source = std::move(source), sink = std::move(sink),
                                modifier = std::move(modifier)](const instances& models) -> transfer {
            const auto upstream = models.find(source.instanceName);
            if (upstream == models.end()) {
                throw std::invalid_argument("Unknown Gauss-Seidel connection source: " + source.str());
            }
            auto* output = find_property<T>(upstream->second->get_properties(), source.variableName);
            auto* input = find_property<T>(models.at(sink.instanceName)->get_properties(), sink.variableName);
            if (!output || !input) {
                throw std::invalid_argument("Missing or mismatched properties for Gauss-Seidel connection "
                    + source.str() + " -> " + sink.str());
            }
            return [output, input, modifier] {
                const auto value = output->get_value();
                input->set_value(modifier ? modifier(value) : value);
            };
        });
        schedule_.clear();
    }

    void model_instance_added(model_instance* instance) override;
    void validate_model_order() const;
    double step(double currentTime) override;
    double step(double currentTime, const std::function<void(const model_instance&)>& transferInputs);

private:
    using instances = std::unordered_map<std::string, model_instance*>;
    using transfer = std::function<void()>;
    using connection = std::function<transfer(const instances&)>;

    struct scheduled_instance
    {
        model_instance* instance;
        std::vector<transfer> inputs;
    };

    template<class T>
    static property_t<T>* find_property(nova::properties& props, const std::string& name)
    {
        if constexpr (std::is_same_v<T, double>) {
            return props.get_real_property(name);
        } else if constexpr (std::is_same_v<T, int>) {
            return props.get_int_property(name);
        } else if constexpr (std::is_same_v<T, bool>) {
            return props.get_bool_property(name);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return props.get_string_property(name);
        } else {
            static_assert(std::is_same_v<T, std::vector<double>>, "Unsupported Gauss-Seidel property type");
            return props.get_vector_property(name);
        }
    }

    void resolve_schedule();

    double stepSize_;
    std::vector<std::string> stepOrder_;
    instances instances_;
    std::unordered_map<std::string, std::vector<connection>> links_;
    std::vector<scheduled_instance> schedule_;
    bool stepFailed_ = false;
};

} // namespace nova
