import copy
import importlib.util
import tempfile
import unittest
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "tools/generate_catalog.py"
spec = importlib.util.spec_from_file_location("generate_catalog", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.registry = {"native_apps": [{"application_id": "demo", "executable": "/punos/bin/demo"}]}
        self.metadata = {"applications": [{"application_id": "demo", "display_name": "Демо",
                          "executable": "/punos/bin/demo", "default_arguments": ["--fixed"],
                          "environment": ["MODE=fixed"], "terminates_on_sigterm": True}]}

    def test_valid_deterministic_output(self):
        first = module.generate(self.registry, self.metadata)
        self.assertEqual(first, module.generate(self.registry, self.metadata))
        self.assertIn("PLATFORM_REGISTRY_ID", first[0])
        self.assertIn("app_generated_catalog", first[1])
        self.assertIn("\\320", first[1])

    def test_empty_catalogue(self):
        header, source = module.generate({"native_apps": []}, {"applications": []})
        self.assertIn("PLATFORM_NATIVE_APP_COUNT 0", header)
        self.assertIn("NULL, NULL", source)

    def test_registry_identity_covers_services(self):
        before = module.generate(self.registry, self.metadata)[0]
        self.registry["services"] = [{"service_id": "appserv", "enabled": True}]
        self.assertNotEqual(before, module.generate(self.registry, self.metadata)[0])

    def test_mismatched_executable(self):
        self.metadata["applications"][0]["executable"] = "/evil"
        with self.assertRaises(ValueError): module.generate(self.registry, self.metadata)

    def test_mismatched_identifier(self):
        self.metadata["applications"][0]["application_id"] = "other"
        with self.assertRaises(ValueError): module.generate(self.registry, self.metadata)

    def test_missing_metadata(self):
        with self.assertRaises(ValueError): module.generate(self.registry, {"applications": []})

    def test_duplicate_registry_and_metadata(self):
        self.registry["native_apps"] *= 2
        self.metadata["applications"] *= 2
        with self.assertRaises(ValueError): module.generate(self.registry, self.metadata)
        self.registry["native_apps"][1] = {"application_id": "other", "executable": "/other"}
        with self.assertRaises(ValueError): module.generate(self.registry, self.metadata)

    def test_count_limit(self):
        registry = {"native_apps": []}
        metadata = {"applications": []}
        for i in range(65):
            app = copy.deepcopy(self.metadata["applications"][0])
            app["application_id"] = str(i)
            metadata["applications"].append(app)
            registry["native_apps"].append({"application_id": str(i), "executable": app["executable"]})
        module.generate({"native_apps": registry["native_apps"][:64]},
                        {"applications": metadata["applications"][:64]})
        with self.assertRaises(ValueError): module.generate(registry, metadata)

    def test_invalid_strings(self):
        for field, values in {"display_name": ["a" * 65, "я" * 33, "\0", "\ud800", None],
                              "application_id": ["", "a" * 65],
                              "executable": ["", "bad\0path"]}.items():
            for value in values:
                with self.subTest(field=field, value=repr(value)):
                    metadata = copy.deepcopy(self.metadata)
                    metadata["applications"][0][field] = value
                    with self.assertRaises((ValueError, UnicodeError)):
                        module.generate(self.registry, metadata)

    def test_invalid_vectors_and_sigterm(self):
        for field, values in {"default_arguments": [None, ["bad\0arg"], [1]],
                              "environment": [["MISSING"], ["=value"], [""]],
                              "terminates_on_sigterm": [False, 1, None]}.items():
            for value in values:
                with self.subTest(field=field, value=value):
                    metadata = copy.deepcopy(self.metadata)
                    metadata["applications"][0][field] = value
                    with self.assertRaises(ValueError): module.generate(self.registry, metadata)

    def test_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.json"
            path.write_text('{"native_apps": [], "native_apps": []}')
            with self.assertRaises(ValueError): module.read_json(path)

    def test_metadata_order_does_not_change_registry_order(self):
        self.registry["native_apps"].append({"application_id": "other", "executable": "/other"})
        other = copy.deepcopy(self.metadata["applications"][0])
        other.update(application_id="other", executable="/other")
        self.metadata["applications"].insert(0, other)
        _, source = module.generate(self.registry, self.metadata)
        self.assertLess(source.index(module.c_string("demo")), source.index(module.c_string("other")))


if __name__ == "__main__":
    unittest.main()
