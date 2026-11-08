#!/usr/bin/env python3
"""Import versioned AKEData tables into the offline AIC catalog (stdlib only)."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
import urllib.error
import urllib.request

BASE_URL = "https://data.akedata.wiki"
CORE_TABLES = ("FactoryBuildingTable", "FactoryMachineCraftTable",
               "FactoryMachineCraftGroupTable", "ItemTable", "I18nTextTable_EN")
SPECIALIZATION_TABLES = (
    "FactoryPowerPoleTable", "FactoryPowerStationTable", "FactoryHubTable",
    "FactoryStoragerTable", "FactoryFluidContainerTable", "FactoryGasContainerTable",
    "FactoryUndergroundPipeTable", "FactoryFluidPumpOutTable", "FactoryFluidPumpInTable",
    "FactoryFluidSprayTable", "FactoryTransmuterTable", "FactoryMinerTable",
    "FactoryGasMinerTable", "FactoryBattleTable", "FactoryGridBeltTable",
    "FactoryLiquidPipeTable", "FactoryMachineCrafterTable", "FactoryFluidValveTable",
    "FactoryLiquidConnectorTable", "FactoryLiquidRouterTable", "FactoryGridRouterTable",
    "FactoryGridConnecterTable", "FactoryBoxValveTable", "FactoryFluidConsumeTable")
TABLES = CORE_TABLES + ("DomainDataTable", "SettlementBasicDataTable", "FactoryItemTable",
                       "FactoryFuelItemTable", "FactoryBatteryItemTable", "FactoryMachineCraftModeTable") + SPECIALIZATION_TABLES
ROOT = Path(__file__).resolve().parents[1]


def encode(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False,
                       allow_nan=False) + "\n").encode("utf-8")


def read_json(data):
    def reject(value):
        raise ValueError(f"Non-finite JSON number: {value}")
    def pairs(values):
        result = {}
        for key, value in values:
            if key in result:
                raise ValueError(f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(data, parse_constant=reject, object_pairs_hook=pairs)


def atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    name = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            name = stream.name
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if name and os.path.exists(name):
            os.unlink(name)


def fetch_json(url):
    """Verify TLS, bound requests, and retry transient failures only."""
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={
                "User-Agent": "aic-planner-catalog/1.0", "Accept": "application/json"})
            with urllib.request.urlopen(request, timeout=45) as response:
                data = response.read()
            read_json(data)
            return data
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504) or attempt == 2:
                raise
        except (urllib.error.URLError, TimeoutError):
            if attempt == 2:
                raise
        time.sleep(attempt + 1)
    raise RuntimeError("Download failed")


def positive(value, context, zero=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{context}: expected a finite number")
    if value < 0 or (not zero and value == 0):
        raise ValueError(f"{context}: expected {'nonnegative' if zero else 'positive'} number")
    return value


def integer(value, context, zero=False):
    positive(value, context, zero)
    if int(value) != value:
        raise ValueError(f"{context}: expected integer")
    return int(value)


def localized(record, localization, fallback):
    field = record.get("name", {})
    return field.get("text") or localization.get(str(field.get("id"))) or fallback


def convert_port(port, width, depth):
    """Reflect source z into planner y; source input rotations point inward."""
    position = port["trans"]["position"]
    x = integer(position["x"], "port x", zero=True)
    z = integer(position["z"], "port z", zero=True)
    if x >= width or z >= depth:
        raise ValueError("Port outside footprint")
    yaw = port["trans"]["rotation"]["y"]
    if yaw not in (0, 90, 180, 270):
        raise ValueError(f"Unsupported port rotation: {yaw}")
    fx, fz = {0: (0, 1), 90: (1, 0), 180: (0, -1), 270: (-1, 0)}[yaw]
    if not port["isOutput"]:
        fx, fz = -fx, -fz
    result = {"index": port["index"], "x": x, "y": depth - 1 - z,
              "fx": fx, "fy": -fz, "type": "pipe" if port["isPipe"] else "solid",
              "elevation": position["y"], "source_rotation": yaw}
    if not (x + fx < 0 or x + fx >= width or result["y"] - fz < 0 or result["y"] - fz >= depth):
        raise ValueError("Port direction does not face out of footprint")
    return result


def convert(tables, version, hashes, aliases):
    buildings, crafts, groups, items, localization = (tables[name] for name in CORE_TABLES)
    for name, table in tables.items():
        if not isinstance(table, dict) or (name in CORE_TABLES and not table):
            raise ValueError(f"{name}: expected nonempty object")
    used_items = set()
    regions = []
    for domain_id, domain in sorted(tables["DomainDataTable"].items()):
        if domain_id != domain["domainId"]:
            raise ValueError(f"Domain ID mismatch: {domain_id}")
        currency_id = domain["domainGoldItemId"]
        if currency_id not in items:
            raise ValueError(f"Unknown regional currency: {currency_id}")
        used_items.add(currency_id)
        outposts = []
        for settlement_id, settlement in sorted(tables["SettlementBasicDataTable"].items()):
            if settlement["domainId"] != domain_id:
                continue
            if settlement["settlementId"] != settlement_id:
                raise ValueError(f"Settlement ID mismatch: {settlement_id}")
            levels = []
            for level_id, level in sorted(settlement["settlementLevelMap"].items(), key=lambda pair: int(pair[0])):
                level_number = integer(int(level_id), f"{settlement_id} level")
                offers = []
                for item_id, trade in sorted(level["settlementTradeItemMap"].items()):
                    if item_id != trade["itemId"] or item_id not in items:
                        raise ValueError(f"{settlement_id}: unknown or mismatched trade item {item_id}")
                    used_items.add(item_id)
                    offers.append({"id": f"{settlement_id}:{level_number}:{item_id}",
                                   "item_id": item_id, "product": aliases["items"].get(item_id, item_id),
                                   "value": positive(trade["rewardMoneyCount"], item_id + " sale price"),
                                   "activity_id": trade.get("activityId", ""),
                                   "time_id": trade.get("timeId", ""),
                                   "version_start": trade.get("versionStart", ""), "properties": trade})
                levels.append({"level": level_number, "offers": offers,
                               "cash_pool_capacity": positive(level["moneyMax"], "outpost cash capacity", zero=True),
                               "cash_pool_replenishment": positive(level["moneyPeriod"], "outpost cash replenishment", zero=True),
                               "time_id": level.get("timeId", ""), "properties": level})
            if not levels:
                raise ValueError(f"{settlement_id}: no outpost levels")
            outposts.append({"id": settlement_id,
                            "name": localized({"name": settlement["settlementName"]}, localization, settlement_id),
                            "levels": levels})
        regional_products = {}
        for outpost in outposts:
            for level in outpost["levels"]:
                for offer in level["offers"]:
                    product = regional_products.setdefault(offer["item_id"], {
                        "id": offer["item_id"], "product": offer["product"],
                        "display_name": localized(items[offer["item_id"]], localization, offer["item_id"]),
                        "prices": set(), "offer_ids": []})
                    product["prices"].add(offer["value"])
                    product["offer_ids"].append(offer["id"])
        for product in regional_products.values():
            product["prices"] = sorted(product["prices"])
        regions.append({"id": domain_id, "name": localized({"name": domain["domainName"]}, localization, domain_id),
                        "currency_id": currency_id, "outposts": outposts,
                        "products": [regional_products[k] for k in sorted(regional_products)],
                        "machine_modes": domain["machineModeTypeGroup"], "properties": domain})
    known_domains = {region["id"] for region in regions}
    for settlement in tables["SettlementBasicDataTable"].values():
        if settlement["domainId"] not in known_domains:
            raise ValueError("Settlement references unknown domain")
    facilities = []
    for identity, building in sorted(buildings.items()):
        if building["id"] != identity:
            raise ValueError(f"Building ID mismatch: {identity}")
        footprint = building["range"]
        width = integer(footprint["width"], identity + " width")
        depth = integer(footprint["depth"], identity + " depth")
        ports = {}
        for side in ("input", "output"):
            raw_ports = sorted(building[side + "Ports"], key=lambda p: p["index"])
            if [p["index"] for p in raw_ports] != list(range(len(raw_ports))):
                raise ValueError(f"{identity}: non-contiguous {side} port indices")
            ports[side] = [convert_port(p, width, depth) for p in raw_ports]
        facilities.append({"id": identity, "name": aliases["facilities"].get(identity, identity),
                           "display_name": localized(building, localization, identity),
                           "power": positive(building["powerConsume"], identity + " power", zero=True),
                           "width": width, "height": depth,
                           "inputs": ports["input"], "outputs": ports["output"],
                           "place_domains": building.get("placeDomains", []),
                           "properties": building,
                           "specializations": {name: table[identity] for name in SPECIALIZATION_TABLES
                                               if identity in (table := tables[name])}})
    by_id = {f["id"]: f for f in facilities}
    recipes = []
    for identity, craft in sorted(crafts.items()):
        if craft["id"] != identity:
            raise ValueError(f"Recipe ID mismatch: {identity}")
        machine = craft["machineId"]
        group_id = craft["formulaGroupId"]
        if machine not in by_id or group_id not in groups:
            raise ValueError(f"{identity}: unresolved machine or group")
        group = groups[group_id]
        if identity not in group["craftList"]:
            raise ValueError(f"{identity}: absent from its recipe group")
        # progressRound counts crafting rounds; totalProgress is an animation/progress measure.
        duration = (positive(craft["progressRound"], identity + " rounds") *
                    positive(group["msPerRound"], group_id + " msPerRound") / 1000)
        converted = {}
        for source, target in (("ingredients", "ingredients"), ("outcomes", "products")):
            batches = []
            for batch in craft[source]:
                values = []
                for value in batch["group"]:
                    item_id = value["id"]
                    if item_id not in items:
                        raise ValueError(f"{identity}: unknown item {item_id}")
                    used_items.add(item_id)
                    values.append({"id": item_id, "item": aliases["items"].get(item_id, item_id),
                                   "amount": positive(value["count"], identity + " quantity")})
                if not values:
                    raise ValueError(f"{identity}: empty {source} group")
                batches.append(values)
            if not batches:
                raise ValueError(f"{identity}: empty {source}")
            converted[target] = batches
        reasons = []
        if craft["gasEnv"]:
            reasons.append("environment requirement")
        if group.get("pipeIngredientBufferBinding") or group.get("pipeOutcomeBufferBinding"):
            reasons.append("pipe transport")
        if len(converted["ingredients"]) != 1 or len(converted["products"]) != 1:
            reasons.append("multiple buffer groups")
        if len(converted["products"][0]) != 1:
            reasons.append("multiple outputs")
        recipes.append({"id": identity, "name": identity, "facility_id": machine,
                        "group_id": group_id, "time": duration,
                        "ingredients": [x for batch in converted["ingredients"] for x in batch],
                        "products": [x for batch in converted["products"] for x in batch],
                        "ingredient_groups": converted["ingredients"],
                        "product_groups": converted["products"],
                        "port_bindings": {k: group.get(k, []) for k in (
                            "ingredientBufferBinding", "outcomeBufferBinding",
                            "pipeIngredientBufferBinding", "pipeOutcomeBufferBinding")},
                        "environment": craft["gasEnv"], "unsupported_reasons": reasons,
                        "modes": [mode["modeName"] for mode in tables["FactoryMachineCrafterTable"].get(machine, {}).get("modeMap", [])
                                  if mode["groupName"] == group_id], "properties": craft})
    for category, records in (("facilities", facilities), ("items", [
            {"id": identity, "name": aliases["items"].get(identity, identity)}
            for identity in sorted(used_items)])):
        names = [record["name"] for record in records]
        if len(names) != len(set(names)):
            raise ValueError(f"Duplicate compatibility name in {category}")
    fuels = []
    for item_id, fuel in sorted(tables["FactoryFuelItemTable"].items()):
        if item_id not in items:
            raise ValueError(f"Unknown fuel item: {item_id}")
        used_items.add(item_id)
        station = tables["FactoryPowerStationTable"].get("power_station_1")
        if not station:
            raise ValueError("Missing thermal-bank timing specification")
        fuels.append({"id": item_id, "fuel": aliases["items"].get(item_id, item_id),
                      "power": positive(fuel["powerProvide"], "fuel power"),
                      "duration": positive(fuel["progressRound"], "fuel rounds") *
                                  positive(station["msPerRound"], "thermal bank round time") / 1000,
                      "properties": fuel})
    return {"schema_version": 2,
            "source": {"provider": "AKEData", "version": version["id"],
                       "tables": {name: {"url": f'{BASE_URL}/{version["tableCfgPath"]}/{name}.json',
                                         "sha256": hashes[name]} for name in TABLES}},
            "facilities": facilities, "recipes": recipes, "regions": regions, "fuels": fuels,
            "specializations": {name: tables[name] for name in SPECIALIZATION_TABLES},
            "items": [{"id": identity, "name": aliases["items"].get(identity, identity),
                       "display_name": localized(items[identity], localization, identity),
                       "factory_properties": tables["FactoryItemTable"].get(identity, {})}
                      for identity in sorted(used_items)]}


def change_report(previous, current):
    report = {}
    for section in ("facilities", "recipes", "items", "regions", "fuels"):
        old = {row["id"]: row for row in previous.get(section, [])}
        new = {row["id"]: row for row in current.get(section, [])}
        report[section] = {"added": sorted(new.keys() - old.keys()),
                           "removed": sorted(old.keys() - new.keys()),
                           "changed": sorted(k for k in new.keys() & old.keys() if new[k] != old[k])}
    report["unsupported_recipes"] = {r["id"]: r["unsupported_reasons"] for r in current["recipes"]
                                     if r["unsupported_reasons"]}
    return report


def import_catalog(cache, output, version_id=None, offline=False, refresh=False,
                   aliases_path=ROOT / "tools/catalog_aliases.json"):
    cache, output = Path(cache), Path(output)
    manifest_path = cache / "manifest.json"
    if offline:
        manifest = read_json(manifest_path.read_bytes())
    else:
        raw = fetch_json(BASE_URL + "/manifest.json")
        manifest = read_json(raw)
        if manifest.get("schemaVersion") != 1:
            raise ValueError("Unsupported manifest schema")
        atomic_write(manifest_path, raw)
    if manifest.get("schemaVersion") != 1:
        raise ValueError("Unsupported manifest schema")
    selected_id = version_id or manifest["latest"]
    matches = [v for v in manifest["versions"] if v["id"] == selected_id]
    if len(matches) != 1:
        raise ValueError(f"Unknown or duplicate version: {selected_id}")
    selected = matches[0]
    # Paths come from the manifest, but must remain within the provider's public tree.
    if not selected["tableCfgPath"].startswith("public/") or ".." in selected["tableCfgPath"].split("/"):
        raise ValueError("Invalid TableCfg path")
    snapshot_key = hashlib.sha256(selected_id.encode()).hexdigest()[:20]
    tables, hashes = {}, {}
    for name in TABLES:
        path = cache / snapshot_key / (name + ".json")
        if offline or (path.exists() and not refresh):
            raw = path.read_bytes()
        else:
            raw = fetch_json(f'{BASE_URL}/{selected["tableCfgPath"]}/{name}.json')
            atomic_write(path, raw)
        tables[name] = read_json(raw)
        hashes[name] = hashlib.sha256(raw).hexdigest()
    aliases = read_json(Path(aliases_path).read_bytes())
    catalog = convert(tables, selected, hashes, aliases)
    previous = read_json(output.read_bytes()) if output.exists() else {}
    report = change_report(previous, catalog)
    # The catalog is a single artifact: replace it only after all conversion succeeds.
    atomic_write(output, encode(catalog))
    return catalog, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--version", help="Manifest version ID, e.g. 1.6.4@11529267-11")
    selection.add_argument("--latest", action="store_true", help="Resolve the manifest's latest version")
    parser.add_argument("--offline", action="store_true", help="Use previously cached manifest and tables")
    parser.add_argument("--refresh", action="store_true", help="Redownload cached tables (not valid offline)")
    parser.add_argument("--cache", type=Path, default=ROOT / ".cache/akedata")
    parser.add_argument("--output", type=Path, default=ROOT / "data/catalog.json")
    parser.add_argument("--aliases", type=Path, default=ROOT / "tools/catalog_aliases.json")
    args = parser.parse_args()
    if args.offline and args.refresh:
        parser.error("--offline and --refresh are mutually exclusive")
    try:
        catalog, report = import_catalog(args.cache, args.output, args.version,
                                         args.offline, args.refresh, args.aliases)
        print(encode({"version": catalog["source"]["version"], "changes": report}).decode(), end="")
        print(f"Wrote {len(catalog['facilities'])} facilities and {len(catalog['recipes'])} recipes to {args.output}", file=sys.stderr)
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"error: catalog import failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
