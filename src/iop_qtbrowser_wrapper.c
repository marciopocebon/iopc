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
#include "msg.h"
#include "iop_lib.h"
#include "dbugflags.h"
#include "wrapper_lib.h" 
#include "externs.h"
#include "ec.h"

#define QTBROWSER_WRAPPER_DEBUG  0

#define DISPLAY_MAX  1024

static char display[DISPLAY_MAX]  = "";

static char  qtbrowser_exe[]  = "qtbrowser";
static char* qtbrowser_argv[] = { NULL };

pid_t child_b                      = -1;

static  int child_died = 0;

static void chld_handler(int sig){
  fprintf(stderr, "%s died! Exiting\n", self);
  /*
    child_died = 1;
    sendFormattedMsgFD(STDOUT_FILENO, "system\n%s\nstop %s\n", self, self);
  */
}

void forward(int fd, char* body){
  write(fd, body, strlen(body));
  write(fd, "\n", sizeof(char));
}


int main(int argc, char** argv){
  int pin[2], pout[2], perr[2];
  if((argc !=  1) && (argc !=  2)){
    fprintf(stderr, "Usage: %s [displayname]\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  self_debug_flag  = QTBROWSER_WRAPPER_DEBUG;
  self = argv[0];
  
  if(argc == 1){
    char* ed = getenv("DISPLAY");
    if(ed == NULL){ 
      fprintf(stderr, "%s found no display.\n", argv[0]);
      exit(EXIT_FAILURE);
    } else {
      strncpy(display, ed, DISPLAY_MAX);
    }
  } else {
    strncpy(display, argv[1], DISPLAY_MAX);
  }
  
  
  
  ec_neg1( wrapper_installHandler(chld_handler, wrapper_sigint_handler) );


  /* set the virtual display */
  if(argc == 2){ 
    ec_neg1( setenv("DISPLAY", display,  1) );
  }


  ec_neg1( pipe(pin) );
  ec_neg1( pipe(perr) );
  ec_neg1( pipe(pout) );

  /*it's time to fork */
  ec_neg1( child_b = fork() );

  if(child_b == 0){
    /* i'm destined to be qtbrowser  */
    ec_neg1( dup2(pin[0],  STDIN_FILENO) );
    ec_neg1( dup2(perr[1], STDERR_FILENO) );
    ec_neg1( dup2(pout[1], STDOUT_FILENO) );

    ec_neg1( close(pin[0]) );
    ec_neg1( close(perr[1]) );
    ec_neg1( close(pout[1]) );

    
    ec_neg1( execvp(qtbrowser_exe, qtbrowser_argv) );

    /* end of child_b code */
  } else { 
    pthread_t errThread, outThread;
    msg* message = NULL;
    int requestNo = 0;
    fdBundle  errFdB, outFdB;

    /* for monitoring the error stream */
    errFdB.fd = perr[0];
    errFdB.exit = &child_died;

    /* for monitoring the output stream */
    outFdB.fd = pout[0];
    outFdB.exit = &child_died;

    ec_neg1( close(pin[0])  );
    ec_neg1( close(perr[1]) );
    ec_neg1( close(pout[1]) );

    ec_rv( pthread_create(&errThread, NULL, echoErrorsSilently, &errFdB) );

    ec_rv( pthread_create(&outThread, NULL, echoErrorsSilently, &outFdB) );

      
    while(1){
      requestNo++;
      freeMsg(message);
      message = acceptMsg(STDIN_FILENO);
      if(message == NULL){
        fprintf(stderr, "%s readMsg failed", self);
        continue;
      } else {
        char *from, *body;
        int retval = parseActorMsg(message->data, &from, &body);
        if(retval){
          forward(pin[1], body);
        } else {
          fprintf(stderr, "%s did not parse message!\n", self);
        }
      }
    }
    /* end of boss code */
  }
  exit(EXIT_SUCCESS);
  
  EC_CLEANUP_BGN
    exit(EXIT_FAILURE);
  EC_CLEANUP_END
    
}




