#!/usr/bin/env python3
"""Explicitly extract a self-contained fixture from a catalog-backed dataset."""
import argparse
from pathlib import Path
import os
import shutil
import sys
import tempfile

from import_catalog import atomic_write, encode, read_json


def select_market_products(market, region):
    products = {}
    active_activities = set(region.get("active_activities", []))
    active_times = set(region.get("active_time_ids", []))
    levels = region.get("settlement_levels", {})
    outpost_ids = {outpost["id"] for outpost in market["outposts"]}
    if set(levels) - outpost_ids:
        raise ValueError("Unknown outpost in market configuration")
    def available(record):
        return (not record.get("activity_id") or record["activity_id"] in active_activities) and (
            not record.get("time_id") or record["time_id"] in active_times)
    for outpost in market["outposts"]:
        requested = levels.get(outpost["id"], outpost["levels"][-1]["level"])
        if requested == 0:
            continue
        selected = next((level for level in outpost["levels"] if level["level"] == requested), None)
        if selected is None:
            raise ValueError("Unknown outpost level")
        if not available(selected):
            continue
        for offer in selected["offers"]:
            if not available(offer):
                continue
            products[offer["product"]] = max(products.get(offer["product"], 0), offer["value"])
    return products


def make_fixture(dataset, output):
    dataset, output = Path(dataset), Path(output)
    if output.exists():
        raise ValueError("Output must be a new directory; existing fixtures are never overwritten")
    region = read_json((dataset / "region.json").read_bytes())
    catalog = read_json((dataset / region["catalog"]).read_bytes())
    if catalog["schema_version"] != 2:
        raise ValueError("Unsupported catalog schema")
    market = next((m for m in catalog["regions"] if m["id"] == region.get("market_region")), None)
    if region.get("market_region") and market is None:
        raise ValueError("Unknown market region")
    explicit_facilities = region.pop("enabled_facilities", [])
    explicit_recipes = region.pop("enabled_recipes", [])
    enabled_facilities = explicit_facilities or [f["id"] for f in catalog["facilities"] if not market or
        not f["place_domains"] or market["id"] in f["place_domains"]]
    enabled_recipes = explicit_recipes or [r["id"] for r in catalog["recipes"] if r["facility_id"] in enabled_facilities and
        (not market or any(mode in market["machine_modes"] for mode in r["modes"]))]
    region["automatic_recipes"] = not explicit_recipes
    if len(set(enabled_facilities)) != len(enabled_facilities) or len(set(enabled_recipes)) != len(enabled_recipes):
        raise ValueError("Duplicate enabled IDs")
    facilities = {f["id"]: dict(f, recipes=[]) for f in catalog["facilities"] if f["id"] in enabled_facilities}
    recipes = {r["id"]: r for r in catalog["recipes"] if r["id"] in enabled_recipes}
    if set(facilities) != set(enabled_facilities) or set(recipes) != set(enabled_recipes):
        raise ValueError("Unknown enabled facility or recipe")
    for recipe in recipes.values():
        if recipe["facility_id"] not in facilities:
            raise ValueError("Recipe uses disabled facility")
        facilities[recipe["facility_id"]]["recipes"].append(recipe)
    for facility in facilities.values():
        solid_only = all(not recipe["port_bindings"][side] for recipe in facility["recipes"]
                         for side in ("pipeIngredientBufferBinding", "pipeOutcomeBufferBinding"))
        if solid_only:
            for side in ("inputs", "outputs"):
                facility[side] = [p for p in facility[side] if p["type"] != "pipe"]
        facility["recipes"].sort(key=lambda r: r["id"])
    item_names = {item["id"]: item["name"] for item in catalog["items"]}
    for resource in region["resources"]:
        resource["item"] = item_names.get(resource["item"], resource["item"])
    region.pop("catalog")
    region["fixture_source"] = catalog["source"]
    # Parse all ancillary files before creating any output.
    files = {"areas.json": read_json((dataset / "areas.json").read_bytes())}
    if market:
        region.pop("market_region")
        region["currency"] = market["currency_id"]
        products = select_market_products(market, region)
        files["products.json"] = [{"product": name, "value": value} for name, value in sorted(products.items())]
        files["fuels.json"] = [fuel for fuel in catalog["fuels"] if fuel["fuel"] in products]
    else:
        files.update({name: read_json((dataset / name).read_bytes()) for name in ("products.json", "fuels.json")})
    files["region.json"] = region
    files["facilities.json"] = [facilities[k] for k in sorted(facilities)]
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".aic-fixture-", dir=output.parent))
    try:
        for name, data in files.items():
            atomic_write(temporary / name, encode(data))
        os.rename(temporary, output)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        make_fixture(args.dataset, args.output)
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"error: fixture extraction failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
