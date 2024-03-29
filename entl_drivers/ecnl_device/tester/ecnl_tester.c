/*---------------------------------------------------------------------------------------------
 *  Copyright © 2016-present Earth Computing Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE.txt in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
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

//#include <netlink/netlink.h>
//#include <netlink/socket.h>
//#include <netlink/msg.h>
//#include <netlink/genl/genl.h>
#include <linux/genetlink.h>

#include "ecnl_user_api.h"

#define GENLMSG_DATA(glh) ((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh) (NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na) ((void *)((char*)(na) + NLA_HDRLEN))

#define MESSAGE_TO_KERNEL "Hello World!"

//Variables used for netlink
int nl_fd;  //netlink socket's file descriptor
struct sockaddr_nl nl_address; //netlink socket address
int nl_family_id; //The family ID resolved by the netlink controller for this userspace program
int nl_rxtx_length; //Number of bytes sent or received via send() or recv()
struct nlattr *nl_na; //pointer to netlink attributes structure within the payload 
struct { //memory for netlink request and response messages - headers are included
    struct nlmsghdr n;
    struct genlmsghdr g;
    char buf[256];
} nl_request_msg, nl_response_msg;


int main(void) {
	//Step 1: Open the socket. Note that protocol = NETLINK_GENERIC
    nl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (nl_fd < 0) {
  		perror("socket()");
  		return -1;
    }

	//Step 2: Bind the socket.
 	memset(&nl_address, 0, sizeof(nl_address));
 	nl_address.nl_family = AF_NETLINK;
 	nl_address.nl_groups = 0;

 	if (bind(nl_fd, (struct sockaddr *) &nl_address, sizeof(nl_address)) < 0) {
  		perror("bind()");
  		close(nl_fd);
  		return -1;
 	}

	//Step 3. Resolve the family ID corresponding to the string "CONTROL_EXMPL"
    //Populate the netlink header
    nl_request_msg.n.nlmsg_type = GENL_ID_CTRL;
    nl_request_msg.n.nlmsg_flags = NLM_F_REQUEST;
    nl_request_msg.n.nlmsg_seq = 0;
    nl_request_msg.n.nlmsg_pid = getpid();
    nl_request_msg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    //Populate the payload's "family header" : which in our case is genlmsghdr
    nl_request_msg.g.cmd = CTRL_CMD_GETFAMILY;
    nl_request_msg.g.version = 0x1;
    //Populate the payload's "netlink attributes"
 	nl_na = (struct nlattr *) GENLMSG_DATA(&nl_request_msg);
    nl_na->nla_type = CTRL_ATTR_FAMILY_NAME;
    nl_na->nla_len = strlen("ECNL_Family") + 1 + NLA_HDRLEN;
    strcpy(NLA_DATA(nl_na), "ECNL_Family"); //Family name length can be upto 16 chars including \0
    
    nl_request_msg.n.nlmsg_len += NLMSG_ALIGN(nl_na->nla_len);

 	memset(&nl_address, 0, sizeof(nl_address));
 	nl_address.nl_family = AF_NETLINK;

	//Send the family ID request message to the netlink controller
 	nl_rxtx_length = sendto(nl_fd, (char *) &nl_request_msg, nl_request_msg.n.nlmsg_len,
  		0, (struct sockaddr *) &nl_address, sizeof(nl_address));
 	if (nl_rxtx_length != nl_request_msg.n.nlmsg_len) {
 		perror("sendto()");
  		close(nl_fd);
  		return -1;
    }

 	//Wait for the response message
    nl_rxtx_length = recv(nl_fd, &nl_response_msg, sizeof(nl_response_msg), 0);
    if (nl_rxtx_length < 0) {
        perror("recv()");
        return -1;
    }

    //Validate response message
    if (!NLMSG_OK((&nl_response_msg.n), nl_rxtx_length)) {
        fprintf(stderr, "family ID request : invalid message\n");
        return -1;
    }
    if (nl_response_msg.n.nlmsg_type == NLMSG_ERROR) { //error
        fprintf(stderr, "family ID request : receive error\n");
        return -1;
    }

    //Extract family ID
    nl_na = (struct nlattr *) GENLMSG_DATA(&nl_response_msg);
    nl_na = (struct nlattr *) ((char *) nl_na + NLA_ALIGN(nl_na->nla_len));
    if (nl_na->nla_type == CTRL_ATTR_FAMILY_ID) {
        nl_family_id = *(__u16 *) NLA_DATA(nl_na);
    }

	//Step 4. Send own custom message
 	memset(&nl_request_msg, 0, sizeof(nl_request_msg));
 	memset(&nl_response_msg, 0, sizeof(nl_response_msg));

    nl_request_msg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    nl_request_msg.n.nlmsg_type = nl_family_id;
    nl_request_msg.n.nlmsg_flags = NLM_F_REQUEST;
    nl_request_msg.n.nlmsg_seq = 60;
    nl_request_msg.n.nlmsg_pid = getpid();
    nl_request_msg.g.cmd = NL_ECNL_CMD_GET_MODULE_INFO; // 
        
    nl_na = (struct nlattr *) GENLMSG_DATA(&nl_request_msg);
    nl_na->nla_type = NL_ECNL_ATTR_MODULE_ID; // corresponds to DOC_EXMPL_A_MSG
    nl_na->nla_len = sizeof(uint32_t)+NLA_HDRLEN; //Message length
    uint32_t *t = NLA_DATA(nl_na)  ;
    *t = 0 ; 
    nl_request_msg.n.nlmsg_len += NLMSG_ALIGN(nl_na->nla_len);

    memset(&nl_address, 0, sizeof(nl_address));
 	nl_address.nl_family = AF_NETLINK;

 	//Send the custom message
 	nl_rxtx_length = sendto(nl_fd, (char *) &nl_request_msg, nl_request_msg.n.nlmsg_len,
  		0, (struct sockaddr *) &nl_address, sizeof(nl_address));
 	if (nl_rxtx_length != nl_request_msg.n.nlmsg_len) {
  		perror("sendto()");
  		close(nl_fd);
  		return -1;
    }
    printf("Sent to kernel: %s\n",MESSAGE_TO_KERNEL);

    //Receive reply from kernel
    nl_rxtx_length = recv(nl_fd, &nl_response_msg, sizeof(nl_response_msg), 0);
    if (nl_rxtx_length < 0) {
        perror("recv()");
        return -1;
    }

 	//Validate response message
    if (nl_response_msg.n.nlmsg_type == NLMSG_ERROR) { //Error
        printf("Error while receiving reply from kernel: NACK Received\n");
        close(nl_fd);
        return -1;
    }
    if (nl_rxtx_length < 0) {
        printf("Error while receiving reply from kernel\n");
        close(nl_fd);
        return -1;
    }
    if (!NLMSG_OK((&nl_response_msg.n), nl_rxtx_length)) {
        printf("Error while receiving reply from kernel: Invalid Message\n");
        close(nl_fd);
     	return -1;
 	}

    //Parse the reply message
    nl_rxtx_length = GENLMSG_PAYLOAD(&nl_response_msg.n);
    nl_na = (struct nlattr *) GENLMSG_DATA(&nl_response_msg);
    printf("Kernel replied: %s\n",(char *)NLA_DATA(nl_na));

	//Step 5. Close the socket and quit
    close(nl_fd);
    return 0;
}
