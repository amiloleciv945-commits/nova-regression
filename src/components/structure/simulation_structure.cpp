
#include "nova/components/structure/simulation_structure.hpp"

#include "nova/engine/variable_identifier.hpp"

#include <ranges>
#include <utility>

using namespace nova;

namespace
{

template<class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

} // namespace

simulation_structure::simulation_structure()
    : locator_(std::make_unique<nova_fmu_locator>())
{
}

void simulation_structure::add_model(const std::string& instanceName, const std::string& uri, std::optional<double> stepSizeHint)
{
    const auto model = locator_->locate(std::filesystem::current_path(), uri);
    if (!model) {
        throw std::runtime_error("Unable to resolve model: " + uri);
    }
    add_model(instanceName, model, stepSizeHint);
}

void simulation_structure::add_model(const std::string& instanceName, const std::filesystem::path& path, std::optional<double> stepSizeHint)
{
    add_model(instanceName, relative(path).string(), stepSizeHint);
}

void simulation_structure::add_model(const std::string& instanceName, std::shared_ptr<model> model, std::optional<double> stepSizeHint)
{

    if (!model) throw std::runtime_error("Attempting to pass nullptr as model!");

    if (models_.contains(instanceName)) {
        throw std::runtime_error("A model named " + instanceName + " has already been added!");
    }
    models_[instanceName] = {std::move(model), stepSizeHint};
}

void simulation_structure::add_parameter_set(const std::string& name, const parameter_set& map)
{
    parameterSets[name] = map;
}

std::unique_ptr<nova_engine> simulation_structure::load(std::unique_ptr<algorithm> algorithm)
{
    std::unordered_map<std::string, std::unique_ptr<model_instance>> instances;
    for (const auto& [name, model] : models_) {
        instances.emplace(name, model.first->instantiate(name, model.second));
    }

    for (const auto& [parameterSetName, map] : parameterSets) {
        for (const auto& [v, value] : map) {
            instances[v.instanceName]->add_parameterset_entry(parameterSetName, v.variableName, value);
        }
    }

    auto sim = std::make_unique<nova_engine>(std::move(algorithm));
    for (auto& instance : instances | std::views::values) {
        sim->add_slave(std::move(instance));
    }

    for (const auto& link : links_) {
        NovaDataLink nlink;
        nlink.src_instance = link.src_instance;
        nlink.src_variable = link.src_variable;
        nlink.dst_instance = link.dst_instance;
        nlink.dst_variable = link.dst_variable;
        nlink.type = link.type;
        nlink.real_modifier = link.real_modifier;
        sim->add_link(nlink);
    }

    return sim;
}
