{
  stdenv,
  lib,
  cmake,
  pkg-config,
  gnumake,
  # Dependencies
  or-tools,
  fast-cpp-csv-parser,
  protobuf,
  re2,
  zlib,
  bzip2,
  clp,
  cbc,
  glpk,
  eigen,
}:

stdenv.mkDerivation {
  pname = "aic_planner";
  version = "0.0.1";

  # Source code directory, it clean the useless files for the package.
  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [
      # Keeps only these files
      ./CMakeLists.txt
      ./src
      ./include
    ];
  };

  nativeBuildInputs = [
    cmake
    pkg-config
    gnumake
  ];

  buildInputs = [
    # Dependencies for the build
    or-tools
    fast-cpp-csv-parser
    protobuf
    re2
    zlib
    bzip2
    clp
    cbc
    glpk
    eigen
  ];

  cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];

  # Metadata of the package
  meta = with lib; {
    description = "AIC Planner";
    longDescription = ''
      An optimization tool for the Automated Industry Complex (AIC) system in Arknights: Endfield.
    '';
    homepage = "https://github.com/diaarca/aic-planner";
    license = licenses.mit;
    maintainers = [ maintainers.diaarca ];
    platforms = platforms.unix;
    mainProgram = "aic_planner";
  };
}
