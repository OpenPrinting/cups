Creating an OpenPrinting CUPS Docker Image
==========================================


Prerequisites
-------------

- Install Docker on your system


Building
--------

To build the CUPS Docker image, follow these steps:

1. Clone this repository to your local machine.

2. Navigate to the root directory of the cloned repository.

3. Run the following command to build the Docker image:

   ```
   docker build .
   ```


Running
-------

1. To create and run a container with the CUPS image:

   ```
   docker-compose up -d
   ```

2. To start an interactive terminal in the container

    ```
    docker exec -it cups /bin/bash
    ```


Additional Information
----------------------

1. Use 'admin' as CUPS username and 'admin' as password.

2. You can find the CUPS configuration files and log files in the docker-config
   directory of the repository on your local machine. Any changes made to these
   files will be reflected in the CUPS service running inside the Docker
   container.

3. The CUPS web interface is accessible at http://localhost:631 from your web
   browser.
