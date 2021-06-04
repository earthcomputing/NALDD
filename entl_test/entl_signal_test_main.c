/*---------------------------------------------------------------------------------------------
 *  Copyright © 2016-present Earth Computing Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE.txt in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

#include <stdio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "entl_user_api.h"

#define MY_ADDR "192.168.99.1"

#define MY_DEVICE "enp6s0"

// the signal handler
static void entl_error_sig_handler( int signum ) {
  if( signum == SIGUSR1 ) {
    printf( "entl_error_sig_handler got SIGUSR1 signal!!!\n") ;
  }
  else {
    printf( "entl_error_sig_handler got unknown %d signal.\n", signum ) ;
  }
}

static void dump_state( entl_state_t *st )
{
	printf( "event_i_know: %d  event_i_sent: %d event_send_next: %d current_state: %d @ %ld \n", 
		st->event_i_know, st->event_i_sent, st->event_send_next, st->current_state, st->update_time.tv_sec
	) ;
}

int main( int argc, char *argv[] ) {
	int sock;
  	struct entl_ioctl_data entl_data ;
  	struct ifreq ifr;

  	printf( "ENTL driver signal test.. \n" ) ;

	// Creating socet
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket");
		return 0;
	}

	// Try ioctl with interface name 
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, MY_DEVICE, sizeof(ifr.ifr_name));

	// Set my handler here
	signal(SIGUSR1, entl_error_sig_handler);

  	// Set parm pinter to ifr
	memset(&entl_data, 0, sizeof(entl_data));
	entl_data.pid = getpid() ;
	printf( "The pid is %d\n", entl_data.pid ) ;
  	ifr.ifr_data = (char *)&entl_data ;

  	// SIOCDEVPRIVATE_ENTL_RD_CURRENT
	if (ioctl(sock, SIOCDEVPRIVATE_ENTL_SET_SIGRCVR, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTL_SET_SIGRCVR failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTL_SET_SIGRCVR successed on %s\n",ifr.ifr_name );
		//dump_state( &entl_data.state ) ;
	}

	if (ioctl(sock, SIOCDEVPRIVATE_ENTL_GEN_SIGNAL, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTL_GEN_SIGNAL failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTL_GEN_SIGNAL successed on %s\n",ifr.ifr_name );
		//dump_state( &entl_data.state ) ;
		printf( "sleeping 10..\n") ;
		sleep(10) ;
	}

}
