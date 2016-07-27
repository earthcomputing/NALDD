/* 
 * ENTL Device Tester
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "entl_user_api.h"

#define MY_ADDR "192.168.99.1"

#define MY_DEVICE "enp6s0"

static void entl_rd_current( )


int main( int argc, char *argv[] ) {
	int sock;
	struct ifreq paifr;

	// Creating socet
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket");
		return 0;
	}

	// Try ioctl with interface name 
	memset(&paifr, 0, sizeof(paifr));
	strncpy(paifr.ifr_name, MY_DEVICE, sizeof(paifr.ifr_name));

	if (ioctl(s, SIOCGIFFLAGS, &ifreq) == -1) {
		printf( "SIOCGIFFLAGS failed on %s\n",paifr.ifr_name );
	}
	else {
		printf( "SIOCGIFFLAGS successed on %s\n",paifr.ifr_name );
	}

}