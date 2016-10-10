/* 
 * ENTL Device Tester
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Mike Mian
 *
 */


#ifndef _CLIENT_WEB_H_
#define _CLIENT_WEB_H_

#include "entl_state_defs.h"

static char HTTPHEADER[] = "PUT /earthUpdate HTTP/1.1\nHost: localhost:3000\nUser-Agent: earth-web-monitor web 0.1\nAccept: */*\nContent-Type: application/json\nContent-Length:";


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

#define DEFAULT_DBG_PORT  3000

#endif
