# AIC Planner

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Generate factory blueprints and optimize regional production for the Automated Industry Complex in **Arknights: Endfield**.

The C++17 planner uses OR-Tools CP-SAT to place machines, route conveyors, and choose a regional production mix under resource, power, storage, and space constraints. Game data is imported separately, so planning works offline.

## Build and run

```sh
nix develop
configure
build
./build/bin/aic_planner --validate data/test
./build/bin/aic_planner --no-layouts data/test
check
```

The terminal project targets macOS and Linux. `check` configures, builds, and runs the offline regression suite.

Run without `--no-layouts` to show the terminal factory maps. Diagnostics go to stderr; solutions go to stdout. Select diagnostic detail with `--log-level error|warning|info|debug`.

`--validate` checks inputs and material balances without running placement. Invalid datasets and unsupported explicitly selected recipe chains fail validation. With automatic recipes, unsupported targets are reported and counted. Ordinary planning skips unsupported targets and fails if no blueprint can be generated or the regional solve fails. Use `--blueprints-only` to require successful placement and routing for every requested target.

## Update facilities and recipes

The repository contains a generated [catalog](data/catalog.json) from [AKEData](https://github.com/NagiYume/AKEDatabase). Refresh it after a game update:

```sh
python3 tools/import_catalog.py --latest > changes.json
```

Pin a snapshot for reproducible updates, or regenerate it without network access after downloading it once:

```sh
python3 tools/import_catalog.py --version 1.6.4@11529267-11
python3 tools/import_catalog.py --version 1.6.4@11529267-11 --offline
```

Review the change report and catalog diff before committing. New facilities, recipes, and regional sale offers are discovered automatically. Catalog-backed regions use their imported market to build the complete product objective; no manual product/price list is needed. See [data maintenance](docs/DATA.md) for caching, conversion, compatibility names, and adding supported chains.

## Datasets and supported behavior

Inspect every imported regional sale price without running placement:

```sh
./build/bin/aic_planner --list-products data/valley_iv
```

- `data/test` is a fixed, self-contained regression fixture for SC Valley Battery and Ferrium Part production.
- `data/valley_iv` uses every imported permanent sale product for Valley IV (14 in the checked-in snapshot), with prices and fuel specifications loaded from the shared catalog. Outposts default to maximum permanent unlocks. Area capacities and resource budgets are inherited project settings; infrastructure counts are not populated.
- `region.json` stores external supplies, base power, inventory capacity, catalog location, and market region ID. `areas.json` stores building areas and depot segments. Catalog-backed markets derive targets, prices, and fuel data automatically. Self-contained test fixtures retain `products.json`, `fuels.json`, and `facilities.json`.

The planner supports single-output solid-item recipes with multiple ingredients and productive recycling loops. The checked-in Valley IV dataset generates blueprints for all 14 sale products. Per-machine flow allocation checks belt capacities and distinct ports, including parallel belts. Pipe, environmental, and multiple-output mechanics remain unsupported.

Valley IV plant production uses a fixed **15×9 module**: two planting units around one seed picker, with a protocol stash below and an electric pylon beside it. Insert one flower into the picker once. The left planter replenishes the picker; the right planter exports plants. With the recorded two-second recipes and one circulating flower, the conservative output is **15 plants/minute**, without recurring external seeds or depot frontage. Connect the pylon to your power network.

The same machine arrangement and short internal belts are preserved inside manufacturing blueprints; harvested plants feed downstream machines directly. Crop-dependent candidates are sized around one module per crop, and the regional solver can replicate the complete factory. A seed-only target retains the smaller picker/planter pair.

```sh
./build/bin/aic_planner --list-seed-loops data/valley_iv
./build/bin/aic_planner --plant-loop sandleaf data/valley_iv
./build/bin/aic_planner --seed-loop sandleaf_seed data/valley_iv
./build/bin/aic_planner --blueprints-only --no-layouts data/valley_iv
```

Material-balance optimization, facility rounding, placement, and routing generate candidate factory designs. A regional `OPTIMAL` result is scoped to those candidates. Regional optimization defaults to 30 seconds and reports `FEASIBLE` when it finds a solution without proving optimality; increase it with `--solve-seconds 120`. See [the model overview](MODEL.md) and [the explanation of the original blueprint failures](docs/BLUEPRINT_FAILURES.md).

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md) and [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). Code is MIT licensed. Game data and names belong to their respective rights holders; see [NOTICE](NOTICE) for data attribution.
