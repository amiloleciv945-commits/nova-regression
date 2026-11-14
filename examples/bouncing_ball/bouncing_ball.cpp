#include "nova/engine/nova_engine.hpp"
#include "nova/components/structure/simulation_structure.hpp"
#include "nova/components/algorithm/fixed_step_algorithm.hpp"
#include "nova/components/algorithm/strong_coupling_algorithm.hpp"
#include "nova/components/recorder/csv_recorder.hpp"
#include "nova/components/logger/logger.hpp"

using namespace nova;


enum class AlgorithmType
{
    FixedStep,
    StrongCoupling
};

int main()
{

    set_logging_level(log::level::debug);


    AlgorithmType algoType = AlgorithmType::StrongCoupling;
    // AlgorithmType algoType = AlgorithmType::FixedStep;


    std::string resultFile =
        (algoType == AlgorithmType::StrongCoupling)
        ? "results/strong_coupling.csv"
        : "results/fixed_step.csv";


    const std::string fmuPathA{
        std::string(DATA_FOLDER) +
        "/fmus/test/co_simulation_example_Hydraulic_HydraulicActuator.fmu"};

    const std::string fmuPathB{
        std::string(DATA_FOLDER) +
        "/fmus/test/co_simulation_example_Hydraulic_mass_spring.fmu"};


    simulation_structure ss;

    ss.add_model("Actuator", fmuPathA);
    ss.add_model("mass_spring", fmuPathB);


    nova::parameter_set params;
    params[{"Actuator", "V_input"}] = 0;
    params[{"mass_spring", "F_input"}] = 0;
    ss.add_parameter_set("initial_params", params);

    ss.make_connection<double>("Actuator::F", "mass_spring::F_input");
    ss.make_connection<double>("mass_spring::V", "Actuator::V_input");


    std::unique_ptr<algorithm> algo;

    if (algoType == AlgorithmType::StrongCoupling) {
        algo = std::make_unique<strong_coupling_algorithm>(
            0.0001,
            1, 
            false, 
            0,
            2);
    } else {
        algo = std::make_unique<fixed_step_algorithm>(
            0.0001, 
            true 
        );
    }

    
    const auto sim = ss.load(std::move(algo));



    auto csvWriter = std::make_unique<csv_recorder>(resultFile);
    csvWriter->config();
  
    csv_config& config = csvWriter->config();
    config.register_variable("mass_spring::F_input");
    config.register_variable("Actuator::V_input");
    config.register_variable("Actuator::F");
    config.register_variable("mass_spring::V");

    sim->add_listener("csv_recorder", std::move(csvWriter));



    sim->init("initial_params");
    sim->step_for(1);
    sim->terminate();

    return 0;
}
