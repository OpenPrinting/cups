# REPORTING ISSUES FOR CUPS PROJECT

> Disclaimer: We provide only best effort support for CUPS releases older than the latest release. This means we try to reproduce the issue on the latest release if it is possible, and if we can't reproduce, we can give a hint what can be the problem and ask you to contact your distribution support.

> All commands are written from point of view of user which has superuser rights (f.e. he is in `wheel` on Linux system) or he is in a group defined in SystemGroup directive in `/etc/cups/cups-files.conf`. Other users will have to use `sudo` with certain commands.

The steps you are asked to do differ based on how your printer is connected (USB/network) and whether you use driverless printing or not. Since the connection is obvious for the user, the paragraph below will tell you how to find out whether driverless printing is used.


## HOW TO FIND OUT THAT I USE DRIVERLESS PRINTING

For network printers:
* your printer is seen by `lpstat -l -e` and it is marked as `network` in the output - you print via temporary queue, which works only via driverless means,

or

* your printer is seen by `lpstat -l -e`, marked as `permanent` in the output, its PPD file at `/etc/cups/ppd` has `IPP Everywhere` or `driverless` in its `Nickname` entry and its connection is `ipp` or `ipps` when you check the connection uri (f.e. by `lpstat -v <printer_name>`)

If any of them apply, your device works in driverless mode.

For USB printers:
* your printer is listed if you enter:

```
    $ sudo ipp-usb check
```

in case the printer is not listed or the command is not found, your device doesn't work in driverless mode.


## HOW TO REPORT THE ISSUE TO CUPS PROJECT

Please do the steps below and provide the mentioned information (**use attachments for blocks of text longer than 10 lines** - put the text into a file, rename it to have .txt suffix and click on `pasting them` string under comment box in the issue) in your initial comment if you file an issue:

