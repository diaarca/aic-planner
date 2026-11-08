{ pkgs ? import <nixpkgs> { } }:

let
  aic_planner = pkgs.callPackage ./package.nix { };
in
{
  inherit aic_planner;
  default = aic_planner;
  overlay = final: prev: {
    aic_planner = final.callPackage ./package.nix { };
  };
}
