# CUPS-Rock Testing Guide

This guide provides step-by-step instructions to test printing using `cups-rock`. Ensure you have the following requirements installed before proceeding.

## Requirements

- Docker

  **Follow the [official Docker installation guide](https://docs.docker.com/get-docker/) for your operating system.**

- `cups-ipp-utils`
- `cups-client`

  Install the required packages using the following commands:

  ```sh
  sudo apt-get update
  sudo apt-get install cups-ipp-utils cups-client -y
  ```
## Step-by-Step Guide to Test CUPS-Rock

### 1. Setup IPP Everywhere Printer
To simulate a printer for testing, use `ippeveprinter`:

```sh
/usr/sbin/ippeveprinter -f application/pdf myprinter
```

### 2. Start the CUPS Container
Open another terminal session and start the CUPS container with the following environment variables:

```sh
CUPS_ADMIN="print"
CUPS_PASSWORD="print"
CUPS_PORT=631
```

#### Run the following Docker command to start cups available on docker hub:
```sh
sudo docker run --rm -d --name cups --network host \
    -e CUPS_PORT="${CUPS_PORT}" \
    -e CUPS_ADMIN="${CUPS_ADMIN}" \
    -e CUPS_PASSWORD="${CUPS_PASSWORD}" \
    openprinting/cups:latest
```

Alternatively:

#### Use Rockcraft to build image locally
1. Install rockcraft:
    ```sh
    sudo snap install rockcraft --classic
    ```
2. Pack with Rockcraft:
    ```sh
    sudo rockcraft pack -v
    ```
3. Compile Docker Image Using Skopeo:
    ```sh
    ROCK="$(ls *.rock | tail -n 1)"
    sudo rockcraft.skopeo --insecure-policy copy oci-archive:"${ROCK}" docker-daemon:cups:latest
    ```
3. Run the CUPS Docker Container:
    ```sh
    ```sh
    sudo docker run --rm -d --name cups --network host \
        -e CUPS_PORT="${CUPS_PORT}" \
        -e CUPS_ADMIN="${CUPS_ADMIN}" \
        -e CUPS_PASSWORD="${CUPS_PASSWORD}" \
        cups:latest
    ```

### 3. Add Test Printer to CUPS Server
You can either use the `CUPS web interface` to add a test printer or use the following command:
```sh
sudo docker exec -u "${CUPS_ADMIN}" cups lpadmin -p testprinter \
    -v ipps://myprinter._ipps._tcp.local/ -E -m everywhere
```
    
### 4. Test Printing Files
#### From Host System
To test printing files from the host system:

```sh
CUPS_SERVER=localhost:CUPS_PORT lp -d testprinter <file>
```

To check the print status:
```sh
CUPS_SERVER=localhost:CUPS_PORT lpstat -W completed
```

#### From Inside the Container
##### To test printing files without creating any print queue
Print a file present inside container without creating print queue
```sh
sudo docker exec -u "${CUPS_ADMIN}" cups lp -d myprinter /share/cups/ipptool/testfile.txt
```

##### To test printing files from inside the container:
1. Copy the file inside the container:
    ```sh
    sudo docker cp <testfile> cups:<testfile>
    ```
2. Print the file using the following command:
    ```sh
    sudo docker exec -u "${CUPS_ADMIN}" cups lp -d testprinter <testfile>
    ```

##### Print File Already Present Inside Container
To print a file already present inside the container:
```sh
sudo docker exec -u "${CUPS_ADMIN}" cups lp -d testprinter /share/cups/ipptool/testfile.txt
```

Check Job Status
```sh
sudo docker exec -u "${CUPS_ADMIN}" cups lpstat -W completed
```

You can also use the CUPS web interface to check the job status.

## Notes
- Ensure all the environment variables are correctly set before running the Docker commands.
- The CUPS web interface can be accessed at `http://localhost:CUPS_PORT` to manage printers and check job statuses.
- **The container must be started in `--network host` mode** to allow the CUPS instance inside the container to access and discover local printers running on the host system.
