# Game data maintenance

## Source and artifacts

The importer reads AKEData's [public data interface](https://github.com/NagiYume/AKEDatabase/blob/main/DATA_API.md). It selects a snapshot through `https://data.akedata.wiki/manifest.json`, follows its `tableCfgPath`, and fetches:

- `FactoryBuildingTable`: dimensions, power, ports, and facility IDs.
- `FactoryMachineCraftTable`: ingredients, outputs, machine IDs, and recipe timing fields.
- `FactoryMachineCraftGroupTable`: crafting-round duration and port/buffer bindings.
- `ItemTable` and `I18nTextTable_EN`: item identity and readable English names.
- `DomainDataTable` and `SettlementBasicDataTable`: regional currencies, all outpost levels, sellable items, prices, event/time conditions, and cash-pool metadata.
- `FactoryItemTable`, `FactoryFuelItemTable`, and `FactoryBatteryItemTable`: item states, transport/region metadata, and fuel specifications.
- Facility specialization tables: mining, power, storage, machine modes, ports, and transport properties. The full source building and recipe records are also retained in each record's `properties` field; per-facility `specializations` and the top-level specialization tables preserve the additional specifications.

`data/catalog.json` is generated. Do not edit it by hand. Schema version 2 contains facilities, recipes, referenced items, regions/outposts, fuel specifications, source URLs, the selected game/hotfix version, and SHA-256 hashes of the original table bytes. Python preserves localization IDs without losing 64-bit integer precision. Full localization tables and descriptions are not copied into the catalog.

`tools/catalog_aliases.json` preserves existing planner names for recognized upstream IDs. New facilities and items use their upstream ID as their planner name. Display names never establish identity. Recipe IDs remain upstream IDs. Alias changes require regenerating the catalog and checking regional references.

## Refresh workflow

```sh
python3 tools/import_catalog.py --latest > changes.json
# Or pin the manifest version ID:
python3 tools/import_catalog.py --version 1.6.4@11529267-11 > changes.json
./build/bin/aic_planner --validate data/valley_iv
ctest --test-dir build --output-on-failure
```

The report lists added, removed, and changed facility, recipe, item, region, and fuel IDs, plus imported recipe mechanics that require further solver support. This is a catalog-level report: runtime dependency and stream-allocation checks can identify additional unsupported chains.

Downloads are cached in `.cache/akedata`. Ordinary online updates fetch the manifest but reuse cached tables for the selected version. Use `--refresh` to redownload those tables; use `--offline` with the same `--cache` directory to regenerate from cached inputs. `--output` and `--aliases` accept alternative paths for review or experiments. A fresh cache cannot regenerate offline. The complete output is replaced atomically only after every table parses and conversion succeeds. A failed or interrupted refresh leaves the previous catalog intact; partial valid cache downloads may be retained.

To review an update before replacing the checked-in artifact:

```sh
python3 tools/import_catalog.py --latest --output .cache/catalog-candidate.json > .cache/changes.json
```

For comparisons against the checked-in catalog, use the default output or first copy the current catalog to the candidate path. Change reports compare against the previous contents of the chosen output path.

## Conversion conventions

- Facility footprint is source `range.width × range.depth`; source `height` is vertical height, not grid depth.
- Planner coordinates are `x = source.x`, `y = depth - 1 - source.z`.
- Source yaw 0/90/180/270 corresponds to +z/+x/-z/-x. Input-port direction is reversed to obtain its outward normal, then z is reflected into planner y. Both `fx` and `fy` are stored explicitly; corner ports are never guessed for imported records.
- Recipe seconds are `progressRound × group.msPerRound / 1000`. `totalProgress` is not interpreted as milliseconds. Verified examples include the two-second refinery cycle and ten-second SC Valley Battery cycle.
- Original port indices, elevations, pipe types, buffer groups, bindings, and environmental requirements are retained. Multiple buffer groups, multiple outputs, pipe requirements, and environments are reported rather than flattened into purported solver support. Convenience ingredient/output arrays accompany their original groups.
- Only the ports for solid transport are passed to placement when all selected recipes for a machine are solid-only. The complete catalog remains unchanged.

## Regional configuration

A catalog-backed region needs a market ID, not a manual list of products and prices:

```json
{
  "base_power": 200,
  "storage": 80000,
  "resources": [
    {"item": "item_originium_ore", "limit": 560},
    {"item": "item_quartz_sand", "limit": 240},
    {"item": "item_iron_ore", "limit": 1080}
  ],
  "catalog": "../catalog.json",
  "market_region": "domain_1"
}
```

