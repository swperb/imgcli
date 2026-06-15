{
  description = "imgcli - ffmpeg-style image conversion & processing in C (dependency-free)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "imgcli";
          version = "0.3.0";
          src = ./.;

          # Zero third-party deps: links only the system C library + libm, both
          # provided by the stdenv. The default `make` target builds ./imgcli.
          enableParallelBuilding = true;
          dontConfigure = true;

          installPhase = ''
            runHook preInstall
            install -Dm755 imgcli "$out/bin/imgcli"
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "ffmpeg-style image conversion & processing in C, with no system dependencies";
            homepage = "https://github.com/swperb/imgcli";
            license = licenses.mit;
            mainProgram = "imgcli";
            platforms = systems;
          };
        };
      });

      apps = forAllSystems (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.system}.default}/bin/imgcli";
        };
      });
    };
}
