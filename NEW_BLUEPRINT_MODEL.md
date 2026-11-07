# Mathematical Model: Stage 1 Blueprint Optimization (Subproblem)

This document provides the formal mathematical formulation of the **Blueprint Optimization Model** (Stage 1 Subproblem) for the AIC Planner. Given a single target product $p$, this model determines:
1. The exact recipe dependency Directed Acyclic Graph (DAG), facility instance counts, and flow-sufficient dedicated unloader sizing (eliminating intermediate splitters and mergers).
2. The optimal 2D placement, orientation, and bounding box of all machines on a discrete factory grid using CP-SAT with topological tier constraints.
3. The conflict-free 2.5D conveyor belt routing connecting all facility ports, incorporating planar pin matching, perpendicular overpass bridges, and negotiated congestion resolution.
4. Logic-Based Benders cuts fed back to CP-SAT whenever spatial congestion prevents planar-bridge routing.

---

## 1. Upstream Recipe Graph, Flow Sizing & Dedicated Unloaders

### 1.1 Sets and Indices
- $i \in \mathcal{I}$: set of all items (raw minerals, intermediate parts, end products).
- $m \in \mathcal{M} \subset \mathcal{I}$: set of raw minerals available in the region.
- $f \in \mathcal{F}$: set of all facilities.
- $r \in \mathcal{R}$: set of manufacturing recipes.
  - $\mathcal{I}_r^{in} \subset \mathcal{I}$: ingredients consumed by recipe $r$, each with quantity $q_{r, i}^{in}$.
  - $\mathcal{I}_r^{out} \subset \mathcal{I}$: products yielded by recipe $r$, each with quantity $q_{r, i}^{out}$.
  - $\tau_r$: recipe execution duration (seconds).
  - $f_r \in \mathcal{F}$: facility required to execute recipe $r$.

### 1.2 Upstream Sizing & Flow Conservation
For a chosen target end-product $p \in \mathcal{I} \setminus \mathcal{M}$:
1. **Root Recipe**: Let $r_{root} \in \mathcal{R}$ be the recipe producing $p$.
   - Root cycle time: $T_p = \tau_{r_{root}}$.
   - Root batch output: $Q_p = q_{r_{root}, p}^{out}$.
   - Rated production throughput: $\rho_p = \frac{Q_p}{T_p}$ (items/second).

2. **Flow Demand Propagation (DAG Traversal)**:
   For any item $i \in \mathcal{I}$, let $\phi_i$ be the total required flow rate (items/second).
   - Root demand: $\phi_p = \rho_p$.
   - Ingredient demand for $j \in \mathcal{I}_r^{in}$ supplying recipe $r$:
     $$\phi_j = \sum_{r \in \mathcal{R} : j \in \mathcal{I}_r^{in}} \left( \frac{q_{r, j}^{in}}{\tau_r} \cdot N_r \right)$$
   - Active facility instances $N_r$ executing recipe $r$ (producing item $i$):
     $$N_r = \left\lceil \frac{\phi_i}{q_{r, i}^{out} / \tau_r} \right\rceil$$

3. **Mineral Consumption per Root Batch**:
   For each mineral $m \in \mathcal{M}$:
   $$m_p^m = \phi_m \cdot T_p$$

4. **Facility Counts $fa_p^f$**:
   - For intermediate and root manufacturing facilities $f \in \mathcal{F}_{mfg}$:
     $$fa_p^f = \sum_{r \in \mathcal{R} : f_r = f} N_r$$
   - For protocol stashes (output sink): $fa_p^{\text{protocol\_stash}} = 1$.

5. **Dedicated 1-to-1 Unloader Sizing (Splitter-Free Architecture)**:
      Rather than aggregating mineral flow into a single high-speed belt and relying on complex tree splitters that cause combinatorial routing congestion and throughput bottlenecks, each primary consumer instance $k$ is allocated dedicated unloader dock(s):
   $$u_{m, k} = \max\left(1, \; \left\lceil \frac{\phi_{m, k}^{req}}{C_{belt}} - 10^{-6} \right\rceil \right)$$
   The total number of unloader entities for mineral $m$ is:
   $$fa_p^{\text{unloader}, m} = \sum_{k \in \text{Consumers}(m)} u_{m, k}, \quad fa_p^{\text{unloader}} = \sum_{m \in \mathcal{M}} fa_p^{\text{unloader}, m}$$
   The required depot docking interface length is:
   $$d_p = 3 \cdot fa_p^{\text{unloader}}$$
   Every intermediate machine operates at $100\%$ rated capacity, guaranteeing that the root facility operates at $100\%$ efficiency.

