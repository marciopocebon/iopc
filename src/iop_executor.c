/*
    The InterOperability Platform: IOP
    Copyright (C) 2004 Ian A. Mason
    School of Mathematics, Statistics, and Computer Science   
    University of New England, Armidale, NSW 2351, Australia
    iam@turing.une.edu.au           Phone:  +61 (0)2 6773 2327 
    http://mcs.une.edu.au/~iam/     Fax:    +61 (0)2 6773 3312 


    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "cheaders.h"
#include "constants.h"
#include "types.h"
#include "actor.h"
#include "iop_lib.h"
#include "msg.h"
#include "externs.h"
#include "dbugflags.h"
#include "ec.h"

static int requestNo = 0;

static void executor_sigchild_handler(int sig){
  /* for the prevention of zombies */
  pid_t child;
  int status;
  child = waitpid(-1, &status, WNOHANG); 
}

static int executor_installHandler(void){
  struct sigaction sigactchild;
  sigactchild.sa_handler = executor_sigchild_handler;
  sigactchild.sa_flags = 0;
  ec_neg1( sigfillset(&sigactchild.sa_mask) );
  ec_neg1( sigaction(SIGCHLD, &sigactchild, NULL) );
  return 0;
EC_CLEANUP_BGN
  return -1;
EC_CLEANUP_END
}


int main(int argc, char** argv){
  msg* message = NULL;
  char *sender, *body;
  int retval, errcode;

  self = argv[0];
  self_debug_flag = EXECUTOR_DEBUG;

  if(executor_installHandler() < 0){
    fprintf(stderr, "couldn't install handler\n");
    exit(EXIT_FAILURE);
  }

  while(1){
    requestNo++;
    freeMsg(message);
    message = acceptMsg(STDIN_FILENO);
    if(message == NULL){
      perror("executor readMsg failed");
      continue;
    }
    announce("received:\"%s\"\n", message->data);
    retval = parseActorMsg(message->data, &sender, &body);
    if(!retval){
      fprintf(stderr, "executor didn't understand: \n\t \"%s\" \n", message->data);
      continue;
    }
    if(body == NULL){
      fprintf(stderr, "executor didn't understand: (body == NULL)\n");
      errcode = -1;
    } else {
      pid_t child = fork();
      if(child < 0){
	fprintf(stderr, "executor couldn't fork!\n");
	errcode = -1;
      } else if(child > 0){
	continue;
      } else {
	errcode = system(body);
      }
    }
    announce("%s\n%s\nexecuteOK\n%d\n", sender, self, errcode);
    sendFormattedMsgFP(stdout, "%s\n%s\nexecuteOK\n%d\n", sender, self, errcode);
    break;
  }
  return EXIT_SUCCESS;
}
