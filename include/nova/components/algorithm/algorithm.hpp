
#ifndef NOVA_ALGORITHM_HPP
#define NOVA_ALGORITHM_HPP

#include "nova/engine/model_instance.hpp"

namespace nova
{

struct algorithm
{

    virtual void model_instance_added(model_instance* instance) = 0;

    virtual double step(double currentTime) = 0;

    virtual ~algorithm() = default;
};


} // namespace nova

#endif // NOVA_ALGORITHM_HPP
