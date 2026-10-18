# AIC Planner

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

An optimization tool for the Automated Industry Complex (AIC) system in **Arknights: Endfield**. 

This project uses Mixed-Integer Linear Programming (MILP) and Google's [OR-Tools](https://developers.google.com/optimization) to maximize regional stock bill production by finding the optimal balance of products, facilities, and resource consumption.

For a detailed breakdown of the math, see [MODEL.md](MODEL.md).

---

## Installation and Build

This project uses [Nix](https://nixos.org/) to manage its development environment, ensuring all dependencies (OR-Tools, Clang, CSV Parser) are consistent across different systems.

### 1. Enter the Development Shell
From the project root, run:
```bash
nix develop
```

### 2. Configurate the project
```bash
configure
```

### 3. Build the project
```
build
```

### 4. Usage
The executable is located in `build/bin/`. Run it by providing a path to a region's data folder:
```bash
./build/bin/aic_planner data/valley_iv
```
or by running the `run` command:
```bash
run data/valley_iv
```

---

## Nix Package

This project provides a Nix Package. It can be installed with or without flakes.

### 1. Flakes - Install / Upgrade

#### A. Local profile (clone repo) - Install
```bash
nix build
nix profile install .
```
Binary is build in: _./result/bin/aic_planner_

#### B. Distant profile (From GitHub) - Install
```bash
nix run github:diaarca/aic-planner <input_file> # Run the application without installation
nix profile install github:diaarca/aic-planner  # Install the application on nix profile
```

#### C. Profile (local / distant) - Upgrade
```bash
nix profile list    # Get the name of the profile to upgrade
nix profile upgrade <name>
```

#### D. Home Manager - Install
Add the input in your `flake.nix`:
```nix
inputs.aic-planner.url = "github:diaarca/aic-planner";
```

Then add the package to your Home Manager modules:
```nix
modules = [
  ./home.nix
  { home.packages = [ aic-planner.packages.x86_64-linux.default ]; }
];
```

_*OR*_ use the overlay, to get `pkgs.aic_planner` in all your modules:
```nix
pkgs = import nixpkgs {
  system = "x86_64-linux";
  overlays = [ aic-planner.overlays.default ];
};
```

#### E. Home Manager - Upgrade
```bash
nix flake update aic-planner
```

### 2. Non-Flakes - Install / Upgrade

#### A. Profile nix - Install
```bash
nix-env -if https://github.com/diaarca/aic-planner/archive/main.tar.gz -A aic_planner
```

#### B. Home Manager - Install
In `home.nix`:
```nix
home.packages = [
  (import (fetchTarball "https://github.com/diaarca/aic-planner/archive/main.tar.gz") { }).aic_planner
];
```

_*OR*_ use the overlay, to get `pkgs.aic_planner` in all your modules:
```nix
nixpkgs.overlays = [
  (import (fetchTarball "https://github.com/diaarca/aic-planner/archive/main.tar.gz") { }).overlay
];
```

#### C. Upgrade
To upgrade, run the commands again.

### 3. Usage
```bash
aic_planner <data_folder_path>
```

---

## Data Structure
The solver expects a directory containing the following CSV files:
- `minerals.csv`: Resource limits.
- `products.csv`: Manufacturing recipes and values.
- `areas.csv`: Available space and static facilities.
- `fuels.csv`: Power generation data.
- `facilities.csv`: Power consumption per facility type.
- `region.csv`: Global base power and storage capacity.

---

## Roadmap

We are actively working to improve the AIC Planner. Here is our wishlist for future features:

- **Wuling Region Support** ([#7](https://github.com/diaarca/aic-planner/issues/7)): Add full support for the `Wulling` region, including data files and specific in-game logic.
- **Dynamic Facility Logic** ([#8](https://github.com/diaarca/aic-planner/issues/8)): Shift from blueprint-based data to a facility-logic-based engine. This allows the solver to analyze individual facility attributes (size, recipes, inputs/outputs) to autonomously decide on the most profitable products and optimal placements.
- **Solution Visualizer** ([#9](https://github.com/diaarca/aic-planner/issues/9)): Create a visual representation of the solver's output to help players build the optimized factory layout in-game.

## Contributing
Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for setup instructions and [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for community guidelines.

## License
This project is licensed under the **MIT License** - see our [LICENSE](LICENSE) file for details.

**Arknights: Endfield** and its assests are the property of **GRYPHLINE. GRYPH FRONTIER PTELTD**, and this project claims no rights over the original game.
