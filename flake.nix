{
  description = "Development environment for AIC Planner";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        build-dir = "build";
        executable = "aic_planner";

        configure = pkgs.writeShellScriptBin "configure" ''
          set -euo pipefail
          cmake -B ${build-dir} -S . -DCMAKE_BUILD_TYPE=Release "$@"
        '';

        build = pkgs.writeShellScriptBin "build" ''
          set -euo pipefail
          if [ ! -f ${build-dir}/CMakeCache.txt ]; then
              echo "Configuration not found. Running configure first..."
              configure
          fi
          cmake --build ${build-dir} --parallel
        '';

        check = pkgs.writeShellScriptBin "check" ''
          set -euo pipefail
          configure -DBUILD_TESTING=ON
          build
          ctest --test-dir ${build-dir} --output-on-failure
        '';

        clean = pkgs.writeShellScriptBin "clean" ''
          rm -rf ${build-dir}
        '';

        run = pkgs.writeShellScriptBin "run" ''
          set -euo pipefail
          if [ ! -f ${build-dir}/bin/${executable} ]; then
              echo "Executable not found. Running build first..."
              build
          fi
          ./${build-dir}/bin/${executable} "$@"
        '';
      in
      {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            gnumake
            clang
            python3
          ];

          buildInputs = with pkgs; [
            # dependencies
            or-tools
            nlohmann_json
            protobuf
            re2
            zlib
            bzip2
            clp
            cbc
            glpk
            eigen

            # custom commands
            configure
            build
            check
            clean
            run
          ];

          shellHook = ''
            export ORTOOLS_DIR=${pkgs.or-tools}
          '';
        };
      }
    );
}