### 1.3 Commodity Stream Network $\mathcal{S}$
Let $\mathcal{S}$ be the set of directed commodity streams to be routed. Each stream $s = (src(s), p_{src}(s), dst(s), p_{dst}(s), \text{item}(s), \phi_s)$ connects an output port to an input port:
1. **Unloader-to-Primary Streams**:
   For each unloader $u$, its single centered output port ($px=1, py=0, fx=0, fy=-1$) feeds directly into the dedicated consumer machine $k$. For $3 \times 3$ primary converters with 3 input ports, the stream connects to the **center input port** ($px=1, py=0, fx=0, fy=-1$ at base rotation).
2. **Intermediate-to-Root Streams (Symmetrical Flank-and-Center Mapping)**:
   When intermediate facilities feed a multi-port root machine (e.g., `packaging_unit` with 6 input ports):
   - Two flank intermediate machines (e.g., 2 `fitting_unit` instances) map to the leftmost port (index 0) and rightmost port (index 5).
   - Center intermediate machines (e.g., 3 `shredding_unit` instances) map sequentially to the center ports (indices 1, 2, 3).
   This preserves geometric symmetry and prevents planar stream crossings.
3. **Root-to-Stash Stream**:
   The output port of the root machine connects to the input port of the `protocol_stash`.

---

## 2. Placed Entities and Port Kinematics

### 2.1 Entity Definitions
Let $\mathcal{E} = \{1, \dots, N\}$ be the set of physical entities placed on a 2D integer grid $\mathcal{G} = [0, W_{grid}-1] \times [0, H_{grid}-1] \subset \mathbb{Z}^2$.
For each entity $e \in \mathcal{E}$:
- Base footprint dimensions: width $w_e^0 \in \mathbb{Z}_{>0}$, height $h_e^0 \in \mathbb{Z}_{>0}$.
- Classification: $\text{type}(e) \in \mathcal{F}$, boolean flag $\text{is\_unloader}(e) \in \{0, 1\}$.
- Set of physical ports $\mathcal{P}_e = \mathcal{P}_e^{in} \cup \mathcal{P}_e^{out}$.
  Each port $j \in \mathcal{P}_e$ has base local coordinate $(px_{e, j}^0, py_{e, j}^0) \in [0, w_e^0-1] \times [0, h_e^0-1]$ and outward facing normal vector $(fx_{e, j}^0, fy_{e, j}^0) \in \{(0, 1), (0, -1), (1, 0), (-1, 0)\}$.

### 2.2 Discrete Rotations and Kinematic Transformations
Each entity $e \in \mathcal{E}$ can be rotated by $\theta_e \in \{0^\circ, 90^\circ, 180^\circ, 270^\circ\}$, modeled via 4 binary decision variables $r_{e, k} \in \{0, 1\}$ ($k \in \{0, 1, 2, 3\}$):
$$\sum_{k=0}^3 r_{e, k} = 1 \quad \forall e \in \mathcal{E}$$

The effective bounding box dimensions $W_e, H_e$ are linear expressions:
$$W_e = (r_{e, 0} + r_{e, 2}) \cdot w_e^0 + (r_{e, 1} + r_{e, 3}) \cdot h_e^0$$
$$H_e = (r_{e, 0} + r_{e, 2}) \cdot h_e^0 + (r_{e, 1} + r_{e, 3}) \cdot w_e^0$$

For each port $j \in \mathcal{P}_e$, the rotated local coordinate $(px_{e, j}, py_{e, j})$ and facing vector $(fx_{e, j}, fy_{e, j})$ are:
$$px_{e, j} = r_{e, 0} \cdot px_{e, j}^0 + r_{e, 1} \cdot py_{e, j}^0 + r_{e, 2} \cdot (w_e^0 - 1 - px_{e, j}^0) + r_{e, 3} \cdot (h_e^0 - 1 - py_{e, j}^0)$$
$$py_{e, j} = r_{e, 0} \cdot py_{e, j}^0 + r_{e, 1} \cdot (w_e^0 - 1 - px_{e, j}^0) + r_{e, 2} \cdot (h_e^0 - 1 - py_{e, j}^0) + r_{e, 3} \cdot px_{e, j}^0$$
$$fx_{e, j} = r_{e, 0} \cdot fx_{e, j}^0 + r_{e, 1} \cdot fy_{e, j}^0 - r_{e, 2} \cdot fx_{e, j}^0 - r_{e, 3} \cdot fy_{e, j}^0$$
$$fy_{e, j} = r_{e, 0} \cdot fy_{e, j}^0 - r_{e, 1} \cdot fx_{e, j}^0 - r_{e, 2} \cdot fy_{e, j}^0 + r_{e, 3} \cdot fx_{e, j}^0$$

