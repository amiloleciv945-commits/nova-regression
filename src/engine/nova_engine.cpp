
#include "nova/engine/nova_engine.hpp"
#include "nova/components/algorithm/gauss_seidel_algorithm.hpp"

#include "nova/components/recorder/engine_observer.hpp"
#include "nova/components/logger/logger.hpp"
#include "nova/engine/property.hpp"
#include "engine/scenario/scenario.hpp"

#include <execution>
#include "nova/components/util/nova_xml.hpp"
#include <ranges>

using namespace nova;

struct nova_engine::Impl
{
    struct ResolvedDataLink {
        std::string type;
        property_t<double>* src_real = nullptr;
        property_t<double>* dst_real = nullptr;
        property_t<int>* src_int = nullptr;
        property_t<int>* dst_int = nullptr;
        property_t<bool>* src_bool = nullptr;
        property_t<bool>* dst_bool = nullptr;
        property_t<std::string>* src_string = nullptr;
        property_t<std::string>* dst_string = nullptr;
        property_t<std::vector<double>>* src_vector = nullptr;
        property_t<std::vector<double>>* dst_vector = nullptr;
        std::function<double(double)> real_modifier;

        bool is_bound() const
        {
            return (src_real && dst_real) || (src_int && dst_int) || (src_bool && dst_bool)
                || (src_string && dst_string) || (src_vector && dst_vector);
        }
    };
    double lastDelta_{};
    double currentTime_{0};
    bool initialized_{false};
    bool terminated_{false};
    unsigned long num_iterations_{0};

    scenario scenario_;
    std::unique_ptr<algorithm> algorithm_;
    gauss_seidel_algorithm* const gaussSeidel_;
    std::vector<std::unique_ptr<model_instance>> instances_;
    std::vector<NovaDataLink> links_;
    std::vector<ResolvedDataLink> resolved_links_;
    std::unordered_map<const model_instance*, std::vector<std::size_t>> incoming_links_;
    std::unordered_map<std::string, std::shared_ptr<engine_observer>> listeners_;

    nova_engine& sim_;

    static void transfer_link(const ResolvedDataLink& link)
    {
        if (link.type == "real" && link.src_real && link.dst_real) {
            const double value = link.src_real->get_value();
            link.dst_real->set_value(link.real_modifier ? link.real_modifier(value) : value);
        } else if (link.type == "int" && link.src_int && link.dst_int) {
            link.dst_int->set_value(link.src_int->get_value());
        } else if (link.type == "bool" && link.src_bool && link.dst_bool) {
            link.dst_bool->set_value(link.src_bool->get_value());
        } else if (link.type == "string" && link.src_string && link.dst_string) {
            link.dst_string->set_value(link.src_string->get_value());
        } else if (link.type == "vector" && link.src_vector && link.dst_vector) {
            link.dst_vector->set_value(link.src_vector->get_value());
        }
    }

    void transfer_data()
    {
        for (const auto& link : resolved_links_) {
            transfer_link(link);
        }
    }

    void transfer_inputs(const model_instance& destination)
    {
        const auto incoming = incoming_links_.find(&destination);
        if (incoming != incoming_links_.end()) {
            for (const auto index : incoming->second) {
                transfer_link(resolved_links_[index]);
            }
        }
    }

    explicit Impl(nova_engine& sim, std::unique_ptr<algorithm> algorithm)
        : algorithm_(std::move(algorithm))
        , gaussSeidel_(dynamic_cast<gauss_seidel_algorithm*>(algorithm_.get()))
        , sim_(sim)
    { }

    double step(unsigned int numStep)
    {
        if (!initialized_) {
            throw std::runtime_error("init() has not been invoked!");
        }

        double newT{};
        for (unsigned i = 0; i < numStep; ++i) {

            for (auto l = listeners_; const auto& listener : l | std::views::values) {
                listener->pre_step(sim_);
            }

            scenario_.apply(currentTime_);

            if (gaussSeidel_ && !incoming_links_.empty()) {
                newT = gaussSeidel_->step(currentTime_, [this](const model_instance& destination) {
                    transfer_inputs(destination);
                });
            } else {
                newT = algorithm_->step(currentTime_);
                transfer_data();
                std::for_each(std::execution::par, instances_.begin(), instances_.end(), [](auto& instance) {
                    instance->get_properties().apply_sets();
                    instance->get_properties().apply_gets();
                });
            }

            lastDelta_ = newT - currentTime_;
            currentTime_ = newT;
            ++num_iterations_;

            for (auto l = listeners_; const auto& listener : l | std::views::values) {
                listener->post_step(sim_);
            }
        }

        return currentTime_;
    }

