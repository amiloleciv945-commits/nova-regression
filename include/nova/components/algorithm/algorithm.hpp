
#ifndef NOVA_ALGORITHM_HPP
#define NOVA_ALGORITHM_HPP

#include "nova/engine/model_instance.hpp"

namespace nova
{

struct algorithm_connections
{
    virtual void transfer_inputs(model_instance& destination) = 0;
    virtual void synchronize() = 0;
    virtual ~algorithm_connections() = default;
};

struct algorithm
{

    virtual void model_instance_added(model_instance* instance) = 0;

    virtual double step(double currentTime) = 0;
    virtual double step_with_connections(double currentTime, algorithm_connections& connections)
    {
        const double nextTime = step(currentTime);
        connections.synchronize();
        return nextTime;
    }
    virtual void reset() { }

    virtual ~algorithm() = default;
};


} // namespace nova

#endif // NOVA_ALGORITHM_HPP
