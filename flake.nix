{
  description = "OpenPrinting CUPS";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in {
      packages = forAllSystems (pkgs: {
        default = pkgs.stdenv.mkDerivation (finalAttrs: {
          pname = "cups";
          version = "2.5b1";
          src = ./.;

          nativeBuildInputs = with pkgs; [
            pkg-config
          ];

          buildInputs = with pkgs; [
            zlib
            libjpeg
            libpng
            libtiff
            libusb1
            gnutls
            libpaper
          ]
          ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isLinux [
            avahi
            pam
            dbus
            acl
            systemd
          ];

          propagatedBuildInputs = [ pkgs.gmp ];

          configureFlags = [
            "--localstatedir=/var"
            "--sysconfdir=/etc"
            "--enable-raw-printing"
            "--enable-threads"
          ]
          ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isLinux [
            "--enable-dbus"
            "--enable-pam"
            "--with-dbusdir=${placeholder "out"}/share/dbus-1"
            "--with-systemd=${placeholder "out"}/lib/systemd/system"
          ]
          ++ [
            "--enable-libusb"
            "--enable-ssl"
            "--enable-avahi"
            "--enable-libpaper"
          ];

          preConfigure = ''
            export AR="${pkgs.lib.getBin pkgs.stdenv.cc.bintools.bintools}/bin/${pkgs.stdenv.cc.targetPrefix}ar"
          '';

          installFlags = [
            "CACHEDIR=$(TMPDIR)/dummy"
            "LAUNCHD_DIR=$(TMPDIR)/dummy"
            "LOGDIR=$(TMPDIR)/dummy"
            "REQUESTS=$(TMPDIR)/dummy"
            "STATEDIR=$(TMPDIR)/dummy"
            "PAMDIR=$(out)/etc/pam.d"
            "XINETD=$(out)/etc/xinetd.d"
            "SERVERROOT=$(out)/etc/cups"
            "MENUDIR=$(out)/share/applications"
            "ICONDIR=$(out)/share/icons"
            "CUPS_PRIMARY_SYSTEM_GROUP=root"
          ];

          enableParallelBuilding = true;
        });
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            autoconf
            gnumake
            ninja
            pkg-config
            zlib
            libjpeg
            libpng
            libtiff
            libusb1
            gnutls
            libpaper
            avahi
            pam
            dbus
            acl
            systemd
            gmp
          ];
        };
      });
    };
}
