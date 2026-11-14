
#ifndef LIBNOVA_CT_COMMON_HPP
#define LIBNOVA_CT_COMMON_HPP

#include "nova/components/algorithm/fixed_step_algorithm.hpp"
#include "nova/components/recorder/csv_recorder.hpp"
#include "nova/components/logger/logger.hpp"
#include "nova/engine/nova_fmu_locator.hpp"
#include "nova/engine/nova_engine.hpp"
#include "nova/components/util/chart_plotter.hpp"


using namespace nova;

inline double kelvi2deg(double k)
{
    return k - 273.15;
}

inline int run(const std::filesystem::path& fmuPath, bool remoting)
{
    set_logging_level(log::level::debug);

    try {
        auto locator = std::make_unique<nova_fmu_locator>();
        const auto fmuModel = locator->locate(std::filesystem::current_path(), remoting ? ("proxyfmu://localhost?file=" + fmuPath.string()) : fmuPath.string());

        if (!fmuModel) {
            return 1;
        }

        nova_engine sim(std::make_unique<fixed_step_algorithm>(1.0 / 50));
        sim.add_slave(fmuModel->instantiate("slave"));

        const auto t_room = sim.get_real_property("slave::Temperature_Room");
        t_room->set_output_modifier(kelvi2deg);

        const auto t_ref = sim.get_real_property("slave::Temperature_Reference");
        t_ref->set_output_modifier(kelvi2deg);

        auto csvWriter = std::make_unique<csv_recorder>("results/controlled_temperature.csv");
        const auto outputPath = csvWriter->output_path();
        sim.add_listener("csv_recorder", std::move(csvWriter));

        sim.init();
        sim.step_for(10);

        sim.terminate();

        plot_csv(outputPath, fmuPath.parent_path() / "ChartConfig.xml");
    } catch (const std::exception& ex) {

        log::err(ex.what());
        return 1;
    }

    return 0;
}

#endif // LIBNOVA_CT_COMMON_HPP
