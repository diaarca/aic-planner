{
  pkgs ? import <nixpkgs> { },
}:

let
  # Package
  aic_planner = pkgs.callPackage ./package.nix { };
in
{
  # Direct function call method
  inherit aic_planner;

  # Default alias
  default = aic_planner;

  # Overlays method (for nixpkgs.overlays = [ (import ./default.nix {}).overlay ])
  overlay = final: prev: {
    inherit aic_planner;
  };
}
