{
  description = "AIC Planner package and development environment";

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
    let
      overlay = final: prev: {
        aic_planner = final.callPackage ./package.nix { };
      };
    in
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ overlay ];
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
        packages.default = pkgs.aic_planner;
        checks.default = pkgs.aic_planner;

        devShells.default = pkgs.mkShell {
          inputsFrom = [ pkgs.aic_planner ];
          nativeBuildInputs = [ pkgs.clang ];
          buildInputs = with pkgs; [
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
    )
    // {
      overlays.default = overlay;
    };
}
