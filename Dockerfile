# syntax=docker/dockerfile:1

FROM ubuntu:latest AS builder

WORKDIR /root/cups

RUN apt-get update -y && apt-get upgrade --fix-missing -y \
    && apt-get install -y --no-install-recommends \
        autoconf \
        build-essential \
        libavahi-client-dev \
        libkrb5-dev \
        libnss-mdns \
        libpam-dev \
        libssl-dev \
        libsystemd-dev \
        libusb-1.0-0-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . /root/cups

# Install into a build root directory for clean stage separation
RUN ./configure \
        --prefix=/usr \
	--libdir=/usr/lib \
        --sysconfdir=/etc \
        --localstatedir=/var \
    && make clean \
    && make \
    && make install DESTDIR=/buildroot

FROM ubuntu:latest AS runtime

RUN apt-get update -y \
    && apt-get install -y --no-install-recommends \
        avahi-daemon \
        libavahi-client3 \
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

# copy the build root into the runtime image
COPY --from=builder /buildroot/usr /usr
COPY --from=builder /buildroot/etc/cups /etc/cups

# Update the dynamic linker cache so libcups.so.2 is discoverable
RUN ldconfig

RUN useradd -m --create-home \
        --password "$(echo 'admin' | openssl passwd -1 -stdin)" \
        -f 0 admin \
    && groupadd -f lpadmin \
    && usermod -aG lpadmin admin \
    && echo 'admin ALL=(ALL:ALL) ALL' >> /etc/sudoers

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
