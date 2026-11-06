#!/bin/sh
#
# Script to initialize system services (users, D-Bus, Avahi) for the CUPS OCI 
# container environment.
#
# Copyright © 2024-2025 by OpenPrinting.
# Copyright © 2024-2025 by Till Kamppeter.
# Copyright © 2024-2025 by Rudra Pratap Singh.
#
# Licensed under Apache License v2.0. See the file "LICENSE" for more
# information.
#
set -eux

echo "Creating system users"

# Create system users with system accounts and no home directories, using nologin shell
useradd --system --no-create-home --shell /usr/sbin/nologin systemd-resolve || true
useradd --system --no-create-home --shell /usr/sbin/nologin systemd-network || true

echo "Creating directories"

# Create the /run/dbus directory if it doesn't exist, set permissions, and ownership
mkdir -p /run/dbus
chmod 755 /run/dbus
chown root:root /run/dbus

echo "Starting dbus"

# Start the dbus daemon in the foreground
service dbus start
# Check the status of the dbus service
service dbus status || true

echo "Starting avahi-daemon"

# Start the avahi-daemon in the background without dropping root privileges
avahi-daemon --daemonize --no-drop-root

# Keep the script running to avoid container exit
tail -f /dev/null