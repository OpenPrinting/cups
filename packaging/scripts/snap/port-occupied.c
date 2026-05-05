/* Copyright (C) 2014-2020 Daniel Dressler, Till Kamppeter, and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Taken from the ippusbxd (https://github.com/OpenPrinting/ippusbxd) code.
 *
 * To check whether a port is in use or not, this program tries to
 * bind to the given port (on localhost) and in case of success
 * releases it immediately again. A return value of 0 indicates that the
 * port is free, 1 indicates that the port is already in use.
 * To check privileged ports (0..1023) the program has to be run as root.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  int raw_port;
  uint16_t port;
  char interface[] = "lo";

  struct tcp_sock_t {
    int sd;
    struct sockaddr_in6 info;
    socklen_t info_size;
  };

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    return(-1);
  }

  raw_port = atoi(argv[1]);
  if (raw_port < 0 || raw_port > 65535) {
    fprintf(stderr, "Invalid port: %d (must be 0 <= <port> <= 65535)\n",
	    raw_port);
    return(-1);
  }
  port = raw_port;
  
  struct tcp_sock_t *this = calloc(1, sizeof *this);
  if (this == NULL) {
    fprintf(stderr, "IPv4: callocing this failed.\n");
    goto error;
  }

  /* Open [S]ocket [D]escriptor */
  this->sd = -1;
  this->sd = socket(AF_INET, SOCK_STREAM, 0);
  if (this->sd < 0) {
    fprintf(stderr, "IPv4 socket open failed.\n");
    goto error;
  }
  /* Set SO_REUSEADDR option to allow for a clean host/port unbinding even with
     pending requests on shutdown of ippusbxd. Otherwise the port will stay
     unavailable for a certain kernel-defined timeout. See also
     http://stackoverflow.com/questions/10619952/how-to-completely-destroy-a-socket-connection-in-c */
  int true = 1;
  if (setsockopt(this->sd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1) {
    fprintf(stderr, "IPv4 setting socket options failed.\n");
    goto error;
  }

  /* Find the IP address for the selected interface */
  struct ifaddrs *ifaddr, *ifa;
  getifaddrs(&ifaddr);
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL)
      continue;
    if ((strcmp(ifa->ifa_name, interface) == 0) &&
	(ifa->ifa_addr->sa_family == AF_INET))
      break;
  }
  if (ifa == NULL) {
    fprintf(stderr, "Interface %s does not exist or IPv4 IP not found.\n",
	    interface);
    goto error;
  }

  /* Configure socket params */
  struct sockaddr_in addr, *if_addr;
  if_addr = (struct sockaddr_in *) ifa->ifa_addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = if_addr->sin_addr.s_addr;
  /* addr.sin_addr.s_addr = htonl(0xC0A8000F); */
  fprintf(stderr, "IPv4: Binding to %s:%d ...\n", inet_ntoa(if_addr->sin_addr), port);

  /* Bind to the interface/IP/port */
  if (bind(this->sd,
	   (struct sockaddr *)&addr,
	   sizeof addr) < 0) {
    fprintf(stderr, "IPv4 bind on port failed. "
	    "Requested port may be taken or require root permissions.\n");
    goto error;
  }
  fprintf(stderr, "IPv4 bind on port %d succeeded.\n", port);

  /* Let kernel over-accept max number of connections */
  if (listen(this->sd, 0) < 0) {
    fprintf(stderr, "IPv4 listen failed on socket.\n");
    goto error;
  }
  fprintf(stderr, "IPv4 listen port %d succeeded.\n", port);

  /* Unbind host/port cleanly even with pending requests. Otherwise
     the port will stay unavailable for a certain kernel-defined
     timeout. See also
     http://stackoverflow.com/questions/10619952/how-to-completely-destroy-a-socket-connection-in-c */
  shutdown(this->sd, SHUT_RDWR);
  close(this->sd);
  fprintf(stderr, "IPv4: Port %d released.\n", port);
  free(this);

  return(1);
  
 error:
  if (this != NULL) {
    if (this->sd != -1) {
      close(this->sd);
    }
    free(this);
  }
  return(0);
}
