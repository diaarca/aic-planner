# Mathematical Model: Blueprint Generation

Blueprint generation has three stages: steady-state material balance, per-machine conveyor allocation, and physical placement/routing. It produces candidates for the regional master; it does not prove globally optimal factory designs.

## 1. Material balance, including productive loops

Let $i\in I$ be items, $rc\in RC$ eligible single-output recipes, and $m\in M$ explicitly supplied regional resources. Each recipe has cycle time $\tau_{rc}$, ingredient quantities $q^{in}_{rc,i}$, output quantities $q^{out}_{rc,i}$, and a facility with dimensions $w_{rc},h_{rc}$ and power $P_{rc}$.

For target $p$, fix a candidate output rate $\rho_p$ using the highest nominal single-machine rate among its eligible recipes. The seed-loop exception below uses the net closed-circuit rate instead. Restrict the candidate recipe set to upstream dependencies; collect visited items without rejecting a dependency merely because it is cyclic.

Continuous variables $x_{rc}\ge0$ count recipe operations per second. External supplies $u_i\ge0$ exist only for declared resources other than the target. For every item:

$$\sum_{rc\in RC}(q^{out}_{rc,i}-q^{in}_{rc,i})x_{rc}+u_i
=\begin{cases}\rho_p&i=p\\0&i\ne p\end{cases}$$

Undeclared resources have $u_i=0$. An unproductive cycle cannot satisfy positive net target output. A productive plant/seed cycle can, provided it is initialized as described below.

GLOP minimizes occupied machine capacity with small power and external-input terms:

$$\min\sum_{rc\in RC}\tau_{rc}(w_{rc}h_{rc}+0.01P_{rc})x_{rc}+0.001\sum_i u_i$$

Initial installed counts are $N_{rc}=\lceil\tau_{rc}x_{rc}\rceil$. The dedicated seed-module rules can increase these counts. This relaxation and rounding provide a candidate, not an optimum over all integer machine portfolios. The runtime checks numerical tolerances and limits manufacturing entity counts.

For regional optimization, per-item cycle time is $1/\rho_p$, external consumption is $u_i/\rho_p$, and power is conservatively based on installed facility counts. Partial machine utilization is printed in the blueprint legend.

## 2. Fixed plant modules

Recognize the recorded normal-mode recipe pair by facility role and quantities:

- Seed picking: one flower $f$ produces two seeds $s$.
- Planting: one seed $s$ produces one flower $f$.

Each feedback pair contains one dedicated picker and one dedicated recycling planter. A second, harvesting planter consumes the spare seed and exports plants. Its internal flows are fixed: the planter's flower goes only to its picker; one picker seed returns only to that planter; the spare seed feeds the harvesting planter (or the export stash for a seed-only target). Modules are not cross-connected for their return inputs.

Initialize **one flower in each seed-picking unit, once**. With one flower circulating and spare seeds exported immediately, use the conservative serial cycle time:

$$T_s=\tau_{pick}+\tau_{plant},\qquad \rho_s=1/T_s$$

For the current two-second recipes, a pair exports one spare seed every four seconds. No additional circulating stock or startup priority controller is assumed. To support total picker operation rate $x_s$, create $\lceil x_sT_s\rceil$ modules. Each harvesting planter is kept with its dedicated feedback pair. Crop-dependent candidates are scaled down uniformly when necessary so the largest crop demand fits one module; all material rates and target throughput scale together. The regional optimizer can replicate complete factories. Initial-stock instructions count flowers, not recurring seed imports.

A standalone seed target uses this net rate and the same two machines plus a protocol stash for export. It has no external material supply, unloaders, or depot frontage. The standalone plant target instead uses the fixed 15×9 planter/picker/planter layout, with stash and electric pylon below. Its conservative net output is 15 plants/minute for the recorded recipes. In integrated blueprints, plants from the harvesting planter connect directly to other machines. Both cases still require normal electrical power.

## 3. Per-machine conveyor allocation

