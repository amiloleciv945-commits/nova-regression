import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from zipfile import ZipFile

from nova_sim_py import NovaLib, NovaSimulationStructure


FMUS = Path(__file__).resolve().parents[2] / "data" / "fmus"
IDENTITY = FMUS / "1.0" / "identity.fmu"


class TestAddModel(unittest.TestCase):
    def test_missing_fmu_reports_path_and_allows_retry(self):
        with TemporaryDirectory() as directory, NovaSimulationStructure() as structure:
            missing = Path(directory) / "not-found.fmu"
            with self.assertRaisesRegex(Exception, "Unable to resolve model: .*not-found[.]fmu"):
                structure.add_model("model", missing)

            structure.add_model("model", IDENTITY)
            self.assertEqual(NovaLib.get_last_error(), "")

    def test_reports_duplicate_instance_name(self):
        with NovaSimulationStructure() as structure:
            structure.add_model("identity", IDENTITY)
            with self.assertRaisesRegex(Exception, "A model named identity has already been added"):
                structure.add_model("identity", IDENTITY)

    def test_reports_corrupt_archive(self):
        with TemporaryDirectory() as directory, NovaSimulationStructure() as structure:
            corrupt = Path(directory) / "corrupt.fmu"
            corrupt.write_bytes(b"invalid FMU archive")
            with self.assertRaisesRegex(Exception, "Failed to unzip FMU '.*corrupt[.]fmu'"):
                structure.add_model("model", corrupt)

    def test_invalid_metadata_preserves_working_directory(self):
        working_directory = Path.cwd()
        with TemporaryDirectory() as directory, NovaSimulationStructure() as structure:
            invalid = Path(directory) / "invalid-description.fmu"
            with ZipFile(invalid, "w") as archive:
                archive.writestr("modelDescription.xml", "<invalid/>")

            with self.assertRaisesRegex(Exception, "Invalid modelDescription[.]xml"):
                structure.add_model("model", invalid)
            self.assertEqual(Path.cwd(), working_directory)
            structure.add_model("model", IDENTITY)

    def test_reports_missing_platform_library(self):
        for model in (IDENTITY, FMUS / "2.0" / "20sim" / "ControlledTemperature.fmu"):
            with self.subTest(model=model.name):
                with TemporaryDirectory() as directory, NovaSimulationStructure() as structure:
                    missing_library = Path(directory) / model.name
                    with ZipFile(model) as source, ZipFile(missing_library, "w") as archive:
                        archive.writestr("modelDescription.xml", source.read("modelDescription.xml"))

                    with self.assertRaisesRegex(Exception, "model library"):
                        structure.add_model("model", missing_library)
