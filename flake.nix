{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    devkitNix.url = "github:bandithedoge/devkitNix";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      devkitNix,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ devkitNix.overlays.default ];
        };

        # the actual compilation derivation
        wut-example = pkgs.devkitNix.stdenvPPC.mkDerivation {
          pname = "wut-example";
          version = "0.1.0";
          src = ./.;
          makeFlags = [ "TARGET=example" ];
          installPhase = ''
            mkdir -p $out
            cp example.wuhb $out/
          '';
        };
      in
      {
        packages = {
          # 1. .#dist target
          dist = wut-example;

          # 2. default target for `nix run`
          default = pkgs.writeShellScriptBin "wiiload-script" ''
            # build the dist package and get the out path
            # we use --no-link to avoid cluttering the local dir with result symlinks
            RESULT_PATH=$(${pkgs.nix}/bin/nix build .#dist --print-out-paths --no-link)

            echo "sending $RESULT_PATH/example.wuhb to 192.168.1.18..."

            export WIILOAD=tcp:192.168.1.18
            ${pkgs.devkitNix.devkitPPC}/bin/wiiload "$RESULT_PATH/example.wuhb"
          '';
        };
      }
    );
}
