#pragma once

#include "nova/components/algorithm/algorithm.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nova
{

// One explicit, serial Gauss-Seidel sweep per communication step.
class gauss_seidel_algorithm : public algorithm
{
public:
    // stepOrder lists every instance once, in its execution order.
    gauss_seidel_algorithm(double stepSize, const std::vector<std::string>& stepOrder);

    void model_instance_added(model_instance* instance) override;
    double step(double currentTime) override;
    double step_with_connections(double currentTime, algorithm_connections& connections) override;
    void reset() override;
    ~gauss_seidel_algorithm() override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace nova