    void load_scenario(const std::filesystem::path& config)
    {

        if (!exists(config)) {
            throw std::runtime_error("No such file: " + absolute(config).string());
        }

        if (const auto ext = config.extension().string(); ext != ".xml") {
            throw std::runtime_error("Wrong config extension. Was " + ext + ", expected " + ".xml");
        }

        nova_sim::xml::XmlDocument doc;
        if (nova_sim::xml::XmlParseResult result = doc.load_file(config.string().c_str()); !result) {
            throw std::runtime_error("Unable to parse '" + absolute(config).string() + "': " + result.description());
        }

        size_t numActions{};
        const auto root = doc.child("nova:Scenario");
        for (const auto& action : root) {
            ++numActions;
            const auto t = action.attribute("t").as_double();
            const auto epsAttr = action.attribute("eps");
            std::optional<double> eps;
            if (epsAttr) eps = epsAttr.as_double();

            for (const auto& variable : action) {

                const variable_identifier id = variable.attribute("id").as_string();

                nova_sim::xml::XmlNode var;
                if ((var = variable.child("nova:real"))) {
                    auto p = sim_.get_real_property(id);
                    if (!p) {
                        log::warn("No variable with id: {}", id.str());
                        continue;
                    }
                    const double value = var.attribute("value").as_double();
                    sim_.invoke_at(
                        t, [p, value] {
                            p->set_value(value);
                        },
                        eps);
                } else if ((var = variable.child("nova:integer"))) {
                    auto p = sim_.get_int_property(id);
                    if (!p) {
                        log::warn("No variable with id: {}", id.str());
                        continue;
                    }
                    const int value = var.attribute("value").as_int();
                    sim_.invoke_at(
                        t, [p, value] {
                            p->set_value(value);
                        },
                        eps);
                } else if ((var = variable.child("nova:boolean"))) {
                    auto p = sim_.get_bool_property(id);
                    if (!p) {
                        log::warn("No variable with id: {}", id.str());
                        continue;
                    }
                    const bool value = var.attribute("value").as_bool();
                    sim_.invoke_at(
                        t, [p, value] {
                            p->set_value(value);
                        },
                        eps);
                } else if ((var = variable.child("nova:string"))) {
                    auto p = sim_.get_string_property(id);
                    if (!p) {
                        log::warn("No variable with id: {}", id.str());
                        continue;
                    }
                    const std::string value = var.attribute("value").as_string();
                    sim_.invoke_at(
                        t, [p, value] {
                            p->set_value(value);
                        },
                        eps);
                } else {
                    throw std::runtime_error("Assertion error");
                }
            }
        }

        log::debug("Applied scenario {} with {} actions to nova_engine", config.string(), numActions);
    }
};


nova_engine::nova_engine(std::unique_ptr<algorithm> algorithm)
    : pimpl_(std::make_unique<Impl>(*this, std::move(algorithm)))
{ }

double nova_engine::time() const
{
    return pimpl_->currentTime_;
}

unsigned long nova_engine::iterations() const
{
    return pimpl_->num_iterations_;
}

bool nova_engine::initialized() const
{
    return pimpl_->initialized_;
}

bool nova_engine::terminated() const
{
    return pimpl_->terminated_;
}

