{
  description = "TapUI: device-defined UI over Bluetooth, launched from an NFC tag or QR code";

  inputs = {
    # Same revision as 3d-gallery: its playwright-driver matches the
    # @playwright/test pinned in package.json.
    nixpkgs.url = "github:NixOS/nixpkgs/64c08a7ca051951c8eae34e3e3cb1e202fe36786";
    # Same revision as fieldpad/fw, whose Zephyr toolchain this copies.
    nixpkgs-fw.url = "github:NixOS/nixpkgs/fe13242281a67641c0102cdb1087f0ebe586c7df";
  };

  outputs = { self, nixpkgs, nixpkgs-fw }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      fwpkgs = nixpkgs-fw.legacyPackages.${system};
      fwpython = fwpkgs.python3.withPackages (ps: with ps; [
        west pyelftools pyyaml packaging pykwalify jsonschema anytree intelhex
        psutil pyserial requests semver tqdm patool pytest
      ]);
    in {
      devShells.${system} = {
      # Web page, its tests, and the QR tool.
      default = pkgs.mkShell {
        packages = [
          pkgs.nodejs_22
          pkgs.playwright-driver.browsers
          (pkgs.python3.withPackages (ps: [ ps.qrcode ]))
        ];
        # Playwright's own chromium can't resolve its libraries on NixOS.
        PLAYWRIGHT_BROWSERS_PATH = "${pkgs.playwright-driver.browsers}";
        PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS = "true";
      };

      # Firmware: Zephyr with nixpkgs' gcc-arm-embedded, no Zephyr SDK.
      fw = fwpkgs.mkShell {
        packages = [
          fwpython
          fwpkgs.cmake
          fwpkgs.ninja
          fwpkgs.dtc
          fwpkgs.gperf
          fwpkgs.gcc-arm-embedded
          fwpkgs.git
          fwpkgs.file
          fwpkgs.which
        ];
        # native_sim links its runner with the host gcc; nixpkgs' hardening
        # flags break that link.
        hardeningDisable = [ "all" ];
        ZEPHYR_TOOLCHAIN_VARIANT = "gnuarmemb";
        GNUARMEMB_TOOLCHAIN_PATH = "${fwpkgs.gcc-arm-embedded}";
      };
      };
    };
}
