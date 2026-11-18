
#ifndef NOVA_ENGINE_HPP
#define NOVA_ENGINE_HPP

#include "nova/components/algorithm/algorithm.hpp"

#include "nova/components/recorder/engine_observer.hpp"
#include "nova/engine/model_instance.hpp"
#include "nova/engine/variable_identifier.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace nova
{

struct NovaDataLink {
    std::string src_instance;
    std::string src_variable;
    std::string dst_instance;
    std::string dst_variable;
    std::string type;
    std::function<double(double)> real_modifier;
};

class nova_engine
{

public:
    explicit nova_engine(std::unique_ptr<algorithm> algorithm);

    nova_engine(const nova_engine&) = delete;
    nova_engine(nova_engine&&) = delete;
    nova_engine& operator=(nova_engine&&) = delete;
    nova_engine& operator=(const nova_engine&) = delete;

    [[nodiscard]] double time() const;

    [[nodiscard]] unsigned long iterations() const;

    [[nodiscard]] bool initialized() const;

    [[nodiscard]] bool terminated() const;

    void init(const std::string& parameterSet)
    {
        init(std::nullopt, parameterSet);
    }

    void init(std::optional<double> startTime = std::nullopt, const std::optional<std::string>& parameterSet = std::nullopt);

    double step(unsigned int numStep = 1);

    void step_until(double t);

    void step_for(double t);

    void terminate();

    void reset();

    void add_slave(std::unique_ptr<model_instance> slave);

    void add_listener(const std::string& name, std::shared_ptr<engine_observer> listener);

    void remove_listener(const std::string& name);

    [[nodiscard]] model_instance* get_instance(const std::string& name) const;











    void add_link(const NovaDataLink& link);

    [[nodiscard]] property_t<double>* get_real_property(const variable_identifier& identifier) const;

    [[nodiscard]] property_t<int>* get_int_property(const variable_identifier& identifier) const;

    [[nodiscard]] property_t<std::string>* get_string_property(const variable_identifier& identifier) const;

    [[nodiscard]] property_t<bool>* get_bool_property(const variable_identifier& identifier) const;

    [[nodiscard]] property_t<std::vector<double>>* get_vector_property(const variable_identifier& identifier) const;

    [[nodiscard]] const std::vector<std::unique_ptr<model_instance>>& get_instances() const;

    [[nodiscard]] std::vector<variable_identifier> identifiers() const;

    void on_init(const std::function<void()>& f);

    void invoke_when(const std::function<bool()>& predicate, const std::function<void()>& action);

    void invoke_at(double timePoint, const std::function<void()>& f, const std::optional<double>& eps = 0);

    void load_scenario(const std::filesystem::path& config);

    ~nova_engine();

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace nova

#endif // NOVA_ENGINE_HPP