`domain_1` is Valley IV and `domain_2` is Wuling in the checked-in source snapshot. `catalog.regions[].products` lists the complete imported product/price coverage; `outposts[].levels[].offers` preserves the exact prices and eligibility conditions. Valley IV has 14 distinct products. Wuling has 12 permanent products plus four event products.

All permanent outposts default to their highest configured level, for the default planner. Optional `settlement_levels` maps outpost IDs to actual levels; zero means locked. The loader uses only that level's offers. `active_activities` and `active_time_ids` explicitly enable conditional offers; their defaults are empty, so event or time-gated offers are retained in the catalog but not assumed active. Different regional currencies never enter the same objective. If more than one eligible outpost buys the same item, the objective uses the highest available price. Cash-pool capacities/replenishment are preserved, but outpost cash limits and liaison bonuses are not yet constraints on the production objective.

Products, sale prices, and battery fuels load directly from the catalog. `data/valley_iv/products.json` and `fuels.json` are no longer needed. Use `aic_planner --list-products data/valley_iv` to inspect the full objective input. Source `FactoryItemTable.value` is a logistics value, not a sale price; only outpost `rewardMoneyCount` supplies sale prices.

All facilities and recipes are candidates by default, filtered by facility placement domains and the region's available machine modes. Optional `enabled_facilities` and `enabled_recipes` constrain those candidates with upstream IDs. Automatic selection balances eligible recipe alternatives, including productive recycling loops, using a continuous occupied-capacity/power objective and rounded machine counts. It does not claim a globally optimal integer factory design. An explicit recipe allowlist retains strict ambiguity checking. External leaves must be declared in the regional supply list; missing recipes never create unlimited free inputs. Productive crop/seed cycles and multi-input intermediates are supported. Pipe, environmental, and multiple-output mechanics are reported as unsupported. The complete sellable product list remains intact.

The protocol stash and depot unloader are explicit catalog infrastructure facilities. Their geometry and power come from the selected records; the unloader output is oriented toward the factory.

Catalog paths are relative to the dataset directory. Resource entries may use upstream item IDs or compatibility names. Supply limits are items per minute; recipe times and fuel durations are seconds. `storage` is per-item capacity with the existing 48-hour horizon. Prices support three decimal places. Power uses integer game units; fuel durations are positive integer seconds with bounded common-denominator scaling. Fractional resource and production-time coefficients are conservatively rounded at a scale of one million.

Depot segments use inclusive endpoints on x=0, x=width, y=0, or y=height. Only one continuous segment per area edge is currently supported. Width and height are integer tiles. Regional factory packing requires usable depot frontage. Standalone plant and seed modules can be generated without depot connections.

Static infrastructure is an `areas.json` `facilities` map from enabled facility IDs or planner names to integer counts. Its power is taken from the selected facility definitions.

The importer records unlock and market data but does not infer the player's current progression, mining availability, or infrastructure counts. Maximum permanent unlocks are the configured default; player-specific supplies and infrastructure remain explicit.

## Fixed test data

`data/test/facilities.json` and its target list intentionally remain small. Online refreshes never modify this fixture. Expand it deliberately when adding a supported facility, and add a regression demonstrating the chain. To regenerate a full catalog for inspection without replacing fixtures, use the importer's `--output` option.

`tests/fixtures/akedata_sample.json` contains only a small set of source mechanics and localization names from snapshot `1.6.4@11529267-11`, used for offline importer regression checks. It is independent of future provider changes.

The runtime accepts JSON datasets. Regional supplies live in `region.json`; separate `minerals.json` and CSV files are not read. Valley IV's market and all 14 sale-product blueprints have regression coverage. Wuling's market is imported, but its manufacturing mechanics are not a complete supported regional dataset. Plant production uses the fixed 15×9 module initialized with one flower per picker, with internal recycling and direct plant output. Seed-only targets retain the smaller picker/planter pair. Both avoid recurring depot supplies for seeds.

To explicitly regenerate a self-contained fixture from a catalog-backed region:

```sh
python3 tools/make_fixture.py --dataset data/valley_iv --output .cache/new-fixture
./build/bin/aic_planner --validate .cache/new-fixture
```

Fixture extraction applies the same outpost levels, activity/time conditions, and unknown-outpost checks as the runtime loader. The destination must not already exist. Review the generated fixture before replacing a checked-in test dataset; extraction is never part of an ordinary catalog refresh.
