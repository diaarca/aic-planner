# Mathematical Model: AIC Planner

This document provides the detailed Mixed-Integer Linear Programming (MILP) representation used by the AIC Planner to optimize production.

## Data

### The Region Related Data
- $r \in R$: the region
- $bp_r$: the base power provided in the region $r$
- $s_r$: the per element storage in the region $r$

### The Mineral Related Data
- $m \in M_r$: the minerals of the region $r$
- $l_m$: the limit of the mineral $m$ in the region

### The Product Related Data
- $p \in P_r$: the products of the region $r$
- $v_p$: the value of the product $p$ (in stock bills)
- $m_p^m$: the quantity of mineral $m$ needed for the product $p$
- $t_p$: the production time needed for the product $p$
- $d_p$: the depot length needed for the product $p$'s factory
- $w_p$: the width length needed for the product $p$'s factory
- $h_p$: the height length needed for the product $p$'s factory
- $fa_p^{fa}$: the number of facility $fa$ needed for the product $p$ factory

### The Fuel Related Data
- $fu \in FU_r \subseteq P_r$: the fuel of the region $r$
- $p_{fu}$: the power that the fuel $fu$ provides
- $d_{fu}$: the duration during which the fuel $fu$ stands before being consumed

### The Area Related Data
- $a \in A_r$: the areas of the region $r$
- $fa_a^fa$: the number of facility $fa$ needed in area $a$
- $w_a$: the PAC width in the area $a$
- $h_a$: the PAC height in the area $a$
- $wd_a$: the PAC depot width in the area $a$
- $hd_a$: the PAC depot height in the area $a$

### The Facility Related Data
- $fa \in FA_r$: the facilities of the region $r$
- $p_{fa}$: the power usage of the facility $fa$

## Variables
- $\text{produce}_p$: the quantity of product $p$ to be produced (per minute)
- $\text{factory}_p^a$: the number of factory for product $p$ to be place in area $a$
- $\text{active}_{fu}$: the quantity of fuel $fu$ that must be active for providing power

## Objective
$$\max{\left[\sum_{p\in P_r} \left(produce_p \cdot v_p\right) - \sum_{fu\in FU_r} \left(active_{fu} \cdot \dfrac{60}{d_{fu}} \cdot v_{fu}\right) \right]}$$

## Constraints

- **Fuel Balance:**
  $$produce_{fu} - \dfrac{60}{d_{fu}} \cdot active_{fu} \geq 0 \quad \forall fu\in FU_r$$
  (cannot consume more fuel than produced)

- **Storage Capacity:**
  $$produce_{p} \leq \dfrac{s_r}{48 \cdot 60} \quad \forall p\in P_r\setminus FU_r$$
  (prevent filling the region storage within 48 hours)

- **Mineral Limits:**
  $$\sum_{p\in P_r} \left(produce_p \cdot m_p^m \right) \leq l_m\quad \forall m\in M_r$$

- **Production Capacity:**
  $$produce_p - \sum_{a\in A_r}\left(factory_p^a \cdot \dfrac{60}{t_p}\right)\leq 0\quad\forall p\in P_r$$
  (fix the number of product factory based on its production)

- **Area Space:**
  $$\sum_{p\in P_r}\left(factory_p^a \cdot w_p \cdot h_p\right)\leq w_a \cdot h_a\quad\forall a\in A_r$$

- **Depot Length:**
  $$\sum_{p\in P_r}\left(factory_p^a \cdot d_p\right)\leq wd_a + hd_a\quad\forall a\in A_r$$

- **Power Consumption:**
  $$\sum_{a\in A_r}\left(\sum_{fa\in FA_r}\left(fa_a^{fa} \cdot p_{fa}\right) + \sum_{p\in P_r}\left(factory_p^a \cdot \sum_{fa\in FA_r}\left(fa_p^{fa} \cdot p_{fa}\right)\right)\right) - \sum_{fu\in FU_r}\left(active_{fu} \cdot p_{fu}\right)\leq bp_r$$
