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

static int sock;
static struct entl_ioctl_data entl_data ;
static struct ifreq ifr;
struct entt_ioctl_ait_data ait_data ;

static void dump_regs( struct entl_ioctl_data *data ) {
	printf( " icr = %08x ctrl = %08x ims = %08x\n", data->icr, data->ctrl, data->ims ) ;
}

static void dump_state( char *type, entl_state_t *st, int flag )
{
	printf( "%s event_i_know: %d  event_i_sent: %d event_send_next: %d current_state: %d error_flag %x p_error %x error_count %d @ %ld.%ld \n", 
		type, st->event_i_know, st->event_i_sent, st->event_send_next, st->current_state, st->error_flag, st->p_error_flag, st->error_count, st->update_time.tv_sec, st->update_time.tv_nsec
	) ;
	if( st->error_flag ) {
		printf( "  Error time: %ld.%ld\n", st->error_time.tv_sec, st->error_time.tv_nsec ) ;
	}
#ifdef ENTL_SPEED_CHECK
	if( flag ) {
		printf( "  interval_time    : %ld.%ld\n", st->interval_time.tv_sec, st->interval_time.tv_nsec ) ;
		printf( "  max_interval_time: %ld.%ld\n", st->max_interval_time.tv_sec, st->max_interval_time.tv_nsec ) ;
		printf( "  min_interval_time: %ld.%ld\n", st->min_interval_time.tv_sec, st->min_interval_time.tv_nsec ) ;
	}
#endif
}

static void dump_ait( struct entt_ioctl_ait_data *dt ) 
{
	int i ;
	int len = dt->message_len ;
	if( dt->message_len > MAX_AIT_MESSAGE_SIZE ) {
		printf( "dump_ait: length too long %d\n", dt->message_len ) ;
		len = MAX_AIT_MESSAGE_SIZE ;
		printf( "AIT data :" ) ;
		for( i = 0 ; i < len; i++ ) {
			printf( "%02x ", dt->data[i]) ;
		}
	}
	else {
			printf( "AIT message :" ) ;
		for( i = 0 ; i < len; i++ ) {
			printf( "%c", dt->data[i]) ;
		}
		
	}

	printf( "\n" ) ;
}

// the signal handler
static void entl_ait_sig_handler( int signum ) {
  if( signum == SIGUSR2 ) {
    printf( "entl_ait_sig_handler got SIGUSR2 signal!!!\n") ;
  	// Set parm pinter to ifr
	memset(&ait_data, 0, sizeof(ait_data));
  	ifr.ifr_data = (char *)&ait_data ;
  	// SIOCDEVPRIVATE_ENTL_RD_CURRENT
	if (ioctl(sock, SIOCDEVPRIVATE_ENTT_READ_AIT, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTT_READ_AIT failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTT_READ_AIT successed on %s num_massage %d\n",ifr.ifr_name, ait_data.num_messages );
		if( ait_data.message_len ) {
			dump_ait( &ait_data ) ;
		}
		else {
			printf( "  AIT Message Len is zero\n " ) ;
		}
	}
  }
  else {
    printf( "entl_error_sig_handler got unknown %d signal.\n", signum ) ;
  }
}

// the ait message sender
static void entl_ait_sender( char* msg ) {
    printf( "entl_ait_sender sending \"%s\"\n", msg ) ;
  	// Set parm pinter to ifr
	ait_data.message_len = strlen(msg) + 1 ;
	sprintf( ait_data.data, "%s", msg ) ;
  	ifr.ifr_data = (char *)&ait_data ;
  	// SIOCDEVPRIVATE_ENTL_RD_CURRENT
	if (ioctl(sock, SIOCDEVPRIVATE_ENTT_SEND_AIT, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTT_SEND_AIT failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTT_SEND_AIT successed on %s\n",ifr.ifr_name );
	}
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
		if( entl_data.link_state ) {
			printf( "  Link Up!\n " ) ;
			dump_state( "current", &entl_data.state, 1 ) ;
			dump_state( "error", &entl_data.error_state, 0 ) ;
			dump_regs( &entl_data ) ;
		}
		else {
			printf( "  Link Down!\n " ) ;
			dump_state( "current", &entl_data.state, 1 ) ;
			dump_state( "error", &entl_data.error_state, 0 ) ;
			dump_regs( &entl_data ) ;
		}
	}
  }
  else {
    printf( "entl_error_sig_handler got unknown %d signal.\n", signum ) ;
  }
}

int main( int argc, char *argv[] ) {
	int count = 0 ;
	u32 event_i_know = 0 ;
	char *name = argv[1] ;

	if( argc != 2 ) {
		printf( "%s needs <device name> (e.g. enp6s0) as the argument\n", argv[0] ) ;
		return 0 ;
	}
  	printf( "ENTL driver test on %s.. \n", argv[1] ) ;

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
	signal(SIGUSR2, entl_ait_sig_handler);

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
		dump_state( "current", &entl_data.state, 1 ) ;
		dump_regs( &entl_data ) ;
	}

  	// SIOCDEVPRIVATE_ENTL_DO_INIT
	if (ioctl(sock, SIOCDEVPRIVATE_ENTL_DO_INIT, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTL_DO_INIT successed on %s\n",ifr.ifr_name );
	}

    while( 1 ) {
    	printf( "sleeping 5 sec on %d\n", count++ ) ;
	
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
			printf( "  Link state : %s\n", entl_data.link_state? "UP" : "DOWN" ) ;
			dump_state( "current", &entl_data.state, 1 ) ;
			dump_regs( &entl_data ) ;
			if( entl_data.link_state && entl_data.state.current_state > ENTL_STATE_SEND ) 
			{
				if( event_i_know && entl_data.state.event_i_know > event_i_know ) {
  					char data[MAX_AIT_MESSAGE_SIZE] ;
  					sprintf( data, "AIT %s %d", name, entl_data.state.event_i_know ) ;
  					entl_ait_sender( data ) ;
				}
				event_i_know = entl_data.state.event_i_know ;
			}

		}
    }

    
}
