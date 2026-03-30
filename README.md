# Endfield Factory

This project aims to optimize the Automated Industry Complex (AIC) system of the
game Arknights:Endfield. To this goal, we model the system through a
mathematical representation known as Mixted Integer Linear Programming (MILP)
that is then optimized using a solver, here
[OR-Tools](https://developers.google.com/optimization).

The mathematical model aim to maximize the quantity of regional stock bills by
producing the best AIC products according on the mineral limitations (only
Valley IV for now).

## Mathematical Model

### Data

#### The Region Related Data

- $r$ $\in$ $R$: the region
- $bp_r$: the base power provided in the region $r$
- $s_r$: the per element storage in the region $r$

#### The Mineral Related Data

- $m$ $\in$ $M_r$: the minerals of the region $r$
- $l_m$: the limit of the mineral $m$ in the region

#### The Product Related Data

- $p$ $\in$ $P_r$: the products of the region $r$
- $v_p$: the value of the product $p$ (in stock bills)
- $m_p^m$: the quantity of mineral $m$ needed for the product $p$
- $t_p$: the production time needed for the product $p$
- $d_p$: the depot length needed for the product $p$'s factory
- $w_p$: the width length needed for the product $p$'s factory
- $h_p$: the height length needed for the product $p$'s factory
- $fa_p^{fa}$: the number of facility $fa$ needed for the product $p$ factory

#### The Fuel Related Data

- $fu$ $\in$ $FU_r$ $\subseteq$ $P_r$: the fuel of the region $r$
- $p_{fu}$: the power that the fuel $fu$ provides
- $d_{fu}$: the duration during which the fuel $fu$ stands before being consumed

#### The Area Related Data

- $a$ $\in$ $A_r$: the areas of the region $r$
- $fa_a^fa$: the number of facility $fa$ needed in area $a$
- $w_a$: the PAC width in the area $a$
- $h_a$: the PAC height in the area $a$
- $wd_a$: the PAC depot width in the area $a$
- $hd_a$: the PAC depot height in the area $a$

#### The Facility Related Data

- $fa$ $\in$ $FA_r$: the facilities of the region $r$
- $p_{fa}$: the power usage of the facility $fa$

### Variables

- $\text{produce}_p$: the quantity of product $p$ to be produced (per minute)
- $\text{factory}_p^a$: the number of factory for product $p$ to be place in
  area $a$
- $\text{active}_{fu}$: the quantity of fuel $fu$ that must be active for
  providing power

### Objective

$$\max{\left[\sum_{p\in P_r} \left(produce_p . v_p\right) - \sum_{fu\in FU_r} \left(active_{fu} . \dfrac{60}{d_{fu}} . v_{fu}\right) \right]}$$

### Constraints

- $$produce_{fu} - \dfrac{60}{d_{fu}} . active_{fu} \geq 0 \quad \forall fu\in FU_r$$
  (cannot consume more fuel than produced)
- $$produce_{p} \leq \dfrac{s_r}{48 * 60} \quad \forall p\in P_r\setminus FU_r$$
  (we don't want to fill the region storage within 48 hours)
- $$\sum_{p\in P_r} \left(produce_p . m_p^m \right) \leq l_m\quad \forall m\in M_r$$
  (mineral limits)
- $$produce_p - \sum_{a\in A_r}\left(factory_p^a . \dfrac{60}{t_p}\right)\leq 0\quad\forall p\in P_r$$
  (fix the number of product factory based on its production)
- $$\sum_{p\in P_r}\left(factory_p^a . w_p . h_p\right)\leq w_a . h_a\quad\forall a\in A_r$$
  (product factory space constraint to stand in the area)
- $$\sum_{p\in P_r}\left(factory_p^a . d_p\right)\leq wd_a + hd_a\quad\forall a\in A_r$$
  (product factory depot lenght constraint to stand in the area)
- $$\sum_{a\in A_r}\left(\sum_{fa\in FA_r}\left(fa_a^{fa} . p_{fa}\right) + \sum_{p\in P_r}\left(factory_p^a . \sum_{fa\in FA_r}\left(fa_p^{fa} . p_{fa}\right)\right)\right) - \sum{fu\in FU_r}\left(active_{fu} . p_{fu}\right)\leq bp_r$$
  (power consumption constraint)
