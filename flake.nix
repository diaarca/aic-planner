{
  description = "Development environment for AIC Planner";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
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
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            or-tools
            gnumake
            clang
            bear
            fast-cpp-csv-parser
          ];

          shellHook = ''
            export ORTOOLS_DIR=${pkgs.or-tools}
            export CSV_PARSER_DIR=${pkgs.fast-cpp-csv-parser}
            echo "AIC Planner dev shell loaded with or-tools and fast-cpp-csv-parser"
          '';
        };
      }
    );
}
