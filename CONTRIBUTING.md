# Contributing to AIC Planner

Issues and pull requests are welcome. For a bug, include the dataset, command, observed behavior, and expected result. For a new mechanic, explain its resource flow, geometry, and transport requirements.

## Development

```sh
nix develop
check
```

`check` configures and builds the project, then runs all CTest checks without network access. The terminal application targets macOS and Linux. CMake can also be used directly when OR-Tools and its dependencies are already installed.

Format C++ before submitting changes:

```sh
clang-format -i src/*.cpp include/aic-planner/*.hpp tests/planner_tests.cpp
```

Python importer and fixture tests can run independently:

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

The project targets C++17, CMake, and OR-Tools. Use `snake_case` for functions, variables, and files, `PascalCase` for types, trailing underscores for private members, and the `aic` namespace. Use enums for closed concepts such as directions and transport media; keep facility and item identities data driven.

## Architecture

- `cli` parses commands and validates option combinations; `main` runs the selected workflow.
- `Dataset` loads and validates offline JSON, including regional catalog selections. File parsing uses the shared `json_io` helper so diagnostics identify the source file.
- `RecipeGraph` solves steady-state material balances, rejects unproductive cycles, and constructs autonomous seed-loop plans. The production-network builder allocates exact per-machine flows to distinct ports.
- `BlueprintLayout` owns physical geometry, rotation, and coordinate normalization, independently of solver headers.
- `plant_module` validates the fixed plant geometry and preserves its internal belt paths.
- `BlueprintSolver` places machines; the conveyor router allocates physical routes and returns conflicts for placement retries.
- `Solver` chooses regional production and packs generated candidate blueprints. Production, area packing, and power constraints are separate operations; reports use the same facility power calculation as optimization.
- Terminal rendering is separate from optimization. Diagnostics use the logging interface and stderr.

Add behavior-focused regressions for new mechanics. Keep source conversion tests offline with small fixtures; do not make the test suite depend on live provider availability. For geometry, verify bounds, non-overlap, port directions, and conveyor clearance rather than an exact solver-generated arrangement. Fixed templates should match their specified geometry. CLI regressions cover invalid combinations and numeric options.

## Data changes

Follow [data maintenance](docs/DATA.md). Do not hand-edit generated `data/catalog.json`. Refresh it with the importer, review the version and change report, and validate affected regions. Keep local regression fixtures intentionally small and update them explicitly.

Catalog-backed markets discover their sale products automatically. Keep all imported offers and their eligibility metadata even when a manufacturing mechanic is not yet supported. Adding support requires appropriate dependency, placement, routing, and model documentation changes. An unsupported recipe must produce an actionable diagnostic.

## Pull requests

Describe the problem, resulting behavior, and checks performed. Keep mechanical renames separate from algorithm changes where practical. Contributions to source code are provided under the MIT License; game data retains its rights-holder attribution in NOTICE. Follow [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