Instantiate the selected facilities and their actual solid ports. A recipe pool's operation rate is distributed across its installed instances. For each external-resource consumer, allocate enough dedicated unloaders to supply its demand at a belt capacity $C=0.5$ items/second.

Recover small rational flow rates from the material solution and scale to exact integers. For candidate machine connection $e$, use integer flow $F_e$ and Boolean activity $b_e$:

$$b_e\le F_e\le C_{scaled}b_e$$

Every machine's input demand and output supply must be met exactly by its incident flows. The number of active incoming/outgoing belts cannot exceed its actual input/output port counts. Parallel belts are allowed between the same machine pair, with separate ports. Dedicated seed-loop return connections are enforced explicitly.

The CP-SAT allocation objective penalizes belt count, with a small deterministic connection-order term. Only after a balanced allocation is found are distinct physical port indices assigned. It cannot silently reuse ports or exceed belt capacity.

## 4. Placement and port kinematics

Each entity has integer origin $(X_e,Y_e)$ and rotation binaries for $0,90,180,270$ degrees. Rotations transform footprint dimensions, local port coordinates, and outward normals. Optional dimensions and `NoOverlap2D` enforce physical non-overlap. Active port doorsteps must be clear of other entities; wide input banks also reserve the second approach tile. Active doorsteps of distinct streams cannot coincide.

Unloaders align along the top boundary in permuted slots of their actual width. Pure primary resource converters use the existing aligned tier heuristic where it fits. Other manufacturing entities are below that tier. The chosen root and stash use the existing relative placement convention.

For recycling/multi-input networks, use consistent machine orientation, interior margins, two-tile horizontal and three-tile vertical separation, and adjacent seed-module partners with routing space between them. Fixed plant modules override these generic spacing/orientation rules: the central picker faces opposite the two touching planters, and a 15×9 envelope reserves their short belts and pylon. The three internal belt paths and their ports are fixed; unrelated belts cannot use these cells. A spacious dependency-ordered shelf hint helps CP-SAT find a feasible layout; all physical constraints still apply. Cycles are broken only for hint ordering, never for material balance.

Simple chains retain the compact-envelope/full-rotation search. Complex networks can use the full grid and stop after the first placement solution; their objective is routability rather than proving minimum footprint. Default placement attempts allow two seconds for simple networks and at least six seconds for complex networks, with at most 50 placement/routing attempts. The initial grid currently uses the first configured area.

## 5. Conveyor routing and placement feedback

Match both incoming and outgoing port order to reduce crossings immediately outside machines. Reassignment uses only the already selected active port sets, preserving placement clearance constraints.

The negotiated-congestion router searches a 2.5D grid: normal ground belts and straight perpendicular overpass crossings with ground approaches/ramps. It excludes facility footprints, penalizes congestion, and retries conflicted streams with historical penalties and changing order. Each placed candidate allows up to 12 congestion iterations before feedback to placement.

Successful routing records conveyor cells and normalized entity/port coordinates. Failed routing adds exact-placement and heuristic relative/pairwise conflict cuts before another placement attempt. Those heuristic cuts may exclude more than the exact rejected arrangement. Timeout or exhaustion therefore does not prove that an in-game layout is impossible.

## 6. Validation and limits

Regression coverage checks:

- Conservation for all 14 imported Valley IV sale products, including multi-input intermediates and productive crop cycles.
- Rejection of unproductive cycles and undeclared supplies.
- Belt capacities, unique ports, parallel belts, and exact input/output fulfillment.
- Dedicated seed-module feedback, equal return/spare seed quantities, and one startup flower per pair.
- A standalone Sandleaf seed blueprint with zero depot frontage.
- All four Valley IV plants use the exact 15×9 template, pylon, and eight belt cells.
- Physical generation/routing of every Valley IV blueprint, without fixing a particular arrangement.

Pipe, environmental, and multiple-output mechanics are retained by the importer but remain outside this implementation. A routed blueprint is verified against these modeled constraints, not an in-game simulation. Startup inventory and electrical power must be provided by the player.
