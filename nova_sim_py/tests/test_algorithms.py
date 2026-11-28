import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from xml.etree import ElementTree as ET
from zipfile import ZipFile

from nova_sim_py import NovaEngine, NovaLib, NovaSimulationStructure


FMUS = Path(__file__).resolve().parents[2] / "data" / "fmus"
IDENTITY = FMUS / "1.0" / "identity.fmu"
CHAIN_MODELS = {"source": FMUS / "3.0/ref/Dahlquist.fmu", "relay": IDENTITY, "sink": IDENTITY}
CHAIN_LINKS = (("source::x", "relay::realIn"), ("relay::realOut", "sink::realIn"))
GAUSS_SEIDEL = {"type": "gauss_seidel", "order": ["source", "relay", "sink"]}


def write_ssp(path, models, links):
    root = ET.Element("ssd:SystemStructureDescription", {
        "name": "test", "version": "1.0",
        "xmlns:ssd": "http://ssp-standard.org/SSP1/SystemStructureDescription",
        "xmlns:ssc": "http://ssp-standard.org/SSP1/SystemStructureCommon",
    })
    system = ET.SubElement(root, "ssd:System", name="test")
    elements = ET.SubElement(system, "ssd:Elements")
    connections = ET.SubElement(system, "ssd:Connections")
    connectors = {}
    with ZipFile(path, "w") as archive:
        for name, fmu in models.items():
            source = f"resources/{name}.fmu"
            component = ET.SubElement(elements, "ssd:Component", name=name, source=source)
            connectors[name] = ET.SubElement(component, "ssd:Connectors")
            archive.write(fmu, source)
        for output, input_ in links:
            upstream, output_name = output.split("::")
            downstream, input_name = input_.split("::")
            connector = ET.SubElement(connectors[upstream], "ssd:Connector", name=output_name, kind="output")
            ET.SubElement(connector, "ssc:Real")
            connector = ET.SubElement(connectors[downstream], "ssd:Connector", name=input_name, kind="input")
            ET.SubElement(connector, "ssc:Real")
            ET.SubElement(connections, "ssd:Connection", startElement=upstream, startConnector=output_name,
                          endElement=downstream, endConnector=input_name)
        archive.writestr("SystemStructure.ssd", ET.tostring(root))


def write_identity_with_ports(path, input_name, output_name):
    names = {"realIn": input_name, "realOut": output_name}
    with ZipFile(IDENTITY) as original, ZipFile(path, "w") as updated:
        for member in original.infolist():
            contents = original.read(member)
            if member.filename == "modelDescription.xml":
                tree = ET.fromstring(contents)
                for variable in tree.findall("./ModelVariables/ScalarVariable"):
                    name = variable.attrib["name"]
                    if name in names:
                        variable.set("name", names[name])
                contents = ET.tostring(tree)
            updated.writestr(member, contents)