### 2.3 World Coordinates & Access Tiles
Let $(X_e, Y_e)$ be the grid coordinate of entity $e$'s bottom-left corner.
- **Port World Coordinate**:
  $$P_{e, j}^x = X_e + px_{e, j}, \quad P_{e, j}^y = Y_e + py_{e, j}$$
- **Doorstep Access Tile** (1 tile immediately in front of port mouth):
  $$A_{e, j}^x = P_{e, j}^x + fx_{e, j}, \quad A_{e, j}^y = P_{e, j}^y + fy_{e, j}$$
- **Multi-Input Approach Corridor Tile** (2nd tile in front of port mouth):
  $$A_{e, j}^{2, x} = P_{e, j}^x + 2 \cdot fx_{e, j}, \quad A_{e, j}^{2, y} = P_{e, j}^y + 2 \cdot fy_{e, j}$$

### 2.4 Top-Down Processing Convention
In the AIC data definitions, standard orientation ($0^\circ$) defines inputs along the South edge ($py=0, fy=-1$) and outputs along the North edge ($py=h-1, fy=1$).
For top-down processing from a top boundary depot:
- **Unloaders** are oriented at $0^\circ$ ($r_{u, 0} = 1$), outputting South ($fy = -1$).
This allows mineral feeds to flow naturally top-to-bottom without circular detour loops.

---

## 3. CP-SAT Master Formulation: Facility Placement

### 3.1 Decision Variables
- $X_e \in [0, W_{grid}-1]$: X-coordinate of entity $e$'s bottom-left origin.
- $Y_e \in [0, H_{grid}-1]$: Y-coordinate of entity $e$'s bottom-left origin.
- $r_{e, k} \in \{0, 1\}$: rotation state binaries ($k \in \{0, 1, 2, 3\}$).
- $S_k \in \{0, \dots, K-1\}$: permutation slot assigned to unloader $k \in \{1, \dots, K\}$.
- $Y_{tier1} \in [0, H_{grid}-1]$: shared vertical coordinate for Tier 1 primary converters.
- $X_{min}, X_{max} \in [0, W_{grid}]$: factory bounding box horizontal extents.
- $Y_{min}, Y_{max} \in [0, H_{grid}]$: factory bounding box vertical extents.
- $W_{box}, H_{box} \in [1, W_{grid}]$: overall factory bounding dimensions.

### 3.2 Constraints

#### 1. Depot Docking with Flexible Slot Permutation
All unloader docks $u_1, \dots, u_K \in \mathcal{E}_{unloader}$ must align contiguously along the top depot edge ($Y = H_{grid} - 1$) with rotation $0^\circ$:
$$Y_{u_k} = H_{grid} - 1, \quad r_{u_k, 0} = 1 \quad \forall k \in \{1, \dots, K\}$$

To preserve contiguous docking while allowing CP-SAT full freedom to order unloaders horizontally to match the positions of downstream consumers:
$$\text{AllDifferent}(S_1, S_2, \dots, S_K)$$
$$X_{u_k} = 3 \cdot S_k \quad \forall k \in \{1, \dots, K\}$$
This guarantees that unloaders form an unbroken contiguous block of width $3K$ along the depot, while allowing optimal assignment of minerals to consumer columns.

#### 2. Exact 2D Physical Non-Overlap
For each entity $e \in \mathcal{E}$, define horizontal and vertical interval variables corresponding to its exact physical footprint:
$$I_e^x = [X_e, X_e + W_e), \quad I_e^y = [Y_e, Y_e + H_e)$$
Enforce exact 2D non-overlap:
$$\text{NoOverlap2D}\left( \{(I_e^x, I_e^y) : e \in \mathcal{E}\} \right)$$
Unlike models with artificial padding ($\delta_e \ge 1$), this allows compatible machines to touch side-by-side along non-port edges (such as a contiguous row of 5 primary machines with zero gap), maximizing spatial compactness.

#### 3. Topological Tier 1 Formulation (Primary Mineral Converters)
Let $\mathcal{E}_{primary} \subset \mathcal{E} \setminus \mathcal{E}_{unloader}$ be the set of intermediate machines directly consuming raw minerals:
- All primary converters share the exact same vertical coordinate:
  $$Y_m = Y_{tier1} \quad \forall m \in \mathcal{E}_{primary}$$
