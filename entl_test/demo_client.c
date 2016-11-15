/* 
 * ENTL Device Demo Client
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
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>   // for nanosleep

#include "cJSON.h"

#include "entl_user_api.h"

typedef struct link_device {
  char *name;
  int linkState;
  int entlState;
  int entlCount;
  char AITMessageR[256];  
  char AITMessageS[256];
  char json[512];
} LinkDevice;

char *entlStateString[] = {"IDLE","HELLO","WAIT","SEND","RECEIVE","AM","BM","AH","BH","ERROR"};

static int sockfd, w_socket ;
static struct sockaddr_in sockaddr, w_sockaddr ;

#define PRINTF printf
#define DEFAULT_DBG_PORT  1337
static int sin_port ;

static int open_socket( char *addr ) {
  int flag = 1 ;
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

typedef pthread_mutex_t mutex_t;
static mutex_t access_mutex ;
#define ACCESS_LOCK pthread_mutex_lock( &access_mutex )  
#define ACCESS_UNLOCK pthread_mutex_unlock( &access_mutex )

#define NUM_INTERFACES 4
static int sock;
//static int sock[NUM_INTERFACES];
static struct entl_ioctl_data entl_data[NUM_INTERFACES] ;
static struct ifreq ifr[NUM_INTERFACES];
struct entt_ioctl_ait_data ait_data[NUM_INTERFACES] ;
LinkDevice links[NUM_INTERFACES];

char *machine_name = NULL ;

static int toJSON(struct link_device *dev) {
  int size = 0; 
  if (NULL != dev) {
    size = sprintf( dev->json,"{\"machineName\":\"%s\", \"deviceName\":\"%s\", \"linkState\":\"%s\", \"entlState\":\"%s\", \"entlCount\":\"%d\", \"AITSent\":\"%s\", \"AITRecieved\": \"%s\"}\n",
	     machine_name, dev->name,
	     ((dev->linkState)?"UP":"DOWN"),
	     ((dev->entlState<9)?entlStateString[dev->entlState]:"UNKNOWN"),
	     dev->entlCount,
       dev->AITMessageS,
	     dev->AITMessageR);
  }
  return size;
}

static void toServer(char *json) {
  write( w_socket, json, strlen(json) ) ;
  printf( "toServer:%s",json) ;
}

void entl_error_sig_handler(int signum) {
  int i = 0, lenObj[NUM_INTERFACES] = {0}, lenPut[NUM_INTERFACES] = {0};
  char putString[2048];

  if (SIGUSR1 == signum) {
    printf("***  entl_error_sig_handler got SIGUSR1 (%d) signal ***\n", signum);
    for(i= 0; i<NUM_INTERFACES; i++) {
      
      memset(&entl_data[i],0, sizeof(entl_data[i]));
      ifr[i].ifr_data = (char *) &(entl_data[i]);
      ACCESS_LOCK;
      
      if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_ERROR, &ifr[i]) == -1) {
      	ACCESS_UNLOCK;
      	printf("SIOCDEVPRIVATE_ENTL_RD_ERROR failed on %s\n", ifr[i].ifr_name);
	
      } else {
      	printf("SIOCDEVPRIVATE_ENTL_RD_ERROR succeded on %s\n", ifr[i].ifr_name);
      	links[i].entlState=entl_data[i].state.current_state;
      	links[i].entlCount=entl_data[i].state.event_i_know;
      	links[i].linkState=entl_data[i].link_state;
      	
      	lenObj[i] = toJSON(&links[i]);
        toServer(links[i].json) ;
        ACCESS_UNLOCK;
      	//write(putString,lenPut);
      }
    }
  } else {
    printf("*** entl_error_sig_handler got unknown  signal %d ***\n", signum);
  }
}


static void entl_ait_sig_handler( int signum ) {
  int i ;
  if( signum == SIGUSR2 ) {
    printf( "entl_ait_sig_handler got SIGUSR2 signal!!!\n") ;
    // Set parm pinter to ifr
    for (i=0; i<NUM_INTERFACES; i++) {
      memset(&ait_data[i], 0, sizeof(struct entt_ioctl_ait_data));
      ifr[i].ifr_data = (char *)&ait_data[i] ;
        // SIOCDEVPRIVATE_ENTL_RD_CURRENT
      ACCESS_LOCK ;
      if (ioctl(sock, SIOCDEVPRIVATE_ENTT_READ_AIT, &ifr[i]) == -1) {
        ACCESS_UNLOCK ;
        printf( "SIOCDEVPRIVATE_ENTT_READ_AIT failed on %s\n",ifr[i].ifr_name );
      }
      else {
        printf( "SIOCDEVPRIVATE_ENTT_READ_AIT successed on %s num_massage %d\n",ifr[i].ifr_name, ait_data[i].num_messages );
        if( ait_data[i].message_len ) {
          memcpy( links[i].AITMessageR, ait_data[i].data, ait_data[i].message_len ) ;
          toJSON(&links[i]) ;
          toServer(links[i].json) ;
        }
        else {
          printf( "  AIT Message Len is zero\n " ) ;
        }
        ACCESS_UNLOCK ;
      }
    }
  }
  else {
    printf( "entl_error_sig_handler got unknown %d signal.\n", signum ) ;
  }
}

// the ait message sender
static void entl_ait_sender( int i, char* msg ) {
    printf( "entl_ait_sender sending \"%s\"\n", msg ) ;
    // Set parm pinter to ifr
  ait_data[i].message_len = strlen(msg) + 1 ;
  sprintf( ait_data[i].data, "%s", msg ) ;
    ifr[i].ifr_data = (char *)&ait_data[i] ;
    // SIOCDEVPRIVATE_ENTL_RD_CURRENT
  ACCESS_LOCK ;
  if (ioctl(sock, SIOCDEVPRIVATE_ENTT_SEND_AIT, &ifr[i]) == -1) {
    printf( "SIOCDEVPRIVATE_ENTT_SEND_AIT failed on %s\n",ifr[i].ifr_name );
  }
  else {
    printf( "SIOCDEVPRIVATE_ENTT_SEND_AIT successed on %s\n",ifr[i].ifr_name );
  }
  ACCESS_UNLOCK ;

}

static char *port_name[NUM_INTERFACES] = {"enp6s0","enp7s0","enp8s0","enp9s0"};

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
            cJSON * root = cJSON_Parse(inlin);
            if( root ) {
              int i ;
              char *port = cJSON_GetObjectItem(root,"port")->valuestring ;
              char *message = cJSON_GetObjectItem(root,"message")->valuestring ;
              for( i = 0 ; i<NUM_INTERFACES; i++) {
                if( !strcmp(port, port_name[i]) ) {
                  printf( "port %s index %d message %s\n", port, i, message) ;
                  entl_ait_sender(i, message) ;
                  break ;
                }
              }
              cJSON_Delete(root);
            }
          }
        }
        else {
          sleep(1) ;
        }
    }
}

int main (int argc, char **argv){
  int i = 0;
  int lenObj[NUM_INTERFACES] = {0}, lenPut[NUM_INTERFACES] = {0};
  char putString[2048];
  int count = 0 ;
  int ait_port = 0 ;
  int port ;
  int flag = 1 ;
  struct timespec ts;

  pthread_mutex_init( &access_mutex, NULL ) ;

  if( argc != 2 ) {
    printf( "Usage %s <machine_name> (e.g. %s foobar )\n", argv[0], argv[0] ) ;
    return 0 ;
  }
  printf( "Server Address: Machine Name %s\n", argv[1] ) ;

  machine_name = argv[1] ;

  port = open_socket( "127.0.0.1" ) ;

  if( !port ) {
    printf( "Can't open socket\n" ) ;
    return 0 ;
  }
  w_socket =  sockfd ; // accept( sockfd, (struct sockaddr *)&w_sockaddr, &a_len ) ;
  //setsockopt( w_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) ;

  //for (i=0; i<1; i++) {
  for (i=0; i<NUM_INTERFACES; i++) {
    memset(&links[i], 0, sizeof(links[i])) ;
    links[i].name = port_name[i];
    links[i].entlState = 100 ; // unknown
    sprintf( links[i].AITMessageS, " ") ;
    sprintf( links[i].AITMessageR, " ") ;
    lenObj[i] = toJSON(&links[i]);
    toServer(links[i].json) ;
    
    // Creating socket
    /* if ((sock[i] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror("cannot create socket");
      return 0;
    }*/
  }
  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("cannot create socket");
    return 0;
  }

  signal(SIGUSR1, entl_error_sig_handler);
  signal(SIGUSR2, entl_ait_sig_handler);

  
  for (i=0; i<NUM_INTERFACES; i++) {
    memset(&ifr[i], 0, sizeof(ifr[i]));
    strncpy(ifr[i].ifr_name, port_name[i], sizeof(ifr[i].ifr_name));
    
    // Set my handler here
    //signal(SIGUSR2, entl_ait_sig_handler);
    memset(&entl_data[i], 0, sizeof(entl_data[i]));
    
    entl_data[i].pid = getpid() ;
    printf( "The pid is %d\n", entl_data[i].pid ) ;
    ifr[i].ifr_data = (char *)&(entl_data[i]);
  
    // SIOCDEVPRIVATE_ENTL_SET_SIGRCVR
    ACCESS_LOCK ;
  
    if (ioctl(sock, SIOCDEVPRIVATE_ENTL_SET_SIGRCVR, &ifr[i]) == -1) {
      printf( "SIOCDEVPRIVATE_ENTL_SET_SIGRCVR failed on %s\n",ifr[i].ifr_name );
    } else {
      printf( "SIOCDEVPRIVATE_ENTL_SET_SIGRCVR successed on %s\n",ifr[i].ifr_name );
      //links[i].entlState=entl_data[i].state.current_state;
      //links[i].entlCount=entl_data[i].state.event_i_know;
      //links[i].linkState=entl_data[i].link_state;
      
      //lenObj[i] = toJSON(&links[i]);
      //lenPut[i] = toPutString(lenObj[i],links[i].json, putString);
      //write(putString,lenPut);
      //printf("bytes = %d\n%s\n", lenPut[i], putString);
      //toServer(links[i].json);
    }
    ACCESS_UNLOCK ;

  }


  printf("Processing SIOCDEVPRIVATE_ENTL_RD_ERROR \n" );


  for(i = 0; i<NUM_INTERFACES; i++) {
    
    memset(&entl_data[i],0, sizeof(entl_data[i]));
    ifr[i].ifr_data = (char *) &(entl_data[i]);
    ACCESS_LOCK;
    
    if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_ERROR, &ifr[i]) == -1) {
      printf("SIOCDEVPRIVATE_ENTL_RD_ERROR failed on %s\n", ifr[i].ifr_name);

    } else {
      printf("SIOCDEVPRIVATE_ENTL_RD_ERROR succeded on %s\n", ifr[i].ifr_name);
    }
    ACCESS_UNLOCK;
  }

  pthread_create( &read_thread, NULL, read_task, NULL );

  printf("Entering app loop \n" );

  //ts.tv_sec = 0;
  //ts.tv_nsec = 3000000; // 300 ms

  while (1) {
    //for (i=0; i<1; i++) {
    for (i=0; i<NUM_INTERFACES; i++) {
      int modified = 0 ;
      memset(&entl_data[i], 0, sizeof(entl_data[i]));
      ifr[i].ifr_data = (char *)&entl_data[i] ;
      ACCESS_LOCK ;
     
      // SIOCDEVPRIVATE_ENTL_RD_CURRENT 
      if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_CURRENT, &(ifr[i])) == -1) {
	      ACCESS_UNLOCK ;
        printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",ifr[i].ifr_name );
      } else {
      	printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT successed on %s state %d\n",ifr[i].ifr_name, entl_data[i].state.current_state );
      	if( links[i].entlState != entl_data[i].state.current_state ||
            links[i].entlCount != entl_data[i].state.event_i_know ||
            links[i].linkState != entl_data[i].link_state
         ) 
        {
          modified = 1 ;
          links[i].entlState=entl_data[i].state.current_state;
          links[i].entlCount=entl_data[i].state.event_i_know;
          links[i].linkState=entl_data[i].link_state;
        
        }
      	
      	//write(putString,lenPut);
      	//printf("bytes = %d\n%s\n", lenPut[i], putString);
        ACCESS_UNLOCK ;
      }
      /*
      if( count > 10 && ait_port == i && links[i].linkState && links[i].entlState >= 3 &&  links[i].entlState <= 6 ) {
        count = 0 ;
        ait_port = (ait_port+1) % NUM_INTERFACES ;
        sprintf( links[i].AITMessageS, "AIT %s.%s on %d", machine_name, links[i].name, links[i].entlCount ) ;
        entl_ait_sender( i, links[i].AITMessageS ) ;
        modified = 1 ;
      }
      */
      if( modified) {
          lenObj[i] = toJSON(&links[i]);
          toServer(links[i].json);
      }
    }
    count++ ;

    //nanosleep(&ts, NULL);
    sleep(1);    
  }  
  return 0;
}
