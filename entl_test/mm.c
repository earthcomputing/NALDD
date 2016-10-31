/* 
 * ENTL Device Tester
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Mike Mian
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

#include "entl_user_api.h"
#include "web.h"

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

int toJSON(struct link_device *dev) {
  int size = 0; 
  if (NULL != dev) {
    size = sprintf( dev->json,"{\"deviceName\": \"%s\", \"linkState\":\"%s\",\"entlState\": \"%s\", \"entlCount\": \"%d\",\"AITMessage\": \"%s\"}",
	     dev->name,
	     ((dev->linkState)?"UP":"DOWN"),
	     ((dev->entlState<9)?entlStateString[dev->entlState]:"UNKNOWN"),
	     dev->entlCount,
	     dev->AITMessageR);
  }
  return size;
}
void toCurl(char *json) {
  char comm[4096];
  sprintf (comm, "curl -v -H \"Content-Type: application/json\" -X PUT -d \'%s\' http://localhost:3000/earthUpdate", json);
  printf("%s\n", comm); 

  system(comm);
}

int toPutString(int len, char *json,  char *out) {
  return sprintf(out, "%s %d\n%s", HTTPHEADER, len, json);
  
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
	ACCESS_UNLOCK;
	printf("SIOCDEVPRIVATE_ENTL_RD_ERROR succeded on %s\n", ifr[i].ifr_name);
	links[i].entlState=entl_data[i].state.current_state;
	links[i].entlCount=entl_data[i].state.event_i_know;
	links[i].linkState=entl_data[i].link_state;
	
	lenObj[i] = toJSON(&links[0]);
	lenPut[i] = toPutString(lenObj[i],links[i].json, putString);
	//write(putString,lenPut);
	printf("bytes = %d\n%s\n", lenPut[i], putString);
      }
    }
  } else {
    printf("*** entl_error_sig_handler got unknown  signal %d ***\n", signum);
  }
}
int main (int argc, char **argv){
  int i = 0;
  int lenObj[NUM_INTERFACES] = {0}, lenPut[NUM_INTERFACES] = {0};
  char putString[2048];
  
  char *name[NUM_INTERFACES] = {"enp6s0","enp7s0","enp8s0","enp9s0"};


  //for (i=0; i<1; i++) {
  for (i=0; i<NUM_INTERFACES; i++) {
    links[i].name = name[i];
    lenObj[i] = toJSON(&links[i]);
  
  
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
  
  for (i=0; i<NUM_INTERFACES; i++) {
    memset(&ifr[i], 0, sizeof(ifr[i]));
    strncpy(ifr[i].ifr_name, name[i], sizeof(ifr[i].ifr_name));
    
    // Set my handler here
    signal(SIGUSR1, entl_error_sig_handler);
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
      links[i].entlState=entl_data[i].state.current_state;
      links[i].entlCount=entl_data[i].state.event_i_know;
      links[i].linkState=entl_data[i].link_state;
      
      lenObj[i] = toJSON(&links[i]);
      //lenPut[i] = toPutString(lenObj[i],links[i].json, putString);
      //write(putString,lenPut);
      //printf("bytes = %d\n%s\n", lenPut[i], putString);
      toCurl(links[i].json);
    }
    ACCESS_UNLOCK ;
  }
  while (1) {
    //for (i=0; i<1; i++) {
    for (i=0; i<NUM_INTERFACES; i++) {
      
      memset(&entl_data[i], 0, sizeof(entl_data[i]));
      ifr[i].ifr_data = (char *)&entl_data[i] ;
      
      // SIOCDEVPRIVATE_ENTL_RD_CURRENT 
      if (ioctl(sock, SIOCDEVPRIVATE_ENTL_RD_CURRENT, &(ifr[i])) == -1) {
	printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT failed on %s\n",ifr[i].ifr_name );
      } else {
	printf( "SIOCDEVPRIVATE_ENTL_RD_CURRENT successed on %s\n",ifr[i].ifr_name );
	
	links[i].entlState=entl_data[i].state.current_state;
	links[i].entlCount=entl_data[i].state.event_i_know;
	links[i].linkState=entl_data[i].link_state;
	
	lenObj[i] = toJSON(&links[i]);
	lenPut[i] = toPutString(lenObj[i],links[i].json, putString);
	//write(putString,lenPut);
	//printf("bytes = %d\n%s\n", lenPut[i], putString);
	toCurl(links[i].json);
      }
    }
    sleep(1);    
  }  
  return 0;
}