1. tell us what is **your OS**
2. tell us your **CUPS version**
3. in case you compile CUPS by yourself, tell us **all configuration options** you pass into `./configure`
4. **describe** the problem
5. mention your **printer model**
6. try to **narrow the issue if possible**:

    1. check how the printing works via CUPS CLI tools, f.e.:

    See whether the printer is available (f.e. in case the printer is not shown in application) - shows both temporary and permanent queues:

    ```
        $ lpstat -l -e
        HP_LaserJet_M1536dnf_MFP_42307C network none ipp://HP%20LaserJet%20M1536dnf%20MFP%2042307C._ipp._tcp.local/
        hp-laserjet permanent ipp://localhost/printers/hp-laserjet ipp://192.168.5.5/ipp/print
    ```

    See its available options (in case an application doesn't show some options):

    ```
        $ lpoptions -p HP_LaserJet_M1536dnf_MFP_42307C -l
        PageSize/Media Size: 184x260mm 195x270mm *A4 A5 B5 DoublePostcardRotated Env10 EnvC5 EnvDL EnvMonarch Executive FanFoldGermanLegal ISOB5 Legal Letter Postcard roc16k
        MediaType/Media Type: *Stationery StationeryLightweight Midweight StationeryHeavyweight ExtraHeavy ColorTransparency Labels StationeryLetterhead Envelope StationeryPreprinted StationeryPrepunched Color Bond Recycled Rough Vellum
        cupsPrintQuality/cupsPrintQuality: Draft *Normal
        ColorModel/Output Mode: *Gray
        Duplex/Duplex: *None DuplexNoTumble DuplexTumble
        OutputBin/OutputBin: *FaceDown
    ```

    Print a file to a printer with specific options (in case the printout from application is incorrect - print the same file and set the same options as you did in application, in case they are seen in `lpoptions`), f.e. to check duplex printing on a document you want to print (put the path to the document instead of <document>):

    ```
        $ lp -d HP_LaserJet_M1536dnf_MFP_42307C -o Duplex=DuplexNoTumble <document>
    ```
    
    For more info check `man lp`, `man lpstat`, `man lpoptions`.
    
    2. check if the issue happens with different documents
    3. check if the issue happens when using different applications
    4. in case you use mDNS hostnames (hostnames with `.local`), check mDNS resolution by pinging such hostname - mDNS is used for temporary queues (printers which are seen by `lpstat -e`, but not by `lpstat -a`) or for permanent queues which have `.local` in its URI (check `lpstat -a`). However the hostname from URI is not resolvable as it is, you need to get usual printer's mDNS hostname from `hostname` entry in `avahi-browse -avrt` output.
    
    Rule of thumb:
    1. in case printing works from CUPS CLI tools or CUPS Web UI, but not via an application, file the issue to the application for the initial investigation.
    2. in case mDNS resolution doesn't work and you use driverless printing or mDNS hostnames in CUPS, file the issue to your mDNS resolution provider - it can be `nss-mdns` or `systemd-resolved` depending on your configuration.

7. turn on **CUPS debug logging**:

```
    $ cupsctl LogLevel=debug2
```

8. **reproduce the issue**
9. **collect the logs** - the logs can be in `/var/log/cups/error_log` or in `journalctl`:

    For `error_log`:
    ```
        $ sudo cp /var/log/cups/error_log ~/error_log.txt
        $ sudo chmod 666 ~/error_log.txt
    ```
    
    For `journalctl`:
    ```
        $ journalctl -u cups --since=today > log.txt
    ```
    
    and attach the file to the GitHub issue.

10. provide output of **lpstat -e**, **lpstat -t** and **lpinfo -v**
11. provide **PPD file** from `/etc/cups/ppd` if exists for the printer
12. provide **the file you are trying to print**, if the issue happens with a specific file
13. provide **the d file from /var/spool/cups** - this is the file CUPS actually gets from the application
14. tell us the name of application where you experience the problem
15. if needed, turn off the debug logging by:

    ```
        $ cupsctl LogLevel=warn
    ```

### INFORMATION REQUESTED FOR USB DEVICES

* attach **output of `lsusb -v`** in a file as attachment
* in case of **communication issues with USB device**, it is helpful to **capture USB communication** - you have to know bus number where your device is connected to with `tcpdump`:

    ```
        $ lsusb
        Bus 002 Device 010: ID 03f0:012a HP, Inc HP LaserJet M1536dnf MFP
              =
        $ sudo tcpdump -s0 -w usb.pcap -i usbmon2
    ```
    compress the `usb.pcap` with `zip` and attach it to the issue if the problem is with USB printer.

#### INFORMATION REQUESTED FOR DRIVERLESS USB PRINTERS

* **provide the file attr.log** from `ipptool` command if the command passes:

    ```
        $ ipptool --ippserver attr.log -v ipp://localhost:60000/ipp/print get-printer-attributes.test
    ```

* compress **/var/log/ipp-usb** directory into `.zip` file and attach it to the issue


### INFORMATION REQUESTED FOR NETWORK DEVICES

* **provide network.pcap.zip** which is network packet capture - **catch the network traffic** f.e. with tcpdump:

    ```
        $ sudo tcpdump -s0 -w network.pcap -i any host <server_or_printer_IP>
    ```

compress the `network.pcap` with `zip` and attach it to the issue.

The communication can be encrypted - the person who investigates the issue can contact you off-issue for further data, because session keys will be needed as it is explained at [Wireshark wiki](https://wiki.wireshark.org/TLS) and a change in CUPS configuration (Wireshark is not able to decode IPPS directly, so the printer's connection has to be via HTTPS, which Wireshark can decrypt correctly).

#### INFORMATION REQUESTED FOR DRIVERLESS NETWORK PRINTERS

* **provide the file attr.log** from `ipptool` command, if the command passes:

For network driverless printer:
```
    $ ipptool --ippserver attr.log -v ipp://<printer_IP>/ipp/print get-printer-attributes.test
```

For driverless printer pointing to a printer at CUPS server:
```
    $ ipptool --ippserver attr.log -v ipp://<server_IP>/printers/<remote_printer_name> get-printer-attributes.test
```

For driverless printer installed in a printer application based on PAPPL:
```
    $ ipptool --ippserver attr.log -v ipp://localhost:8000/ipp/print/<printer_name> get-printer-attributes.test
```


## THANKS

This document uses knowledge from documentation and tips written by Till Kamppeter, Brian Potkin, Mike R. Sweet, Johannes Meixner, Tim Waugh, Jiri Popelka and Zdenek Dohnal. Thank you for all your work!
