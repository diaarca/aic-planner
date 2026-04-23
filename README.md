# EndField Factory: AIC Planner

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

### 2. Compile
Once inside the Nix shell, run:
```bash
make
```

### 3. Usage
The executable is located in `build/bin/`. Run it by providing a path to a region's data folder:
```bash
./build/bin/aic_planner data/valley_iv
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

## Contributing
Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for setup instructions and [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for community guidelines.

## License
This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.