void nova_engine::init(std::optional<double> startTime, const std::optional<std::string>& parameterSet)
{
    if (!pimpl_->initialized_) {

        pimpl_->resolved_links_.clear();
        pimpl_->incoming_links_.clear();
        for (const auto& link : pimpl_->links_) {
            auto src_inst = get_instance(link.src_instance);
            auto dst_inst = get_instance(link.dst_instance);
            if (!src_inst || !dst_inst) {
                if (pimpl_->gaussSeidel_) {
                    throw std::invalid_argument("Unknown instance in Gauss-Seidel connection "
                        + link.src_instance + "::" + link.src_variable + " -> "
                        + link.dst_instance + "::" + link.dst_variable);
                }
                log::warn("Invalid link: instance not found (src: {}, dst: {})", link.src_instance, link.dst_instance);
                continue;
            }

            auto& src_props = src_inst->get_properties();
            auto& dst_props = dst_inst->get_properties();

            Impl::ResolvedDataLink rlink;
            rlink.type = link.type;
            rlink.real_modifier = link.real_modifier;

            if (link.type == "real") {
                rlink.src_real = src_props.get_real_property(link.src_variable);
                rlink.dst_real = dst_props.get_real_property(link.dst_variable);
            } else if (link.type == "int") {
                rlink.src_int = src_props.get_int_property(link.src_variable);
                rlink.dst_int = dst_props.get_int_property(link.dst_variable);
            } else if (link.type == "bool") {
                rlink.src_bool = src_props.get_bool_property(link.src_variable);
                rlink.dst_bool = dst_props.get_bool_property(link.dst_variable);
            } else if (link.type == "string") {
                rlink.src_string = src_props.get_string_property(link.src_variable);
                rlink.dst_string = dst_props.get_string_property(link.dst_variable);
            } else if (link.type == "vector") {
                rlink.src_vector = src_props.get_vector_property(link.src_variable);
                rlink.dst_vector = dst_props.get_vector_property(link.dst_variable);
            }
            if (pimpl_->gaussSeidel_) {
                if (!rlink.is_bound()) {
                    throw std::invalid_argument("Missing or mismatched properties for Gauss-Seidel connection "
                        + link.src_instance + "::" + link.src_variable + " -> "
                        + link.dst_instance + "::" + link.dst_variable);
                }
                pimpl_->incoming_links_[dst_inst].push_back(pimpl_->resolved_links_.size());
            }
            pimpl_->resolved_links_.push_back(std::move(rlink));
        }

        pimpl_->initialized_ = true;
        log::debug("Initializing nova_engine..");

        for (auto l = pimpl_->listeners_; const auto& listener : l | std::views::values) {
            listener->pre_init(*this);
        }

        int parameterSetAppliedCount = 0;
        for (const auto& instance : pimpl_->instances_) {
            double start = startTime.value_or(0);
            if (start < 0) {
                throw std::runtime_error("Explicitly defined startTime must be greater than 0!");
            }

            instance->enter_initialization_mode(start);
            if (parameterSet) {
                if (instance->apply_parameter_set(*parameterSet)) {
                    ++parameterSetAppliedCount;
                }
            }
        }
        if (parameterSet) {
            log::debug("Parameterset '{}' applied to {} instances", *parameterSet, parameterSetAppliedCount);
        }

        pimpl_->scenario_.runInitActions();

        for (unsigned i = 0; i < pimpl_->instances_.size(); ++i) {
            for (const auto& instance : pimpl_->instances_) {
                instance->get_properties().apply_sets();
                instance->get_properties().apply_gets();
            }
            pimpl_->transfer_data();
        }

        for (const auto& instance : pimpl_->instances_) {
            instance->exit_initialization_mode();
            instance->get_properties().apply_gets();
        }

        pimpl_->transfer_data();

        for (const auto& instance : pimpl_->instances_) {
            instance->get_properties().apply_sets();
            instance->get_properties().apply_gets();
        }

        for (auto l = pimpl_->listeners_; const auto& listener : pimpl_->listeners_ | std::views::values) {
            listener->post_init(*this);
        }

        log::debug("Initialized.");
    }
}

double nova_engine::step(unsigned int numStep)
{
    return pimpl_->step(numStep);
}

void nova_engine::step_until(double t)
{
    if (t <= pimpl_->currentTime_) {
        log::warn("Input time {} is not greater than the current nova_engine time {}. nova_engine will not progress.", t, pimpl_->currentTime_);
    } else {
        double newT = (pimpl_->currentTime_ + pimpl_->lastDelta_);
        while (newT < t) {
            step();
            newT = pimpl_->currentTime_ + pimpl_->lastDelta_;
        }
    }
}

void nova_engine::step_for(double t)
{
    const double newT = pimpl_->currentTime_ + t;
    step_until(newT);
}

void nova_engine::terminate()
{
    if (!pimpl_->terminated_) {
        pimpl_->terminated_ = true;

        log::debug("Terminating nova_engine..");

        for (const auto& instance : pimpl_->instances_) {
            instance->terminate();
        }

        for (auto l = pimpl_->listeners_; const auto& listener : l | std::views::values) {
            listener->post_terminate(*this);
        }

        log::debug("Terminated.");
    }
}

