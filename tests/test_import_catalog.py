import copy
import hashlib
import importlib.util
import json
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("import_catalog", ROOT / "tools/import_catalog.py")
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


sys.path.insert(0, str(ROOT / "tools"))
import make_fixture


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.tables = json.loads((ROOT / "tests/fixtures/akedata_sample.json").read_text())
        self.aliases = json.loads((ROOT / "tools/catalog_aliases.json").read_text())
        self.version = {"id": "test@1", "tableCfgPath": "public/test/1/TableCfg"}
        self.hashes = {name: hashlib.sha256(importer.encode(self.tables[name])).hexdigest()
                       for name in importer.TABLES}

    def convert(self):
        return importer.convert(self.tables, self.version, self.hashes, self.aliases)

    def test_verified_refinery_geometry_and_recipe_timing(self):
        catalog = self.convert()
        refinery = next(f for f in catalog["facilities"] if f["id"] == "furnance_1")
        self.assertEqual((refinery["name"], refinery["width"], refinery["height"], refinery["power"]),
                         ("refining_unit", 3, 3, 5))
        self.assertEqual((refinery["inputs"][0]["x"], refinery["inputs"][0]["y"],
                          refinery["inputs"][0]["fx"], refinery["inputs"][0]["fy"]), (0, 0, 0, -1))
        self.assertEqual(refinery["outputs"][0]["fy"], 1)
        self.assertEqual(refinery["inputs"][3]["fx"], -1)
        self.assertEqual(refinery["outputs"][3]["fx"], 1)
        battery = next(r for r in catalog["recipes"] if r["id"] == "tools_proc_battery_2_1")
        self.assertEqual(battery["time"], 10)
        self.assertEqual([i["amount"] for i in battery["ingredients"]], [10, 15])
        self.assertEqual(battery["products"][0]["item"], "sc_valley_battery")
        self.assertEqual(battery["unsupported_reasons"], [])
        self.assertEqual(catalog["source"]["tables"]["ItemTable"]["sha256"], self.hashes["ItemTable"])

    def test_regional_products_prices_unlocks_and_events_preserved(self):
        catalog = self.convert()
        markets = {market["id"]: market for market in catalog["regions"]}
        valley = markets["domain_1"]
        wuling = markets["domain_2"]
        self.assertNotEqual(valley["currency_id"], wuling["currency_id"])
        self.assertIn("sc_valley_battery", {p["product"] for p in valley["products"]})
        self.assertEqual(next(p["prices"] for p in valley["products"] if p["product"] == "sc_valley_battery"), [30])
        self.assertEqual(next(p["prices"] for p in wuling["products"] if p["product"] == "lc_wuling_battery"), [25])
        offers = [offer for outpost in wuling["outposts"] for level in outpost["levels"] for offer in level["offers"]]
        self.assertTrue(any(offer["activity_id"] for offer in offers))
        self.assertEqual([level["level"] for level in valley["outposts"][0]["levels"]], [1, 2, 3, 4])
        self.assertGreater(valley["outposts"][0]["levels"][-1]["cash_pool_capacity"], 0)
        self.tables["SettlementBasicDataTable"]["stm_tundra_1"]["settlementLevelMap"]["4"]["settlementTradeItemMap"]["unknown"] = {
            "itemId": "unknown", "rewardMoneyCount": 30}
        with self.assertRaises(ValueError): self.convert()

    def test_unknown_ids_remain_stable_and_order_is_deterministic(self):
        catalog = self.convert()
        self.aliases = {"facilities": {}, "items": {}}
        unaliased = self.convert()
        self.assertEqual(unaliased["facilities"][0]["id"], unaliased["facilities"][0]["name"])
        self.aliases = json.loads((ROOT / "tools/catalog_aliases.json").read_text())
        self.tables = {k: dict(reversed(list(v.items()))) for k, v in self.tables.items()}
        self.assertEqual(importer.encode(catalog), importer.encode(self.convert()))

    def test_rejects_invalid_geometry_quantities_and_references(self):
        original = copy.deepcopy(self.tables)
        cases = [lambda: self.tables["FactoryBuildingTable"]["furnance_1"]["range"].update(width=0),
                 lambda: self.tables["FactoryMachineCraftTable"]["tools_proc_battery_2_1"].update(machineId="missing"),
                 lambda: self.tables["FactoryMachineCraftTable"]["tools_proc_battery_2_1"]["ingredients"][0]["group"][0].update(count=-1),
                 lambda: self.tables["FactoryMachineCraftGroupTable"]["group_tools_normal"].update(msPerRound=0)]
        for change in cases:
            self.tables = copy.deepcopy(original)
            change()
            with self.assertRaises(ValueError): self.convert()

    def test_unsupported_mechanics_are_retained_and_reported(self):
        self.tables["FactoryMachineCraftTable"]["tools_proc_battery_2_1"]["gasEnv"] = 1
        catalog = self.convert()
        report = importer.change_report({}, catalog)
        self.assertIn("environment requirement", report["unsupported_recipes"]["tools_proc_battery_2_1"])

    def test_diff_detects_added_changed_removed_records(self):
        old = self.convert()
        new = copy.deepcopy(old)
        removed = new["facilities"].pop()
        new["facilities"][0]["power"] += 1
        new["facilities"].append({"id": "new_machine", "name": "new_machine"})
        delta = importer.change_report(old, new)["facilities"]
        self.assertEqual(delta["added"], ["new_machine"])
        self.assertEqual(delta["removed"], [removed["id"]])
        self.assertEqual(delta["changed"], [new["facilities"][0]["id"]])

    def test_online_pinned_offline_and_failed_refresh(self):
        manifest = {"schemaVersion": 1, "latest": "test@1", "versions": [self.version]}
        calls = []
        def fetch(url):
            calls.append(url)
            return importer.encode(manifest if url.endswith("manifest.json") else
                                   self.tables[url.rsplit("/", 1)[1][:-5]])
        with tempfile.TemporaryDirectory() as temp:
            cache, output = Path(temp) / "cache", Path(temp) / "catalog.json"
            with patch.object(importer, "fetch_json", side_effect=fetch):
                first, _ = importer.import_catalog(cache, output)
                baseline = output.read_bytes()
                second, report = importer.import_catalog(cache, output, version_id="test@1")
            self.assertEqual(first, second)
            self.assertEqual(len(calls), len(importer.TABLES) + 2)
            self.assertEqual(report["recipes"]["changed"], [])
            with patch.object(importer, "fetch_json", side_effect=AssertionError("network in offline mode")):
                importer.import_catalog(cache, output, version_id="test@1", offline=True)
            self.assertEqual(baseline, output.read_bytes())
            with patch.object(importer, "fetch_json", side_effect=OSError("interrupted download")):
                with self.assertRaises(OSError): importer.import_catalog(cache, output, refresh=True)
            self.assertEqual(baseline, output.read_bytes())
            # Conversion failure after successful downloads also preserves the artifact.
            self.tables["FactoryBuildingTable"]["furnance_1"]["range"]["width"] = 0
            with patch.object(importer, "fetch_json", side_effect=fetch):
                with self.assertRaises(ValueError): importer.import_catalog(cache, output, refresh=True)
            self.assertEqual(baseline, output.read_bytes())

    def test_explicit_fixture_extraction_never_overwrites_existing_data(self):
        catalog = self.convert()
        with tempfile.TemporaryDirectory() as temp:
            dataset, output = Path(temp) / "region", Path(temp) / "fixture"
            dataset.mkdir()
            importer.atomic_write(dataset / "catalog.json", importer.encode(catalog))
            region = {"catalog": "catalog.json", "base_power": 200, "storage": 80000,
                      "resources": [{"item": "item_iron_ore", "limit": 120}],
                      "enabled_facilities": [f["id"] for f in catalog["facilities"]],
                      "enabled_recipes": [r["id"] for r in catalog["recipes"]]}
            importer.atomic_write(dataset / "region.json", importer.encode(region))
            for name in ("products.json", "areas.json", "fuels.json"):
                importer.atomic_write(dataset / name, importer.encode([]))
            make_fixture.make_fixture(dataset, output)
            projected = json.loads((output / "region.json").read_bytes())
            self.assertNotIn("catalog", projected)
            self.assertEqual(projected["resources"][0]["item"], "ferrium_ore")
            facilities = json.loads((output / "facilities.json").read_bytes())
            refinery = next(f for f in facilities if f["id"] == "furnance_1")
            self.assertEqual(len(refinery["inputs"]), 3)  # inactive pipe port is omitted
            self.assertEqual(sum(len(f["recipes"]) for f in facilities), 4)
            with self.assertRaises(ValueError): make_fixture.make_fixture(dataset, output)

    def test_fixture_market_eligibility_and_unknown_outposts(self):
        catalog = self.convert()
        market = next(m for m in catalog["regions"] if m["id"] == "domain_1")
        for outpost in market["outposts"]:
            for level in outpost["levels"]:
                level.update(activity_id="fixture_event", time_id="fixture_time")
        with tempfile.TemporaryDirectory() as temp:
            dataset = Path(temp) / "dataset"
            dataset.mkdir()
            importer.atomic_write(dataset / "catalog.json", importer.encode(catalog))
            importer.atomic_write(dataset / "areas.json", importer.encode([]))
            region = {"catalog": "catalog.json", "market_region": "domain_1", "resources": [],
                      "settlement_levels": {"unknown_outpost": 1}}
            importer.atomic_write(dataset / "region.json", importer.encode(region))
            with self.assertRaisesRegex(ValueError, "Unknown outpost"):
                make_fixture.make_fixture(dataset, Path(temp) / "invalid")
            self.assertFalse((Path(temp) / "invalid").exists())
            region.pop("settlement_levels")
            for name, activities, times in (("locked", [], []), ("timed", ["fixture_event"], []),
                                             ("unlocked", ["fixture_event"], ["fixture_time"])):
                region.update(active_activities=activities, active_time_ids=times)
                importer.atomic_write(dataset / "region.json", importer.encode(region))
                output = Path(temp) / name
                make_fixture.make_fixture(dataset, output)
                products = json.loads((output / "products.json").read_bytes())
                if name == "unlocked":
                    self.assertIn({"product": "sc_valley_battery", "value": 30}, products)
                else:
                    self.assertEqual(products, [])

    def test_missing_cache_and_unknown_version_fail_without_output(self):
        with tempfile.TemporaryDirectory() as temp:
            cache, output = Path(temp) / "cache", Path(temp) / "catalog.json"
            with self.assertRaises(OSError): importer.import_catalog(cache, output, offline=True)
            self.assertFalse(output.exists())
            cache.mkdir()
            importer.atomic_write(cache / "manifest.json", importer.encode({
                "schemaVersion": 1, "latest": "test@1", "versions": [self.version]}))
            with self.assertRaises(ValueError):
                importer.import_catalog(cache, output, version_id="missing", offline=True)
            self.assertFalse(output.exists())

    def test_http_errors_and_malformed_response_are_not_empty_data(self):
        for code in (403, 404):
            with patch.object(importer.urllib.request, "urlopen", side_effect=urllib.error.HTTPError(
                    "https://example.invalid", code, "failure", {}, None)) as request:
                with self.assertRaises(urllib.error.HTTPError): importer.fetch_json("https://example.invalid")
                self.assertEqual(request.call_count, 1)
        with self.assertRaises(ValueError): importer.read_json("not json")
        with self.assertRaises(ValueError): importer.read_json('{"value": NaN}')
        with self.assertRaises(ValueError): importer.read_json('{"id": 1, "id": 2}')


if __name__ == "__main__":
    unittest.main()
