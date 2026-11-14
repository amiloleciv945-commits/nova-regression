from nova_sim_py import *
from nova_sim_py.chart_plotter import *

from pathlib import Path

def kelvin_to_deg(value: float) -> float:
    return value - 273.15

def main():
    print(f"Novalib version: {NovaLib.version()}")

    NovaLib.set_log_level("debug")

    fmu_path = str((Path(__file__).parent.parent.parent / 'data' / 'fmus' / '2.0' / '20sim' / 'ControlledTemperature.fmu').resolve())
    result_file = "results/python/temperature.csv"

    with NovaSimulationStructure() as ss:
        ss.add_model("model", fmu_path)

        with (NovaEngine(structure=ss, step_size=1/50)) as sim:

            sim.add_csv_writer(result_file)
            sim.init()
            sim.step_until(10)
            sim.terminate()

    config = TimeSeriesConfig(
        title="ControlledTemperature",
        y_label="Temperature[deg]",
        identifiers=["model::Temperature_Reference", "model::Temperature_Room"],
        modifiers={
            "model::Temperature_Reference" : kelvin_to_deg,
            "model::Temperature_Room" : kelvin_to_deg
        }
    )
    chart_plotter = Plotter(result_file, config)
    chart_plotter.show()


if __name__ == "__main__":
    main()
