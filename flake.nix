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
    let
      # Package definition
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
          cmake -B ${build-dir} -S . -DCMAKE_BUILD_TYPE=Release
        '';

        build = pkgs.writeShellScriptBin "build" ''
          if [ ! -f ${build-dir}/CMakeCache.txt ]; then
              echo "Configuration not found. Running configure first..."
              configure
          fi
          cmake --build ${build-dir} --parallel
        '';

        clean = pkgs.writeShellScriptBin "clean" ''
          rm -rf ${build-dir}
        '';

        run = pkgs.writeShellScriptBin "run" ''
          if [ ! -f ${build-dir}/bin/${executable} ]; then
              echo "Executable not found. Running build first..."
              build
          fi
          ./${build-dir}/bin/${executable} "$@"
        '';
      in
      {
        # Get from package.nix values
        packages.default = pkgs.aic_planner;

        # Check command for the build
        checks.default = self.packages.${system}.default;

        devShells.default = pkgs.mkShell {
          # Reuses the inputs from package.nix
          inputsFrom = [ self.packages.${system}.default ];

          buildInputs = with pkgs; [
            # custom commands
            configure
            build
            clean
            run
          ];

          shellHook = ''
            export ORTOOLS_DIR=${pkgs.or-tools}
            export CSV_PARSER_DIR=${pkgs.fast-cpp-csv-parser}
          '';
        };
      }
    )
    // {
      # Overlay of the aic_planner package
      overlays.default = overlay;
    };
}
