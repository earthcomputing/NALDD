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
#define DEFAULT_DBG_PORT  7775

/* variables for debug window */
static int sockfd, w_socket ;
static struct sockaddr_in sockaddr, w_sockaddr ;
static int w_pid ;
static int m_pid = 0 ;
static int pipefd[2] ;


static int open_socket() {
  int st = -1 ;
  int n = 0 ;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if( sockfd < 0 ) {
    PRINTF( "Can't create socket\n" ) ;
    return 0 ;
  }
  sockaddr.sin_family = AF_INET ;
  sockaddr.sin_addr.s_addr = inet_addr("127.0.0.1") ; /* local host */
  while( st < 0 && n < 100 ) {
    sockaddr.sin_port = htons(DEFAULT_DBG_PORT + n) ;
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
      "Bind socket to the port %d\n",
      sockaddr.sin_port
    ) ;
    
  }
  return sockaddr.sin_port ;
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
      m_pid = fork() ;
      if( m_pid == 0 ) {
        /* forward original stdin to the new pipe */
        int c ;
        close(1) ;
        dup(pipefd[1]) ;
        while( 1 ) {
          c = getchar() ;
          if( c != '\f' ) putchar( c ) ;
        }
      }
      else {
        close( 1 ) ;
        dup(pipefd[1]) ;
        sprintf( port_c, "%d", port ) ;
         
        fprintf( stderr, "execlp wish -f %s %s %s\n", "demo_window.tcl", dev_name, port_c) ;
        
        execlp( "wish", "-f", "demo_window.tcl", dev_name, port_c, 0 ) ;
        exit(0) ;
      }
    }
    else {
      /* get stdin from the pipe */
      close(0) ;
      dup(pipefd[0]) ;
      a_len = sizeof(w_sockaddr) ;
      w_socket =   
        accept( sockfd, (struct sockaddr *)&w_sockaddr, &a_len ) ;
      if( w_socket < 0) {
        PRINTF( "accept error on port %d\n", port ) ;
        close( sockfd ) ;
        kill( w_pid, SIGKILL ) ;
        return ;
      }
    }

}

void write_window( char *str ) {
  /* PRINTF( "soc: %s", str ) ; */
  write( w_socket, str, strlen(str) ) ;
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

  	com_window( name ) ;

  	while( 1 ) {
    	printf( "sleeping 5 sec on %d\n", count++ ) ;
	
		sleep(5) ;
	}
}

