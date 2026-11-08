{
  stdenv,
  lib,
  cmake,
  pkg-config,
  gnumake,
  python3,
  or-tools,
  nlohmann_json,
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

  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [
      ./CMakeLists.txt
      ./src
      ./include
      ./data
      ./LICENSE
      ./NOTICE
      (lib.fileset.fileFilter (
        file: file.hasExt "cpp" || file.hasExt "py" || file.hasExt "json"
      ) ./tests)
      (lib.fileset.fileFilter (file: file.hasExt "py" || file.hasExt "json") ./tools)
    ];
  };

  nativeBuildInputs = [
    cmake
    pkg-config
    gnumake
    python3
  ];

  # OR-Tools' exported CMake configuration also resolves its solver dependencies.
  buildInputs = [
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
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DBUILD_TESTING=ON"
  ];

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    ctest --output-on-failure
    runHook postCheck
  '';

  meta = {
    description = "Factory layout and regional production planner for Arknights: Endfield";
    homepage = "https://github.com/diaarca/aic-planner";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux ++ lib.platforms.darwin;
    mainProgram = "aic_planner";
  };
}
