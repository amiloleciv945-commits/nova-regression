
#ifndef NOVA_MODEL_HPP
#define NOVA_MODEL_HPP

#include "nova/engine/model_instance.hpp"

#include <memory>
#include <optional>

namespace nova
{

class model
{

public:
    virtual std::unique_ptr<model_instance> instantiate(const std::string& instanceName, std::optional<double> stepSizeHint = std::nullopt) = 0;

    virtual ~model() = default;
};

} // namespace nova

#endif // NOVA_MODEL_HPP
