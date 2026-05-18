# syntax=docker/dockerfile:1

# ─────────────────────────────
# Stage 1 – builder
# ─────────────────────────────
FROM ubuntu:latest AS builder

WORKDIR /root/cups

RUN apt-get update -y && apt-get upgrade --fix-missing -y \
    && apt-get install -y --no-install-recommends \
        autoconf \
        build-essential \
        libavahi-client-dev \
        libgnutls28-dev \
        libkrb5-dev \
        libnss-mdns \
        libpam-dev \
        libssl-dev \
        libsystemd-dev \
        libusb-1.0-0-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . /root/cups

# KEY FIX 1: rpath embed karo — /usr/lib64 kyunki Ubuntu pe yahi install hota hai
RUN LDFLAGS='-Wl,-rpath,/usr/lib64' \
    ./configure \
        --prefix=/usr \
        --sysconfdir=/etc \
        --localstatedir=/var \
    && make clean \
    && make \
    && make install

# ─────────────────────────────
# Stage 2 – runtime
# ─────────────────────────────
FROM ubuntu:latest AS runtime

# KEY FIX 2: runtime packages
RUN apt-get update -y \
    && apt-get install -y --no-install-recommends \
        avahi-daemon \
        libavahi-client3 \
        libgnutls30t64 \
        libkrb5-3 \
        libnss-mdns \
        libpam0g \
        libssl3t64 \
        libsystemd0 \
        libusb-1.0-0 \
        openssl \
        sudo \
        zlib1g \
    && rm -rf /var/lib/apt/lists/*

# KEY FIX 3: binaries — /usr/bin
COPY --from=builder /usr/bin/cancel          /usr/bin/cancel
COPY --from=builder /usr/bin/ippeveprinter   /usr/bin/ippeveprinter
COPY --from=builder /usr/bin/ippfind         /usr/bin/ippfind
COPY --from=builder /usr/bin/ipptool         /usr/bin/ipptool
COPY --from=builder /usr/bin/lp              /usr/bin/lp
COPY --from=builder /usr/bin/lpoptions       /usr/bin/lpoptions
COPY --from=builder /usr/bin/lpq             /usr/bin/lpq
COPY --from=builder /usr/bin/lpr             /usr/bin/lpr
COPY --from=builder /usr/bin/lprm            /usr/bin/lprm
COPY --from=builder /usr/bin/lpstat          /usr/bin/lpstat
COPY --from=builder /usr/bin/cupstestppd     /usr/bin/cupstestppd

# KEY FIX 4: binaries — /usr/sbin
COPY --from=builder /usr/sbin/cupsd          /usr/sbin/cupsd
COPY --from=builder /usr/sbin/cupsfilter     /usr/sbin/cupsfilter
COPY --from=builder /usr/sbin/cupsaccept     /usr/sbin/cupsaccept
COPY --from=builder /usr/sbin/cupsctl        /usr/sbin/cupsctl
COPY --from=builder /usr/sbin/cupsdisable    /usr/sbin/cupsdisable
COPY --from=builder /usr/sbin/cupsenable     /usr/sbin/cupsenable
COPY --from=builder /usr/sbin/cupsreject     /usr/sbin/cupsreject

# KEY FIX 5: libcups.so.2 — /usr/lib64 pe hai!
COPY --from=builder /usr/lib64/libcups.so.2      /usr/lib64/libcups.so.2
COPY --from=builder /usr/lib64/libcupsimage.so.2 /usr/lib64/libcupsimage.so.2

# CUPS support directories
COPY --from=builder /usr/lib/cups/           /usr/lib/cups/
COPY --from=builder /usr/share/cups/         /usr/share/cups/
COPY --from=builder /etc/cups/               /etc/cups/

# KEY FIX 6: linker cache update
RUN ldconfig

# Admin user setup
RUN useradd -m --create-home \
        --password "$(echo 'admin' | openssl passwd -1 -stdin)" \
        -f 0 admin \
    && groupadd -f lpadmin \
    && usermod -aG lpadmin admin \
    && echo 'admin ALL=(ALL:ALL) ALL' >> /etc/sudoers

# CUPS config
RUN /usr/sbin/cupsd \
    && sleep 3 \
    && cupsctl --remote-admin --remote-any --share-printers \
    && killall cupsd || true

RUN sed -i \
        -e 's/SystemGroup sys root/SystemGroup lpadmin/' \
        /etc/cups/cups-files.conf \
    && sed -i \
        -e 's/Port 631/Port 631\nServerAlias */' \
        -e 's/DefaultAuthType Basic/DefaultAuthType Basic\nDefaultEncryption IfRequested/' \
        -e 's/Browsing yes/Browsing no/I' \
        /etc/cups/cupsd.conf \
    && echo "Browsing No" >> /etc/cups/cupsd.conf \
    && echo "BrowseLocalProtocols none" >> /etc/cups/cupsd.conf

RUN cp -rp /etc/cups /etc/cups-bak

VOLUME ["/etc/cups"]
VOLUME ["/var/log/cups"]

EXPOSE 631

CMD ["/usr/sbin/cupsd", "-f"]
