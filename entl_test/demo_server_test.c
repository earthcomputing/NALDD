/* 
 * Node.js Communication test
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

static int sockfd, w_socket ;
static struct sockaddr_in sockaddr, w_sockaddr ;

static int sin_port ;

#define PRINTF printf
#define DEFAULT_DBG_PORT  1337

static int open_socket( char *addr ) {
  	int st = -1 ;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if( sockfd < 0 ) {
	    PRINTF( "Can't create socket\n" ) ;
	    return 0 ;
	}

	sockaddr.sin_family = AF_INET ;
	sockaddr.sin_addr.s_addr = inet_addr(addr) ; 

  sockaddr.sin_port = htons(DEFAULT_DBG_PORT) ;
    st = connect( sockfd, (struct sockaddr *) &sockaddr, sizeof(struct sockaddr) );
	sin_port = DEFAULT_DBG_PORT ;

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

#define INMAX 1024
static char inlin[INMAX];

static int read_window() {
 	int rr, n;
   	n = INMAX;
   	//printf( "calling read\n" ) ;
   	rr = read(w_socket,inlin,n);
   	//printf( "done read with %d \n", rr ) ;
   	if (rr <= 0) {
      	rr = 0;
   	}
   	inlin[rr] = '\0';
   	// printf( "got %s\n", inlin ) ;
   	return rr ;
}

static pthread_t read_thread ;

static void read_task( void* me )
{
	printf( "read_task started\n") ;
    while(1) {
    	if( read_window() ) {
        	if( inlin[0] != '\n' ) {
        	    printf( "got data: %s\n", inlin ) ;
        	}
        }
        else {
        	sleep(1) ;
        }
    }
}

char *entlStateString[] = {"IDLE","HELLO","WAIT","SEND","RECEIVE","AM","BM","AH","BH","ERROR"};

void main( int argc, char *argv[] ) {
	int port, a_len ;
	int count = 0 ;
	int err ;

  //if( argc != 2 ) {
  //  printf( "%s needs <server address> (e.g. 127.0.0.1) as the argument\n", argv[0] ) ;
  //  return ;
  //}
  //printf( "Server Address: %s \n", argv[1] ) ;

	port = open_socket( "127.0.0.1" ) ;

  if( !port ) {
	    printf( "Can't open socket\n" ) ;
 		return ;
	}
  printf( "got port %d\n", port ) ;

	printf( "listening %d\n", port ) ;

	//if( listen( sockfd, 5 ) < 0 ) {
  //  PRINTF( "listen error on port %d\n", port ) ;
  //  close( sockfd ) ;
  //  return ;
  //}
 	//printf( "accepting %d\n", port ) ;

  //a_len = sizeof(w_sockaddr) ;
  w_socket =  sockfd ; // accept( sockfd, (struct sockaddr *)&w_sockaddr, &a_len ) ;

  printf( "exit accept\n") ;
		//ioctl(sockfd, FIONBIO, &iMode);  
  if( 0 ) { // w_socket < 0) {
      printf( "accept error on port %d\n", port ) ;
      close( sockfd ) ;
      return ;
  }
  printf( "accept on %d\n", w_socket ) ;


  err = pthread_create( &read_thread, NULL, read_task, NULL );


	while( 1 ) {
		char message[512] ;
  	//printf( "sleeping 1 sec on %d\n", count ) ;

		sleep(1) ;

    sprintf( message ,"{\"machineName\": \"%s\", \"deviceName\": \"enp%ds0\", \"linkState\":\"%s\",\"entlState\": \"%s\", \"entlCount\": \"%d\",\"AITSent\": \"%s\",\"AITRecieved\": \"%s %d\"}",
       "demo_machine", (count%4)+6,
       ((count & 1)?"UP":"DOWN"),
       (((count % 11)<9)?entlStateString[count % 11]:"UNKNOWN"),
       count,
       "AIT sent",
       "Hello AIT",
       count);

    count++ ;

		write( w_socket, message, strlen(message) ) ;
		printf( "%s\n", message ) ;

	}


}