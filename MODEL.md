# Planner model overview

The JSON-only planner uses two stages:

1. [Blueprint generation](NEW_BLUEPRINT_MODEL.md): balance upstream material flows, including productive recycling loops, instantiate facilities and allocate distinct conveyor ports, place machines with CP-SAT, and route solid conveyors with a bounded congestion search. Routing conflicts trigger placement retries.
2. [Regional optimization](NEW_MASTER_MODEL.md): choose production rates, battery use, and placements of the generated blueprints under regional supply, storage, power, depot, and non-overlap constraints.

A generated blueprint is a candidate under the implemented modeling assumptions. Placement defaults to two seconds per attempt for simple chains and at least six seconds for complex recycling/multi-input networks, with at most 50 placement/routing attempts. Failure can mean timeout or routing-search exhaustion; it does not establish that the game admits no factory. Some retry cuts and topology restrictions are heuristic.

Regional targets and prices are derived from every eligible imported sale offer. Material planning minimizes a continuous occupied-capacity/power objective and rounds facility counts, and the regional solver receives one candidate per successfully routed target. Unsupported targets remain in the imported market and are reported explicitly. Its reported status is the actual CP-SAT result. `OPTIMAL` is scoped to those candidates and the modeled region, not all factory layouts or alternative recipes. Production rates are integer items per minute. Fractional recipe consumption and per-unit cycle times use conservative fixed-point rounding; prices retain up to three decimal places.

Regional resource supplies are declared in `region.json`. Missing recipes do not imply unlimited external supply. The importer preserves upstream mechanics that the solver cannot yet model; see [supported behavior](README.md#datasets-and-supported-behavior) and [data conventions](docs/DATA.md).

Plant modules are an explicit exception to tree-shaped recipe analysis: a dedicated picker/planter pair replenishes itself and supplies its spare seeds to a second, harvesting planter. A fixed 15×9 layout includes the export stash and electric pylon. Initialize each picker with one flower once; no recurring seed supply or depot frontage is required. With one flower circulating, the conservative net plant rate uses the sum of picking and planting times (15 plants/minute for the recorded recipes). Crop-dependent candidates are scaled around one module per crop, and the regional solver may replicate complete factories. Seed-only targets retain the two-machine pair.

The regional solve uses safe interchangeable-slot ordering and a default 30-second budget. A time-limited incumbent is reported as `FEASIBLE`, not `OPTIMAL`; `--solve-seconds` adjusts that budget.
