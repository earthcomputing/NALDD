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

#define PRINTF printf
#define DEFAULT_DBG_PORT  2540

typedef struct {
  char *name;			/* User printable name of the function. */
  Function *func;		/* Function to call to do the job. */
  char *doc;			/* Documentation for this function.  */
} COMMAND;

static int com_quit(), com_start(), com_ait() ;

static COMMAND commands[] = {
	{ "quit", com_quit, "quit from program"},
	{ "start", com_start, "start entl mode"},
	{ "AIT", com_ait, "send AIT message"}
  { (char *)NULL, (Function *)NULL, (char *)NULL }
};

#include "entl_user_api.h"

static int sock;
static struct entl_ioctl_data entl_data ;
static struct ifreq ifr;
struct entt_ioctl_ait_data ait_data ;

static void write_window( char *str ) ;

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
	if( flag ) {
		write_window( "#State" ) ;
		switch( st->current_state ) {
			case ENTL_STATE_IDLE:
				write_window( "Idle" ) ;
			break ;
			case ENTL_STATE_HELLO:
				write_window( "Hello" ) ;
			break ;
			case ENTL_STATE_WAIT:
				write_window( "Wait" ) ;
			break ;
			case ENTL_STATE_SEND:
				write_window( "Send" ) ;
			break ;
			case ENTL_STATE_RECEIVE:
				write_window( "Receive" ) ;
			break ;
			case ENTL_STATE_AM:
				write_window( "AM" ) ;
			break ;
			case ENTL_STATE_BM:
				write_window( "BM" ) ;
			break ;
			case ENTL_STATE_AH:
				write_window( "AH" ) ;
			break ;
			case ENTL_STATE_BH:
				write_window( "BH" ) ;
			break ;
		}
	}
}

static void dump_ait( struct entt_ioctl_ait_data *dt ) 
{
	int i ;
	int len = dt->message_len ;
	if( dt->message_len > MAX_AIT_MASSAGE_SIZE ) {
		printf( "dump_ait: length too long %d\n", dt->message_len ) ;
		len = MAX_AIT_MASSAGE_SIZE ;
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
		dt->data[len] = 0 ;
		write_window( "#AIT" ) ;
		write_window( dt->data ) ;

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
			write_window( "#Link" ) ;
			write_window( "UP" ) ;
		}
		else {
			printf( "  Link Down!\n " ) ;
			dump_state( "current", &entl_data.state, 1 ) ;
			dump_state( "error", &entl_data.error_state, 0 ) ;
			dump_regs( &entl_data ) ;
			write_window( "#Link" ) ;
			write_window( "DOWN" ) ;
		}
	}
  }
  else {
    printf( "entl_error_sig_handler got unknown %d signal.\n", signum ) ;
  }
}

/* variables for debug window */
static int sockfd, w_socket ;
static struct sockaddr_in sockaddr, w_sockaddr ;
static int w_pid ;
static int m_pid = 0 ;
static int pipefd[2] ;
static int sin_port ;
#define INMAX 1024
static char inlin[INMAX];

static int open_socket() {
  int st = -1 ;
  int n = 0 ;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if( sockfd < 0 ) {
    PRINTF( "Can't create socket\n" ) ;
    return 0 ;
  }
  sockaddr.sin_family = AF_INET ;
  sockaddr.sin_addr.s_addr = htonl(INADDR_ANY) ; //inet_addr("127.0.0.1") ; /* local host */
  while( st < 0 && n < 100 ) {
    sockaddr.sin_port = htons(DEFAULT_DBG_PORT + n) ;
    sin_port = DEFAULT_DBG_PORT + n ;
    st = bind( sockfd, (struct sockaddr *) &sockaddr, sizeof(struct sockaddr) );
    n++ ;
  }
  if( st < 0 ) {
    PRINTF( 
      "Can't bind socket to the port %d\n",
      sockaddr.sin_port
    ) ;
    close( sockfd ) ;
    return 0 ;
  }
  else {
    
    PRINTF( 
      "Bind socket to the port %d %d\n",
      sockaddr.sin_port, sin_port
    ) ;
    
  }
  return sin_port ; // sockaddr.sin_port ;
}

