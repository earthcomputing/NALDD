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
#include <signal.h>

#include "entl_user_api.h"

#define MY_ADDR "192.168.99.1"

#define MY_DEVICE "enp6s0"

static int sock;
static struct entl_ioctl_data entl_data ;
static struct ifreq ifr;

static void dump_state( entl_state_t *st )
{
	printf( "event_i_know: %d  event_i_sent: %d event_send_next: %d current_state: %d error_flag %x p_error %x error_count %d @ %ld \n", 
		st->event_i_know, st->event_i_sent, st->event_send_next, st->current_state, st->error_flag, st->p_error_flag, st->error_count, st->update_time.tv_sec
	) ;
}

// the signal handler
static void entl_error_sig_handler( int signum ) {
  if( signum == SIGUSR1 ) {
    printf( "entl_error_sig_handler got SIGUSR1 signal!!!\n") ;
  	// Set parm pinter to ifr
	memset(&entl_data, 0, sizeof(entl_data));
  	ifr.ifr_data = (char *)&entl_data ;
  	// SIOCDEVPRIVATE_ENTL_RD_CURRENT
	if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_ERROR, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTL_RD_ERROR failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTL_RD_ERROR successed on %s\n",ifr.ifr_name );
		dump_state( &entl_data.state ) ;
	}
  }
  else {
    printf( "entl_error_sig_handler got unknown %d signal.\n", signum ) ;
  }
}

int main( int argc, char *argv[] ) {

	if( argc != 1 ) {
		printf( "%s needs <device name> (e.g. enp6s0) as the argument\n", argv[0] ) ;
		return 0 ;
	}
  	printf( "ENTL driver test on %s.. \n" argv[1] ) ;

	// Creating socet
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket");
		return 0;
	}

	// Try ioctl with interface name 
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, argv[1], sizeof(ifr.ifr_name));

	// Set my handler here
	signal(SIGUSR1, entl_error_sig_handler);

  	// Set parm pinter to ifr
	memset(&entl_data, 0, sizeof(entl_data));
	entl_data.pid = getpid() ;
	printf( "The pid is %d\n", entl_data.pid ) ;
  	ifr.ifr_data = (char *)&entl_data ;

  	// SIOCDEVPRIVATE_ENTL_SET_SIGRCVR
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

  	// Set parm pinter to ifr
	memset(&entl_data, 0, sizeof(entl_data));
  	ifr.ifr_data = (char *)&entl_data ;

  	// SIOCDEVPRIVATE_ENTL_RD_CURRENT
	if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_CURRENT, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT successed on %s\n",ifr.ifr_name );
		dump_state( &entl_data.state ) ;
	}

  	// SIOCDEVPRIVATE_ENTL_DO_INIT
	if (ioctl(sock, SIOCDEVPRIVATE_ENTL_DO_INIT, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTL_DO_INIT successed on %s\n",ifr.ifr_name );
	}

    while( 1 ) {
    	printf( "sleeping 5 sec\n" ) ;
	
		sleep(5) ;

  		// Set parm pinter to ifr
		memset(&entl_data, 0, sizeof(entl_data));
  		ifr.ifr_data = (char *)&entl_data ;

  		// SIOCDEVPRIVATE_ENTL_RD_CURRENT
		if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_CURRENT, &ifr) == -1) {
			printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",ifr.ifr_name );
		}
		else {
			printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT successed on %s\n",ifr.ifr_name );
			dump_state( &entl_data.state ) ;
		}
    }

    
}