#ifndef NOVA_SIMULATION_STRUCTURE_HPP
#define NOVA_SIMULATION_STRUCTURE_HPP

#include "nova/engine/model.hpp"
#include "nova/engine/nova_fmu_locator.hpp"
#include "nova/engine/scalar.hpp"
#include "nova/engine/nova_engine.hpp"
#include "nova/engine/variable_identifier.hpp"

#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <string>
#include <functional>


namespace nova
{

using parameter_set = std::map<variable_identifier, scalar_value>;

struct DataLink {
    std::string src_instance;
    std::string src_variable;
    std::string dst_instance;
    std::string dst_variable;
    std::string type;
    std::function<double(double)> real_modifier;
};

class simulation_structure
{

public:
    simulation_structure();

    void add_model(const std::string& instanceName, const std::string& uri, std::optional<double> stepSizeHint = std::nullopt);

    void add_model(const std::string& instanceName, const std::filesystem::path& path, std::optional<double> stepSizeHint = std::nullopt);

    void add_model(const std::string& instanceName, std::shared_ptr<model> model, std::optional<double> stepSizeHint = std::nullopt);

    template<class T>
    void make_connection(variable_identifier source, variable_identifier sink, const std::optional<std::function<T(const T&)>>& modifier = std::nullopt)
    {
        std::string type;
        std::function<double(double)> real_modifier;
        if constexpr (std::is_same_v<T, double>) {
            type = "real";
            if (modifier) real_modifier = *modifier;
        }
        else if constexpr (std::is_same_v<T, int>) type = "int";
        else if constexpr (std::is_same_v<T, bool>) type = "bool";
        else if constexpr (std::is_same_v<T, std::string>) type = "string";
        else if constexpr (std::is_same_v<T, std::vector<double>>) type = "vector";

        links_.push_back({source.instanceName, source.variableName, sink.instanceName, sink.variableName, type, real_modifier});
    }

    void add_parameter_set(const std::string& name, const parameter_set& map);

    std::unique_ptr<nova_engine> load(std::unique_ptr<algorithm> algorithm);

private:
    std::unique_ptr<nova_fmu_locator> locator_;
    std::vector<DataLink> links_;
    std::unordered_map<std::string, parameter_set> parameterSets;
    std::unordered_map<std::string, std::pair<std::shared_ptr<model>, std::optional<double>>> models_;
};

} // namespace nova

#endif // NOVA_SIMULATION_STRUCTURE_HPP