class TestAlgorithms(unittest.TestCase):
    def test_fixed_step_and_gauss_seidel_use_different_link_timings(self):
        with TemporaryDirectory() as directory, NovaSimulationStructure() as structure:
            for name, fmu in CHAIN_MODELS.items():
                structure.add_model(name, fmu)
            for source, sink in CHAIN_LINKS:
                structure.make_real_connection(source, sink)
            ssp = Path(directory) / "chain.ssp"
            write_ssp(ssp, CHAIN_MODELS, CHAIN_LINKS)

            for source in ({"structure": structure}, {"ssp_path": str(ssp)}):
                for algorithm, expected in (
                    (None, 1.0), ("fixed_step", 1.0),
                    ({"type": "fixed_step", "parallel": False}, 1.0),
                    (GAUSS_SEIDEL, 0.9),
                    ({"type": "gauss_seidel", "order": ["sink", "relay", "source"]}, 1.0),
                ):
                    with self.subTest(entry=next(iter(source)), algorithm=algorithm):
                        with NovaEngine(0.1, algorithm=algorithm, **source) as engine:
                            self.assertTrue(engine.init(), NovaLib.get_last_error())
                            self.assertAlmostEqual(engine.step(), 0.1)
                            self.assertAlmostEqual(engine.get_real("source", "x"), 0.9)
                            self.assertAlmostEqual(engine.get_real("sink", "realOut"), expected)
                            engine.terminate()

    def test_gauss_seidel_transfers_link_types_and_retains_python_modifier(self):
        calls = []

        def modify(value):
            calls.append(value)
            return 2 * value

        with NovaSimulationStructure() as structure:
            structure.add_model("source", IDENTITY)
            structure.add_model("sink", IDENTITY)
            structure.make_real_connection("source::realOut", "sink::realIn", modifier=modify)
            structure.make_int_connection("source::integerOut", "sink::integerIn")
            structure.make_bool_connection("source::booleanOut", "sink::booleanIn")
            structure.make_string_connection("source::stringOut", "sink::stringIn")
            engine = NovaEngine(0.01, structure=structure,
                                algorithm={"type": "gauss_seidel", "order": ["source", "sink"]})

        with engine:
            self.assertTrue(engine.init(), NovaLib.get_last_error())
            calls.clear()
            engine.set_real("source", "realIn", 3.0)
            engine.set_integer("source", "integerIn", 7)
            engine.set_bool("source", "booleanIn", True)
            engine.set_string("source", "stringIn", "nova")
            engine.step()
            self.assertEqual(calls, [3.0])
            self.assertEqual(engine.get_real("sink", "realOut"), 6.0)
            self.assertEqual(engine.get_integer("sink", "integerOut"), 7)
            self.assertTrue(engine.get_bool("sink", "booleanOut"))
            self.assertEqual(engine.get_string("sink", "stringOut"), "nova")
            engine.terminate()

    def test_strong_coupling_steps_from_structure_and_ssp(self):
        with TemporaryDirectory() as directory, NovaSimulationStructure() as structure:
            models = {name: Path(directory) / f"{name}.fmu" for name in ("force", "velocity")}
            write_identity_with_ports(models["force"], "V_input", "F")
            write_identity_with_ports(models["velocity"], "F_input", "V")
            links = (("force::F", "velocity::F_input"), ("velocity::V", "force::V_input"))
            for name, fmu in models.items():
                structure.add_model(name, fmu)
            for source, sink in links:
                structure.make_real_connection(source, sink)
            ssp = Path(directory) / "strong.ssp"
            write_ssp(ssp, models, links)

            for source in ({"structure": structure}, {"ssp_path": str(ssp)}):
                with NovaEngine(0.0005, algorithm={"type": "strong_coupling", "max_iterations": 2},
                                **source) as engine:
                    self.assertTrue(engine.init(), NovaLib.get_last_error())
                    self.assertAlmostEqual(engine.step(), 0.0006)
                    self.assertAlmostEqual(engine.step(), 0.00132)
                    engine.terminate()

    def test_invalid_options_and_order_report_errors(self):
        with NovaSimulationStructure() as structure:
            structure.add_model("source", IDENTITY)
            structure.add_model("sink", IDENTITY)
            with self.assertRaisesRegex(ValueError, "nonempty list or tuple"):
                NovaEngine(0.01, structure=structure, algorithm="gauss_seidel")
            with self.assertRaisesRegex(ValueError, "algorithm type must be"):
                NovaEngine(0.01, structure=structure, algorithm="unknown")
            for order, error in ((["source"], "every registered instance"),
                                 (["source", "unknown"], "was not registered"),
                                 (["source", "source"], "duplicate")):
                with self.subTest(order=order), self.assertRaisesRegex(RuntimeError, error):
                    NovaEngine(0.01, structure=structure, algorithm={"type": "gauss_seidel", "order": order})

    def test_bad_gauss_seidel_link_and_step_report_errors(self):
        with NovaSimulationStructure() as structure:
            structure.add_model("source", IDENTITY)
            structure.add_model("sink", IDENTITY)
            structure.make_real_connection("source::missing", "sink::realIn")
            with NovaEngine(0.01, structure=structure,
                            algorithm={"type": "gauss_seidel", "order": ["source", "sink"]}) as engine:
                self.assertFalse(engine.init())
                self.assertIn("source::missing -> sink::realIn", NovaLib.get_last_error())
                for step in (engine.step, lambda: engine.step_for(0.1), lambda: engine.step_until(0.1)):
                    with self.assertRaisesRegex(RuntimeError, r"init\(\) has not been invoked"):
                        step()
