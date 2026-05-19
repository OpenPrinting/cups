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

# Sweet's suggestion #1: No RPATH needed
# Sweet's suggestion #2: Use DESTDIR so all files go to /buildroot
RUN ./configure \
        --prefix=/usr \
        --sysconfdir=/etc \
        --localstatedir=/var \
    && make clean \
    && make \
    && make install DESTDIR=/buildroot

# ─────────────────────────────
# Stage 2 – runtime
# ─────────────────────────────
FROM ubuntu:latest AS runtime

# Runtime packages
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

# Sweet's suggestion #2: Copy build root subdirectories
COPY --from=builder /buildroot/usr /usr
COPY --from=builder /buildroot/etc /etc

# lib64 -> lib symlink banao taake ldconfig library dhund sake
RUN ln -sf /usr/lib64/libcups.so.2 /usr/lib/libcups.so.2 \
    && ln -sf /usr/lib64/libcupsimage.so.2 /usr/lib/libcupsimage.so.2 \
    && ldconfig

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
        /etc/cups/cupsd.conf \
    && echo "Browsing No" >> /etc/cups/cupsd.conf \
    && echo "BrowseLocalProtocols none" >> /etc/cups/cupsd.conf

RUN cp -rp /etc/cups /etc/cups-bak

VOLUME ["/etc/cups"]
VOLUME ["/var/log/cups"]

EXPOSE 631

CMD ["/usr/sbin/cupsd", "-f"]
