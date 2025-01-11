#!/bin/sh
set -eux

# Ensure the lpadmin group exists
if ! getent group lpadmin > /dev/null; then
    groupadd -r lpadmin
fi

# Set default value for CUPS_ADMIN if not provided
CUPS_ADMIN="${CUPS_ADMIN:-admin}"

# Set default value for CUPS_PASSWORD if not provided
if [ -z "${CUPS_PASSWORD:-}" ]; then
    # Generate a random password (or provide error message if it fails)
    CUPS_PASSWORD=$(openssl rand -base64 12 || echo "Failed to generate random password")

    # Output default administrative credentials
    echo "Default administrative CUPS credentials are username: $CUPS_ADMIN, password: $CUPS_PASSWORD"

    # Output the default credentials to a file
    credentials_file="/etc/cups/cups-credentials"
    echo "Username: $CUPS_ADMIN" > "$credentials_file"
    echo "Password: $CUPS_PASSWORD" >> "$credentials_file"
    chmod 400 "$credentials_file"
fi

# Check if the user already exists
if ! id -u "$CUPS_ADMIN" > /dev/null 2>&1; then
    # Create the user and add to lpadmin group
    useradd -r -G lpadmin -M "$CUPS_ADMIN"
else
    # Ensure the user is a member of the lpadmin group
    usermod -aG lpadmin "$CUPS_ADMIN"
fi

# Set the user's password
echo "$CUPS_ADMIN:$CUPS_PASSWORD" | chpasswd

# Precheck: Ensure CUPS_PORT is a number or undefined
if [ -n "${CUPS_PORT:-}" ]; then
    if ! echo "$CUPS_PORT" | grep -Eq '^[0-9]+$'; then
        echo "Error: CUPS_PORT must be a valid number" >&2
        exit 1
    fi
fi

# Configure cupsd.conf
if [ -f /etc/cups/cupsd.conf ]; then
    sed -i "s/Listen localhost:631/Listen 0.0.0.0:${CUPS_PORT:-631}/" /etc/cups/cupsd.conf
    sed -i 's/Browsing Off/Browsing Yes/' /etc/cups/cupsd.conf
    sed -i 's/<Location \/>/<Location \/>\n  Allow All/' /etc/cups/cupsd.conf
    sed -i 's/<Location \/admin>/<Location \/admin>\n  Allow All/' /etc/cups/cupsd.conf
    sed -i 's/<Location \/admin\/conf>/<Location \/admin\/conf>\n  Allow All/' /etc/cups/cupsd.conf
    sed -i 's/<Location \/admin\/log>/<Location \/admin\/log>\n  Allow All/' /etc/cups/cupsd.conf

    echo "ServerAlias *" >> /etc/cups/cupsd.conf
    echo "DefaultEncryption Never" >> /etc/cups/cupsd.conf

    # Start CUPS in debug mode
    sed -i 's/LogLevel warn/LogLevel debug/' /etc/cups/cupsd.conf
else
    echo "/etc/cups/cupsd.conf not found"
    exit 1
fi

echo "Starting CUPS"

exec /usr/sbin/cupsd -f
