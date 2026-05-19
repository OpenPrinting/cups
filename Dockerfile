# syntax=docker/dockerfile:1

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

# DESTDIR=/buildroot — sab kuch /buildroot mein install hoga
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

# Poora buildroot copy — ubuntu files overwrite se bachne ke liye
# sirf CUPS ki apni files copy ho rahi hain /buildroot se
COPY --from=builder /buildroot/usr /usr
COPY --from=builder /buildroot/etc/cups /etc/cups

# ldconfig — koi hardcoded path nahi, system khud library dhundega
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