void nova_engine::reset()
{
    log::debug("Resetting nova_engine at t={}", time());
    for (const auto& instance : pimpl_->instances_) {
        instance->reset();
    }
    pimpl_->scenario_.reset();
    pimpl_->currentTime_ = 0;
    pimpl_->num_iterations_ = 0;
    pimpl_->initialized_ = false;
}

void nova_engine::add_listener(const std::string& name, std::shared_ptr<engine_observer> listener)
{
    if (pimpl_->listeners_.contains(name)) {
        log::warn("A listener named {} already exists..", name);
    } else {
        pimpl_->listeners_[name] = std::move(listener);
    }
}

void nova_engine::remove_listener(const std::string& name)
{
    log::debug("Removing listener named {}", name);
    pimpl_->listeners_.erase(name);
}

model_instance* nova_engine::get_instance(const std::string& name) const
{
    for (auto& instance : pimpl_->instances_) {
        if (instance->instanceName() == name) {
            return instance.get();
        }
    }
    return nullptr;
}

void nova_engine::add_slave(std::unique_ptr<model_instance> instance)
{
    if (!instance) {
        throw std::invalid_argument("Cannot add a null model_instance to the engine");
    }

    if (const auto name = instance->instanceName(); get_instance(name)) {
        throw std::runtime_error("A model instance named '" + name + "' has already been added!");
    }

    pimpl_->instances_.emplace_back(std::move(instance));
    pimpl_->algorithm_->model_instance_added(pimpl_->instances_.back().get());
}

void nova_engine::add_link(const NovaDataLink& link)
{
    pimpl_->links_.push_back(link);
}

property_t<double>* nova_engine::get_real_property(const variable_identifier& identifier) const
{
    for (auto& instance : pimpl_->instances_) {
        if (instance->instanceName() == identifier.instanceName) {
            const auto p = instance->get_properties().get_real_property(identifier.variableName);
            if (p) return p;
        }
    }
    return nullptr;
}

property_t<int>* nova_engine::get_int_property(const variable_identifier& identifier) const
{
    for (auto& instance : pimpl_->instances_) {
        if (instance->instanceName() == identifier.instanceName) {
            const auto p = instance->get_properties().get_int_property(identifier.variableName);
            if (p) return p;
        }
    }
    return nullptr;
}

property_t<std::string>* nova_engine::get_string_property(const variable_identifier& identifier) const
{
    for (auto& instance : pimpl_->instances_) {
        if (instance->instanceName() == identifier.instanceName) {
            auto p = instance->get_properties().get_string_property(identifier.variableName);
            if (p) return p;
        }
    }
    return nullptr;
}

property_t<bool>* nova_engine::get_bool_property(const variable_identifier& identifier) const
{
    for (auto& instance : pimpl_->instances_) {
        if (instance->instanceName() == identifier.instanceName) {
            const auto p = instance->get_properties().get_bool_property(identifier.variableName);
            if (p) return p;
        }
    }
    return nullptr;
}

property_t<std::vector<double>>* nova_engine::get_vector_property(const variable_identifier& identifier) const
{
    for (auto& instance : pimpl_->instances_) {
        if (instance->instanceName() == identifier.instanceName) {
            auto p = instance->get_properties().get_vector_property(identifier.variableName);
            if (p) return p;
        }
    }
    return nullptr;
}

const std::vector<std::unique_ptr<model_instance>>& nova_engine::get_instances() const
{
    return pimpl_->instances_;
}

std::vector<variable_identifier> nova_engine::identifiers() const
{
    std::vector<variable_identifier> ids;
    for (const auto& instance : pimpl_->instances_) {
        for (const auto& p : instance->get_properties().get_property_names()) {
            ids.emplace_back(instance->instanceName(), p);
        }
    }
    return ids;
}

void nova_engine::on_init(const std::function<void()>& f)
{
    pimpl_->scenario_.on_init(f);
}

void nova_engine::invoke_when(const std::function<bool()>& predicate, const std::function<void()>& action)
{
    pimpl_->scenario_.invoke_when(predicate_action{predicate, action});
}

void nova_engine::invoke_at(double timePoint, const std::function<void()>& f, const std::optional<double>& eps)
{
    pimpl_->scenario_.invoke_at(timed_action(timePoint, f, eps));
}

void nova_engine::load_scenario(const std::filesystem::path& config)
{
    pimpl_->load_scenario(config);
}

nova_engine::~nova_engine() = default;
