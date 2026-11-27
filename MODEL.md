# Planner model overview

The JSON-only planner uses two stages:

1. [Blueprint generation](NEW_BLUEPRINT_MODEL.md): balance upstream material flows, including productive recycling loops, instantiate facilities and allocate distinct material ports, place machines with CP-SAT, and route belts and pipes with a bounded congestion search. Routing conflicts trigger placement retries.
2. [Regional optimization](NEW_MASTER_MODEL.md): choose production rates, battery use, and placements of the generated blueprints under regional supply, storage, power, depot, and non-overlap constraints.

A generated blueprint is a candidate under the implemented modeling assumptions. Valley IV allows two seconds per placement attempt for simple chains and at least six seconds for complex chains. Wuling uses a shared budget per area and placement strategy. Both modes limit the number of placement and routing retries. Failure can mean timeout or routing-search exhaustion; it does not establish that the game admits no factory. Some retry cuts and topology restrictions are heuristic.

Regional targets and prices are derived from every eligible imported sale offer. Material planning minimizes a continuous occupied-capacity/power objective and rounds facility counts. The regional solver receives routed candidates with the final production facility at full utilization. Wuling generates candidates per area so site-dependent inputs and depot attachments stay at their verified locations. Unsupported targets remain in the imported market and are reported explicitly. The solver reports the actual CP-SAT status. An `OPTIMAL` solution is optimal over these candidates and the modeled region. Production rates are integer items per minute. Fractional recipe consumption and per-unit cycle times use conservative fixed-point rounding, and prices retain up to three decimal places.

Regional solid supplies are declared in `region.json`. Liquid and gas extraction uses bounded sites in `areas.json`. Missing recipes or local sources leave the corresponding supply unavailable. [Wuling setup](docs/WULING.md) The importer preserves upstream mechanics that the solver cannot yet model; see [supported behavior](README.md#datasets-and-supported-behavior) and [data conventions](docs/DATA.md).

Plant modules use a dedicated picker/planter pair that replenishes itself and supplies spare seeds to a harvesting planter. The picker and planter can work concurrently after warmup. The slower recipe or the single belt on each connection limits throughput to 30 plants/minute for the recorded recipes. The model lists startup flowers to stock the concurrent recipe cycles and requires no recurring seed supply or depot frontage. The fixed 15×9 layout includes a stash and an Electric Pylon. Seed-only targets retain the two-machine pair and export 30 seeds/minute. Crop-dependent factories include enough modules to supply the full-speed final recipe. The regional solver may replicate complete factories.

The regional solve uses safe interchangeable-slot ordering and a default 30-second budget. A time-limited incumbent is reported as `FEASIBLE`. Adjust the regional budget with `--solve-seconds`.
