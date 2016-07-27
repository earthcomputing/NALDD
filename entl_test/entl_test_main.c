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

static void entl_rd_current( ) {

}

static void dump_state( entl_state_t *st )
{
	printf( "event_i_know: %d  event_i_sent: %d event_send_next: %d current_state: %d @ %d \n", 
		st->event_i_know, st->event_i_sent, st->event_send_next, st->current_state, st->update_time->tv_sec
	) ;
}

int main( int argc, char *argv[] ) {
	int sock;
  	struct entl_ioctl_data entl_data ;
  	struct ifreq ifr;

	// Creating socet
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket");
		return 0;
	}

	// Try ioctl with interface name 
	memset(&paifr, 0, sizeof(paifr));
	strncpy(paifr.ifr_name, MY_DEVICE, sizeof(paifr.ifr_name));

  	// Set parm pinter to ifr
  	ifr.ifr_data = (char *)entl_data ;

	if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_CURRENT, &paifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",paifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT successed on %s\n",paifr.ifr_name );
		dump_state( &etl_data.state ) ;
	}

}