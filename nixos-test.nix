{ cups }:
{ pkgs, ... }:

let
  port = 8631;

  printer =
    {
      name,
      vlan,
      ipv4,
      ipv6,
      addresses,
    }:
    {
      virtualisation.interfaces.eth1.vlan = vlan;

      networking = {
        useDHCP = false;
        enableIPv6 = ipv6;
        interfaces.eth1 = addresses;
        firewall.allowedTCPPorts = [ port ];
      };

      services.avahi = {
        enable = true;
        inherit ipv4 ipv6;
        allowInterfaces = [ "eth1" ];
        publish = {
          enable = true;
          addresses = true;
          userServices = true;
        };
      };

      systemd.services.ippeveprinter = {
        wantedBy = [ "multi-user.target" ];
        after = [ "avahi-daemon.service" ];
        serviceConfig = {
          ExecStart = "${cups}/bin/ippeveprinter -c ${pkgs.coreutils}/bin/true -d /var/lib/ippeveprinter -f application/pdf -p ${toString port} ${name}";
          StateDirectory = "ippeveprinter";
        };
      };
    };
in
{
  name = "cups-address-scope";

  nodes = {
    client = {
      virtualisation.interfaces = {
        eth1.vlan = 1;
        eth2.vlan = 2;
        eth3.vlan = 3;
        eth4.vlan = 4;
      };

      networking = {
        useDHCP = false;
        interfaces = {
          eth1.ipv4.addresses = [
            {
              address = "192.0.2.1";
              prefixLength = 24;
            }
          ];
          eth2.ipv4.addresses = [
            {
              address = "169.254.2.1";
              prefixLength = 16;
            }
          ];
          eth3.ipv6.addresses = [
            {
              address = "2001:db8:3::1";
              prefixLength = 64;
            }
          ];
          eth4.useDHCP = false;
        };
      };

      services.avahi = {
        enable = true;
      };
      services.resolved = {
        enable = true;
        settings.Resolve.MulticastDNS = true;
      };
      services.printing = {
        enable = true;
        package = cups;
      };

      environment.systemPackages = [ cups ];
    };

    v4 = printer {
      name = "v4";
      vlan = 1;
      ipv4 = true;
      ipv6 = false;
      addresses.ipv4.addresses = [
        {
          address = "192.0.2.2";
          prefixLength = 24;
        }
      ];
    };

    v4ll = printer {
      name = "v4ll";
      vlan = 2;
      ipv4 = true;
      ipv6 = false;
      addresses.ipv4.addresses = [
        {
          address = "169.254.2.2";
          prefixLength = 16;
        }
      ];
    };

    v6 = printer {
      name = "v6";
      vlan = 3;
      ipv4 = false;
      ipv6 = true;
      addresses.ipv6.addresses = [
        {
          address = "2001:db8:3::2";
          prefixLength = 64;
        }
      ];
    };

    v6ll = printer {
      name = "v6ll";
      vlan = 4;
      ipv4 = false;
      ipv6 = true;
      addresses.useDHCP = false;
    };
  };

  testScript = ''
    from datetime import timedelta

    start_all()

    for machine in [v4, v4ll, v6, v6ll]:
        machine.wait_for_unit("ippeveprinter.service")

    client.wait_for_unit("avahi-daemon.service")
    client.wait_for_unit("cups.service")

    cases = [
        ("v4", "v4.local=192.0.2.2"),
        ("v4ll", "v4ll.local=169.254.2.2"),
        ("v6", "v6.local=[v1.2001:db8:3::2]"),
        ("v6ll", "v6ll.local=[v1.fe80::"),
    ]

    for name, address in cases:
        with subtest(name):
            client.wait_until_succeeds(
                f"avahi-browse --parsable --resolve --terminate _ipp._tcp | grep -q ';{name};'",
                timeout=timedelta(seconds=30),
            )
            client.wait_until_succeeds(
                f"getent ahosts {name}.local", timeout=timedelta(seconds=30)
            )
            client.succeed(
                "timeout --kill-after=2s 30s env DEVICE_URI=ipp://"
                + name
                + "._ipp._tcp.local/ "
                + "CONTENT_TYPE=application/pdf FINAL_CONTENT_TYPE=application/pdf "
                + "${cups}/lib/cups/backend/ipp 1 root test 1 document-format=application/pdf ${cups}/share/cups/ipptool/testfile.pdf"
                + f" 2>/tmp/{name}.log"
            )
            client.succeed(f"grep -F '{address}' /tmp/{name}.log")
            if name == "v6ll":
                client.succeed("grep -F '+eth4]' /tmp/v6ll.log")
            queue = f"{name}-ppd"
            client.succeed(
                f"lpadmin -p {queue} -E -v ipp://{name}._ipp._tcp.local/ -m everywhere"
            )
            client.succeed(f"test -s /var/lib/cups/ppd/{queue}.ppd")
            client.succeed(f"lp -d {queue} ${cups}/share/cups/ipptool/testfile.pdf")
            client.wait_until_succeeds(
                f"lpstat -W completed -o {queue} | grep -q '^{queue}-'",
                timeout=timedelta(seconds=30),
            )
  '';
}