- All primary converters are oriented at $180^\circ$ so that inputs face North toward the unloaders:
  $$r_{m, 2} = 1 \quad \forall m \in \mathcal{E}_{primary}$$
- All downstream entities (secondary converters, root machine, protocol stash) must sit strictly below Tier 1:
  $$Y_e + H_e \le Y_{tier1} \quad \forall e \in \mathcal{E} \setminus (\mathcal{E}_{unloader} \cup \mathcal{E}_{primary})$$
This cleanly segregates the depot feeding zone, guaranteeing unobstructed 1-tile straight drops from unloaders to primary converters without interference from secondary machinery.

#### 4. Root Machine and Protocol Stash Alignment
- The root machine (e.g., `packaging_unit`) is oriented at $180^\circ$ ($r_{root, 2} = 1$), receiving intermediate inputs from the North and discharging the finished product to the South.
- The protocol stash is placed at or below the root facility:
  $$Y_{stash} \le Y_{root}$$

#### 5. Port Access Doorstep Clearance
For every port $j \in \mathcal{P}_e$, the doorstep tile $A_{e, j}$ cannot be covered by the physical body of any other entity $e' \ne e$:
$$(A_{e, j}^x \le X_{e'} - 1) \lor (A_{e, j}^x \ge X_{e'} + W_{e'}) \lor (A_{e, j}^y \le Y_{e'} - 1) \lor (A_{e, j}^y \ge Y_{e'} + H_{e'}) \quad \forall e' \ne e$$

#### 6. Multi-Input Approach Corridor Clearance
For facilities with wide multi-port input interfaces ($|\mathcal{P}_e^{in}| > 2$, such as `packaging_unit`), the 2nd approach tile $A_{e, j}^2$ must also remain unobstructed:
$$(A_{e, j}^{2, x} \le X_{e'} - 1) \lor (A_{e, j}^{2, x} \ge X_{e'} + W_{e'}) \lor (A_{e, j}^{2, y} \le Y_{e'} - 1) \lor (A_{e, j}^{2, y} \ge Y_{e'} + H_{e'}) \quad \forall e' \ne e$$
This guarantees an open 2-tile corridor, preventing neighboring structures from choking parallel incoming conveyor lanes.

#### 7. Active Port Doorstep Distinctness
Let $\mathcal{A}_{active} = \{ A_s^{src}, A_s^{dst} : s \in \mathcal{S} \}$ be the set of all active port doorsteps. For any two distinct stream endpoints $a \ne b$ with $s(a) \ne s(b)$:
$$|A_a^x - A_b^x| + |A_a^y - A_b^y| \ge 1$$
Distinct commodity streams cannot collide on or share the same port doorstep tile.

#### 8. Factory Bounding Box & Upper Bound Envelope
$$X_{min} \le X_e, \quad X_{max} \ge X_e + W_e \quad \forall e \in \mathcal{E}$$
$$Y_{min} \le Y_e, \quad Y_{max} \ge Y_e + H_e \quad \forall e \in \mathcal{E}$$
$$W_{box} = X_{max} - X_{min}, \quad H_{box} = Y_{max} - Y_{min}$$

To prevent solver exploration of overly sparse or bloated layouts, dimensions are bounded by an empirical envelope based on total machine area and depot length:
$$W_{box}, H_{box} \le \min\left( W_{grid}, \; \max\left(3K + 8, \; \left\lceil 2.3 \sqrt{\sum_{e \in \mathcal{E}} w_e^0 h_e^0} \right\rceil \right) \right)$$

### 3.3 Regularized Objective Function
The placement objective minimizes the factory footprint while heavily penalizing conveyor Manhattan distance (wirelength) to induce natural topological clustering:
$$\min \quad 100 \cdot W_{box} + 100 \cdot H_{box} + 5 \sum_{s \in \mathcal{S}} \left( |A_s^{src, x} - A_s^{dst, x}| + |A_s^{src, y} - A_s^{dst, y}| \right)$$

---

## 4. Subproblem: Conveyor Routing & Feasibility Checking

