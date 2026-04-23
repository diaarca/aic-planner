# Contributing to AIC Planner

First off, thank you for considering contributing! It's people like you that make the open-source community such a great place to learn, inspire, and create.

## How Can I Contribute?

### Reporting Bugs
*   **Check the existing issues** to see if the bug has already been reported.
*   If you can't find an open issue addressing the problem, **open a new one**.
*   Include a **clear title and description**, as much relevant information as possible, and a **code sample** or an **executable test case** demonstrating the expected behavior that is not occurring.

### Suggesting Enhancements
*   **Check the [Roadmap](README.md#roadmap)** to see our current wishlist and planned features.
*   Open an issue with the tag `enhancement`.
*   Explain the use case and why this feature would be useful for the project.

### Pull Requests
1.  **Fork** the repository and create your branch from `main`.
2.  If you've added code that should be tested, **add tests**.
3.  Ensure the project **builds** and runs correctly.
4.  Ensure your code adheres to the **Coding Standards** below.
5.  Open a Pull Request with a clear description of your changes.

## Development Setup

This project uses **Nix** and **Direnv** for dependency management.
1.  Ensure you have Nix installed.
2.  Run `direnv allow` or `nix develop` to enter the development environment.
3.  Use the `Makefile` for building:
    ```bash
    make clean && make
    ```

## Coding Standards

To keep the codebase clean and consistent, please follow these rules:

### 1. Naming Conventions
*   **Functions and Methods**: Use `snake_case` (e.g., `solve_model()`).
*   **Variables and Members**: Use `snake_case`. Private members should end with an underscore (e.g., `products_`).
*   **Classes and Structs**: Use `PascalCase` (e.g., `Product`).
*   **Files**: Use `snake_case` or `PascalCase` consistently with existing files.

### 2. Namespacing
*   All code must reside within the `aic` namespace.

### 3. Domain Constants
*   Do not use hardcoded strings for minerals or facilities. Use the constants defined in `include/aic-planner/constants.hpp`.

### 4. Modern C++
*   The project targets **C++17**.
*   Prefer `std::unique_ptr` and `std::shared_ptr` over raw pointers for ownership.

## License
By contributing, you agree that your contributions are licensed under our [LICENSE](LICENSE) (MIT).
