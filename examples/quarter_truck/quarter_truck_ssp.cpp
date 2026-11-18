#include "nova/components/algorithm/fixed_step_algorithm.hpp"
#include "nova/components/recorder/csv_recorder.hpp"
#include "nova/components/logger/logger.hpp"
#include "nova/components/ssp/ssp_loader.hpp"
#include "nova/components/util/chart_plotter.hpp"

#include <chrono>
#include "nova/components/logger/nova_logger.hpp"

using namespace nova;

int main()
{
    log::set_logging_level(nova::log::level::debug);

    const std::filesystem::path sspFolder = std::string(DATA_FOLDER) + "/ssp/1.0/quarter_truck";

    try {
        const auto ss = load_ssp(sspFolder);
        const auto sim = ss->load(std::make_unique<fixed_step_algorithm>(1.0 / 100));

        auto csvWriter = std::make_unique<csv_recorder>("results/quarter_truck.csv");
        const auto outputPath = csvWriter->output_path();
        csv_config& config = csvWriter->config();
        config.register_variable("chassis::zChassis");
        config.register_variable("wheel::zWheel");
        config.register_variable("ground::zGround");
        sim->add_listener("csv_recorder", std::move(csvWriter));

        log::Stopwatch sw;
        sim->init("initialValues");
        sim->step_until(5);
        log::info("Elapsed {:.4f}s", sw.elapsed().count());

        sim->terminate();

        plot_csv(outputPath, sspFolder / "ChartConfig.xml");
    } catch (const std::exception& ex) {

        log::err(ex.what());
    }
}