### 4.1 2.5D Layered Routing Graph
Given a candidate placement $(X^*, Y^*, r^*)$, define the routing grid graph $\mathcal{G}_{conv} = (\mathcal{V}_{conv}, \mathcal{A}_{conv})$:
- **Open Grid Cells**: $\mathcal{V}_{open} = [0, W_{grid}-1] \times [0, H_{grid}-1] \setminus \bigcup_{e \in \mathcal{E}} [X_e^*, X_e^* + W_e^* - 1] \times [Y_e^*, Y_e^* + H_e^* - 1]$.
- **State Space**: Each routing state is a 4-tuple $(x, y, \ell, d)$, where:
  - $(x, y) \in \mathcal{V}_{open}$: 2D spatial coordinate.
  - $\ell \in \{0, 1\}$: vertical layer ($0 = \text{Ground Belt}$, $1 = \text{Overpass Bridge}$).
  - $d \in \{N, S, E, W\}$: current travel direction.

### 4.2 Dynamic Planar Pin Matching (Port Order Normalization)
Before routing, when multiple streams converge upon a multi-port destination facility (e.g., `packaging_unit`):
1. The input ports are ordered along their primary bank axis (e.g., strictly ascending by X for horizontal banks).
2. The incoming streams are sorted along the same axis based on the physical coordinate of their source entity origin ($X_{src}$).
3. Stream destination ports are dynamically reassigned to match this ordering:
   $$p_{dst}(s_{(k)}) \leftarrow \text{Port}_{(k)}$$
This guarantees planar pin matching, eliminating artificial crossing twists before path exploration begins.

### 4.3 Conveyor Feasibility & Physical Crossing Rules
A routing solution is a set of directed paths $\Pi = \{\pi_s : s \in \mathcal{S}\}$, where path $\pi_s$ connects $A_s^{src}$ to $A_s^{dst}$.
Let $\mathcal{S}_\ell(x, y)$ denote the set of streams occupying tile $(x, y)$ on layer $\ell$.

1. **Port Doorstep Exclusivity**:
   All stream endpoints must dock exclusively on Ground ($\ell = 0$):
   $$\mathcal{S}_0(A_s^{src}) = \{s\}, \quad \mathcal{S}_0(A_s^{dst}) = \{s\}, \quad \mathcal{S}_1(A_s^{src}) = \emptyset, \quad \mathcal{S}_1(A_s^{dst}) = \emptyset$$
   No other stream $s' \ne s$ may traverse $A_s^{src}$ or $A_s^{dst}$ on either layer.

2. **Perpendicular Bridge Invariant**:
   For any cell $(x, y) \in \mathcal{V}_{open}$:
   $$|\mathcal{S}_0(x, y)| \le 1, \quad |\mathcal{S}_1(x, y)| \le 1$$
   A two-stream crossing ($|\mathcal{S}_0(x, y)| = 1$ and $|\mathcal{S}_1(x, y)| = 1$) is a **valid perpendicular overpass bridge** if and only if:
   - Stream $s_0 \in \mathcal{S}_0(x, y)$ moves straight across $(x, y)$ ($d_0^{in} = d_0^{out}$).
   - Stream $s_1 \in \mathcal{S}_1(x, y)$ moves straight across $(x, y)$ ($d_1^{in} = d_1^{out}$).
   - The streams are strictly orthogonal: $d_0^{in} \perp d_1^{in}$ (one runs North-South, the other East-West).
   - Neither stream is allowed to turn on an overpass bridge tile.
   Any state violating these rules is classified as **conflicted (infeasible)**.

### 4.4 Negotiated Congestion Routing (PathFinder Algorithm)

