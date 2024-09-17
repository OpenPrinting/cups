# syntax=docker/dockerfile:1

# Use the latest Ubuntu base image
FROM ubuntu:latest

# Set the working directory inside the container
WORKDIR /workspaces/cups

# Update package list and upgrade existing packages
RUN apt-get update -y && apt-get upgrade --fix-missing -y

# Install required dependencies for CUPS
RUN apt-get install -y autoconf build-essential \
    avahi-daemon  libavahi-client-dev \
    libssl-dev libkrb5-dev libnss-mdns libpam-dev \
    libsystemd-dev libusb-1.0-0-dev zlib1g-dev \
    openssl sudo

# Copy the current directory contents into the container's working directory
COPY . /root/cups
WORKDIR /root/cups
RUN ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var && make clean && make && make install

# Expose port 631 for CUPS web interface
EXPOSE 631
