from nova_sim_py import *
from nova_sim_py.chart_plotter import *

from pathlib import Path




def main():
    print(f"Novalib version: {NovaLib.version()}")

    NovaLib.set_log_level("debug")

    ssp_dir = (Path(__file__).parent.parent.parent / 'data' / 'ssp' / '1.0' / 'quarter_truck').resolve()
    ssp_file = f"{ssp_dir}/quarter-truck.ssp"
    result_file = f"results/python/quarter_truck.csv"

    with (NovaEngine(ssp_path=ssp_file, step_size=1.0 / 100)) as sim:

        sim.add_csv_writer(result_file, f"{ssp_dir}/CsvConfig.xml")

        sim.init(parameter_set="initialValues")
        sim.step_until(time_point=10)
        sim.terminate()

    configs = [
        TimeSeriesConfig(
            title="Quarter-truck",
            y_label="Height[m]",
            identifiers=["chassis::zChassis"]),
        TimeSeriesConfig(
            title="Quarter-truck",
            y_label="Height[m]",
            identifiers=["wheel::zWheel"]),
        TimeSeriesConfig(
            title="Quarter-truck",
            y_label="Height[m]",
            identifiers=["ground::zGround"])
    ]
    chart_plotter = Plotter(result_file, configs)
    chart_plotter.show()


if __name__ == "__main__":
    main()