#### State Transition Costs
For a directed step from state $(x, y, \ell, d)$ to $(nx, ny, \ell', d_{next})$:
$$\text{Cost} = c_{step}(\ell') + c_{turn} \cdot \mathbf{1}_{d_{next} \ne d} + h(nx, ny) + c_{occ}(nx, ny, \ell', d_{next}) \cdot p_k$$
- Base step cost: $c_{step}(0) = 10$ (Ground), $c_{step}(1) = 30$ (Bridge overpass hardware).
- Turn penalty: $c_{turn} = 15$ (strictly forbidden on Layer 1).
- $h(nx, ny)$: historical congestion accumulated across rip-up iterations.
- $c_{occ}$: dynamic occupancy cost:
  - Empty cell: $c_{occ} = 0$.
  - Valid perpendicular bridge on Layer 1 over straight Layer 0: $c_{occ} = 0$.
  - Collision on Layer 0: $c_{occ} = 1000 + 50 \cdot h$.
  - Collision on Layer 1: $c_{occ} = 1500 + 50 \cdot h$.
  - Foreign port doorstep intrusion: $+500$.
- $p_k$: exponential congestion penalty multiplier, scheduled as:
  $$p_0 = 1.0, \quad p_{k+1} = \min(25.0, \; 1.3 \cdot p_k)$$

#### Rip-Up & Reroute Schedule
1. **Initial Pass**: Route all streams independently with $p_0 = 1.0, h = 0$.
2. **Conflict Detection**: Check all grid cells for violations of the Perpendicular Bridge Invariant. If $\mathcal{C}_{conflicts} = \emptyset$, routing is feasible.
3. **History Update**: For all conflicted cells $(x, y) \in \mathcal{C}_{conflicts}$:
   $$h(x, y) \leftarrow h(x, y) + 35$$
4. **Slack-Based Rip-Up Sorting**:
   Conflicted streams are ripped up synchronously. To reroute:
   - On **even iterations**, streams are sorted ascending by routing slack:
     $$\text{Slack}_s = |\pi_s| - \|A_s^{src} - A_s^{dst}\|_1$$
     Tightly constrained streams claim optimal direct corridors first.
   - On **odd iterations**, stream order is randomly shuffled to escape cyclic deadlocks.
5. **Rerouting**: Conflicted streams are rerouted sequentially under updated $h(x, y)$ and $p_k$. The loop repeats for up to $K_{max} = 50$ iterations.

---

## 5. Logic-Based Benders Feedback (Combinatorial Cuts)

If the subproblem cannot achieve conflict-free conveyor routing within $K_{max}$ iterations for placement $(X^*, Y^*, r^*)$, exact combinatorial cuts are generated and added to the CP-SAT placement model:

1. **Exact Pose Match Indicator**:
   Let $\text{Match}(e)$ be a boolean variable representing whether entity $e$ occupies its exact pose from the failed attempt:
   $$\text{Match}(e) \iff (X_e = X_e^*) \land (Y_e = Y_e^*) \land (r_{e, rot(e)^*} = 1)$$

2. **Global No-Good Cut**:
   Forbids the identical full-factory spatial configuration:
   $$\bigvee_{e \in \mathcal{E}} \neg \text{Match}(e)$$

3. **Targeted Absolute Pose Cuts on Conflicted Streams**:
   For each stream $s$ that remained conflicted, connecting source $u = src(s)$ and destination $v = dst(s)$:
   $$\neg \text{Match}(u) \lor \neg \text{Match}(v)$$

4. **Targeted Relative Displacement Cuts**:
   Prevents reproducing the same relative spatial vector $(\Delta x^*, \Delta y^*)$ and orientations between interacting pairs:
   $$\Delta x^* = X_u^* - X_v^*, \quad \Delta y^* = Y_u^* - Y_v^*$$
   $$\neg \left( (X_u - X_v = \Delta x^*) \land (Y_u - Y_v = \Delta y^*) \land (r_{u, rot(u)^*} = 1) \land (r_{v, rot(v)^*} = 1) \right)$$

5. **Bottleneck Quad Cuts**:
   For any specific cell $(x, y) \in \mathcal{C}_{conflicts}$ where two streams $s_1, s_2$ collide:
   $$\neg \text{Match}(src(s_1)) \lor \neg \text{Match}(dst(s_1)) \lor \neg \text{Match}(src(s_2)) \lor \neg \text{Match}(dst(s_2))$$

CP-SAT is re-solved with these cuts and an updated search seed ($42 + 17 \cdot \text{attempt}$) until a placement admitting feasible conveyor paths is proven.

---

## 6. Layout Properties & Empirical Validation

### 6.1 Validation on Benchmark `sc_valley_battery`
Applying this model to `sc_valley_battery` yields the following verified properties:
- **Upstream Structure**: 2 `refining_unit` (Ferrium), 3 `shredding_unit` (Originium), 1 `packaging_unit`, 1 `protocol_stash`.
- **Unloader Allocation**: 2 dedicated Ferrium unloaders + 3 dedicated Originium unloaders = 5 unloaders ($15$ tiles depot length).
- **Splitters / Mergers**: 0 splitters, 0 mergers.
- **Bounding Box**: $15 \times 14$ (Area: $210 \text{ tiles}^2$, Depot: $15 \text{ tiles}$).
- **Production Throughput**: $100\%$ rated capacity ($1.0$ ferrium_part/s, $1.5$ originium_powder/s $\implies$ 1 battery every 10s = 6 batteries/minute per factory).
- **Regional Master Integration**: In the test region, 3 complete factories fit into available PAC areas, producing **18 batteries/min** with a net economic revenue of **495 bills/min** (up from 207 bills/min under the old split throttled model).