static void com_window( char *dev_name ) {
	int id ;
  	int port, a_len ;
  	char port_c[100] ;

	if( pipe(pipefd) < 0 ) {
	    printf( "Can't open pipe\n" ) ;
	    exit(1) ;
	}

	port = open_socket() ;
    if( !port ) {
 	    printf( "Can't open socket\n" ) ;
   		return ;
  	}

  	if( listen( sockfd, 5 ) < 0 ) {
      PRINTF( "listen error on port %d\n", port ) ;
      close( sockfd ) ;
      return ;
    }

    w_pid = fork() ;
    if( w_pid == 0 ) {
      /* child process */
      	//sleep(5) ;
        //close( 1 ) ;
        //dup(pipefd[1]) ;
        sprintf( port_c, "%d", port ) ;
         
        fprintf( stderr, "execlp wish -f %s %s %s\n", "demo_window.tcl", dev_name, port_c) ;
        //sleep(5) ;

        execlp( "wish", "-f", "demo_window.tcl", dev_name, port_c, 0 ) ;
        exit(0) ;
    }
    else {
      /* get stdin from the pipe */
      printf( "I'm here!\n") ;
      close(0) ;
      dup(pipefd[0]) ;
      a_len = sizeof(w_sockaddr) ;
      w_socket =   
        accept( sockfd, (struct sockaddr *)&w_sockaddr, &a_len ) ;
      printf( "exit accept\n") ;
      if( w_socket < 0) {
        printf( "accept error on port %d\n", port ) ;
        close( sockfd ) ;
        kill( w_pid, SIGKILL ) ;
        return ;
      }
      printf( "accept on %d\n", w_socket ) ;
    }

}

static int com_quit() {
    close( sockfd ) ;
    kill( w_pid, SIGKILL ) ;
    exit(0) ;
}

static int com_start()
{
	if (ioctl(sock, SIOCDEVPRIVATE_ENTL_DO_INIT, &ifr) == -1) {
		printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",ifr.ifr_name );
	}
	else {
		printf( "SIOCDEVPRIVATE_ENTL_DO_INIT successed on %s\n",ifr.ifr_name );
	}
}

static int com_ait()
{
	char *cp = inlin ;
	while( cp != ' ' ) cp++ ;
	while( cp == ' ' ) cp++ ;
	entl_ait_sender(cp) ;
}

static void write_window( char *str ) {
  /* PRINTF( "soc: %s", str ) ; */
  write( w_socket, str, strlen(str) ) ;
}

static int read_window() {
 	int rr, n;
   	n = INMAX;
   	rr = read(w_socket,inlin,n);
   	if (rr <= 0) {
      	rr = 0;
   	}
   	inlin[rr] = '\0';
   	// printf( "got %s\n", inlin ) ;
   	return rr ;
}

static void exec_command()
{
	int i ;
	for( i = 0 ; commands[i].name ; i++ ) {
		if( strncmp(inlin, commands[i].name, strlen(commands[i].name) ) ){
			((*(command->func)) ());
			return ;
		}
	}
}

int main( int argc, char *argv[] ) {
	int count = 0 ;
	//u32 event_i_know = 0 ;
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


	// open window
  	com_window( name ) ;

  	while( 1 ) {

    	//printf( "sleeping 5 sec on %d\n", count++ ) ;
	
		sleep(1) ;

		memset(&entl_data, 0, sizeof(entl_data));
	  	ifr.ifr_data = (char *)&entl_data ;

	  	// SIOCDEVPRIVATE_ENTL_RD_CURRENT
	  	
		if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_CURRENT, &ifr) == -1) {
			printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",ifr.ifr_name );
		}
		else {
			printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT successed on %s\n",ifr.ifr_name );
			dump_state( "current", &entl_data.state, 1 ) ;
			//dump_regs( &entl_data ) ;
		}

        //write_window("#State\n") ;
        //write_window("Read\n") ;

        if( read_window() ) {
        	printf( "got %s\n", inlin ) ;
        	exec_command() ;
        }

	}
}

