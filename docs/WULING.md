# Configure Wuling

Wuling uses the shared AKEData catalog. The shipped dataset identifies the region and loads its sale offers, facilities, and recipes. Enter the geometry and supplies from your game before asking the planner to generate layouts.

```sh
./build/bin/aic_planner --list-products data/wuling
./build/bin/aic_planner --validate data/wuling
./build/bin/aic_planner --product item_copper_cmpt --blueprints-only data/wuling
./build/bin/aic_planner --no-layouts data/wuling
```

The pinned snapshot is `1.7.2@12153128-4`. It contains 12 permanent Wuling sale targets and four activity targets. Select active offers with the same controls as Valley IV. [Catalog maintenance](DATA.md) · [AKEData manifest](https://data.akedata.wiki/manifest.json)

## Enter regional values in `data/wuling/region.json`

| Key | What to enter | Where to find it |
| --- | --- | --- |
| `base_power` | Integer grid power already available before adding the generated factories. Use zero if the model should start with no available power. | Read available fixed generation from the PAC power panel. Account for fixed generation and loads exactly once. Enter fixed facility loads under the relevant area's `facilities`. |
| `storage` | Positive per-item Depot storage capacity, in units. | Check the Depot stack or regional storage limit. The planner uses its existing 48-hour inventory horizon. |
| `resources` | One entry per solid item that reaches the Wuling Depot independently of the generated factories. Each entry has `item` and a sustainable `limit` in units/min. | Read extraction rates or measured unloading rates. Include net recurring imports only if you can sustain the stated rate. Common inputs include `item_copper_ore`, `item_carbon_mtl`, `ferrium_ore`, and `originium_ore`. |
| `facility_limits` | Optional regional placement limits by facility name. Set zero for a locked facility you want the regional optimizer to exclude. | Read the current placement cap from AIC technology or the build menu. The catalog provides the base Forge of the Sky limit. Override `xiranite_oven_1` when research increases it. |
| `settlement_levels` | Optional map of outpost ID to actual level. Level zero locks an outpost. | Read the stock redistribution terminals. The default uses each permanent outpost's highest catalog level. |
| `enabled_facilities`, `enabled_recipes` | Optional allowlists of upstream IDs for the unlocked facilities and formulas to consider. | Use your Wuling AIC plan and the shared catalog. |
| `rebuilding_orders` | Optional map from planner item name to required delivery rate in whole units/min. | Read the phase's accepted supplies and choose the delivery rate for your planned production. Configure a Rebuilding Supply Port in an eligible area as well. |

If you are configuring an area that requires manually connected pylons, provide an operating power connection in the game. The planner checks pylon coverage and the regional power budget. [In-game factory tutorials](https://endfieldtools.dev/guides/tutorials/factory)

## Enter local geometry in `data/wuling/areas.json`

Add one object per Core AIC Area you plan to use. The file is a JSON array. Count the buildable grid tiles for `pac_width` and `pac_height`; give the area a unique name in `area`. Use the area's southwest tile as `(0, 0)`. Increasing `x` moves east and increasing `y` moves north. For each facility, enter the southwest corner of its rotated footprint. Rotation is clockwise in quarter turns from the orientation in the catalog. The catalog's port locations and directions define the reference orientation.

| Key | What to enter | Where to find it |
| --- | --- | --- |
| `depot_bus` | Placement of each Depot Bus Port (`log_hongs_bus_source`) and Section (`log_hongs_bus`). Each entry uses `facility`, `x`, `y`, and `rotation`. | Place or preview a bus in-game. Record the grid coordinates of connected pieces and leave room on exposed sides for loaders, unloaders, and their routes. The planner attaches unloaders to this configured bus. |
| `output_to_depot_bus` | Optional boolean. Set to `true` to use a Depot Loader as the final output of each sale factory. The default uses a Protocol Stash. | Leave one more unoccupied attachment position on the connected bus for each factory using a Depot Loader. |
| `allow_pipe_over_belt` | Optional boolean. Set to `true` to allow perpendicular crossings where a straight elevated pipe can pass over a straight belt. It defaults to `false`. | Confirm the crossing is buildable in the area. Pipe supports and machine ports need clear tiles. |
| `sources` | One entry per usable extractor installation. Enter `item`, `facility`, `x`, `y`, `rotation`, and `limit` in units/min. | Preview the pump or extractor to confirm the footprint, inlet, source material, and valid rotation. Use `pump_1` for compatible liquids, `pump_2` for Precipitation Acid, and `gas_pump_1` for gas vents. Use exact catalog IDs such as `item_liquid_water`, `item_gas_inert`, and `item_gas_xiranite`. The loader checks rates against extractor specifications. |
| `climates` | One entry per climate you want to use. Enter the recipe's `environment` number, the matching `gas` item ID, the disperser's `rate` in whole units/min, and coverage extensions `range_x`, `range_y` in tiles beyond each side of its footprint. | Read `environment` in the catalog recipe. Confirm the gas, its required rate, and the field size in the in-game Dispersing Unit or factory simulation. Stable uses Inergen. An Acrid recipe requires its own compatible feed and coverage. |
| `array_fields` | A map keyed by `L`, `T`, or `E`. Enter the `belt_rotation` from eastbound, the sustainable `cycle_seconds` per conversion, and `projectors` with `mode`, `dx`, `dy`, `rotation`, and Xiragen `feed` in whole units/min. Offsets are from the conversion belt tile to the southwest corner of the forge. An E field requires both matching L and T projections. | In the Array AIC mode, place and activate a Forge of Luminance. Read the covered grid tiles, check the intended overlapping fields, and measure sustained conversion with the planned belt direction and feed. Use a conservative cycle time for chance-based conversions. |
| `rebuilding_port` | The port's `x`, `y`, and `rotation` if this area accepts configured rebuilding deliveries. | Place the Rebuilding Supply Port in an eligible area and check that the project accepts the requested item. |
| `facilities`, `power_pylon` | Optional fixed infrastructure counts by facility name and a pylon ID for generated coverage. Wuling can use `power_diffuser_2` for the Xiranite Pylon. | Read the existing installations and unlocked pylon type in-game. |

An area with no gas or liquid installations can omit those sources or use an empty list. Pipe recipes that depend on a missing source need an alternative recipe or another configured area. For a source outside the buildable rectangle, you can place a connected Conduit Outlet inside the area and use it as the source. Enter `udpipe_unloader_1` or `udpipe_unloader_2` as the facility, give it a region-wide unique `conduit_connection` name, and enter the verified rate delivered by the external connection. If several connections draw from the same upstream production, divide the sustainable rate among them.

The repository includes a [synthetic fixture](../tests/fixtures/wuling/areas.json) that illustrates the file structure. Its dimensions, source coordinates, climate range, and projector geometry are test values. [In-game factory tutorials](https://endfieldtools.dev/guides/tutorials/factory) · [Wuling simulation dialogue](https://endfieldtools.dev/story/factory-simulation-wuling/)

## Read the resulting plans

An area-bound blueprint reports its absolute origin. Add that origin to each normalized tile position in the map. Pipe routes are listed separately in the summary and use the item colors on the map. The facility legend gives the flow settings for activation feeds and the mode of array projectors. A regional model can choose only candidates built for the relevant area. Factories that rely on a specific source or bus location reserve their footprint at that location. Rebuilding deliveries have a separate configured demand and do not earn stock bills.

`--validate` checks material balances and reports how many targets it can balance. Use `--blueprints-only` to require physical routes for every requested target. Increase `--placement-seconds` for a difficult blueprint. EnKAD export covers compatible solid Valley IV layouts.

The model uses rectangular build areas and configured bus placements. It treats extractors and conduit outlets as local entry points and places each area-bound candidate at most once. Current coverage includes pipe splitters, convergers, bridges, waste treatment, and flow controls. Depot Bus trunks and upstream conduit connections are supplied by the player. Bipolar projector optimization and chance-based byproducts are outside the current routing model. Array throughput uses the cycle time you provide. The regional objective uses eligible sale prices before outpost cash limits and liaison bonuses.
