#include "cheaders.h"
#include "constants.h"
#include "types.h"
#include "actor.h"
#include "msg.h"
#include "iop_lib.h"
#include "socket_lib.h"
#include "externs.h"
#include "dbugflags.h"
#include "wrapper_lib.h"
#include <sys/types.h>
#include <sys/select.h>
#include "sal_lib.h"


#define MAX_ARGUMENTS 32
static int requestNo = 0;
static char* myname;
static char* sal_exe;
static char * sal_argv[MAX_ARGUMENTS];
static int pin[2], pout[2], perr[2];
static int size = 0;
int SAL_DIED = 0;

static void sal_actor_sigint_handler(int sig){
  char sal_exit[] = "(exit)\n";
  fprintf(stderr,"\n \n SIGINT received \n\n");
  /* if(child > 0){
    write(pin[1], sal_exit, strlen(sal_exit));
  }
  _exit(EXIT_FAILURE);*/
}
/*
  ian says: this is not correct, will put the time of termination
  out of this process' control. Better to set a flag and make sure
  we keep tabs on the flag.
*/
static void sal_actor_sigchild_handler(int sig){
  SAL_DIED = 1;
  /*  kill(getpid(),SIGINT);*/

  /*  fprintf(stderr,"SAL_DIED is (sigchild) %d\n",SAL_DIED);*/
}

static void sal_actor_installHandler(){
  struct sigaction sigactchild;
  struct sigaction sigactint;
  sigactchild.sa_handler = sal_actor_sigchild_handler;
  sigactchild.sa_flags = SA_NOCLDSTOP;
  sigfillset(&sigactchild.sa_mask);
  sigaction(SIGCHLD, &sigactchild, NULL);
  sigactint.sa_handler = sal_actor_sigint_handler;
  sigactint.sa_flags = 0;
  sigfillset(&sigactint.sa_mask);
  sigaction(SIGINT, &sigactint, NULL);
}

int main(int argc, char** argv){
  msg *messageIn = NULL, *messageOut = NULL;
  char *sender, *rest, *body, *cmd;
  int retval;
  if(argv == NULL){
    fprintf(stderr, "didn't understand: (argv == NULL)\n");
    exit(EXIT_FAILURE);
  }
  sal_actor_installHandler();
  myname = argv[0];
  while(1){
    requestNo++;
    if(messageIn  != NULL){ 
      freeMsg(messageIn);
      messageIn = NULL;
    }  
    if(messageOut != NULL){
      freeMsg(messageOut);
      messageOut = NULL;
    }
    if(SAL_ACTOR_DEBUG)
      fprintf(stderr, "%s waiting to process request number %d\n", myname, requestNo);
    messageIn = acceptMsg(STDIN_FILENO);
    if(messageIn == NULL){
      perror("sal_actor: acceptMsg failed");
      continue;
    }
    if(SAL_ACTOR_DEBUG)
      fprintf(stderr, "%s processing request:\n\"%s\"\n", myname, messageIn->data);
    retval = parseActorMsg(messageIn->data, &sender, &body);
    if(!retval){
      fprintf(stderr, "didn't understand: (parseActorMsg)\n\t \"%s\" \n", messageIn->data);
      continue;
    }
    if(getNextToken(body, &cmd, &rest) != 1){
      fprintf(stderr, "didn't understand: (cmd)\n\t \"%s\" \n", body);
      continue;
    }
    if( !strcmp(cmd,"sal-smc") || !strcmp(cmd,"sal-bmc") || !strcmp(cmd,"sal-path-finder") ||
	!strcmp(cmd,"sal-deadlock-checker")){

      sal_exe = cmd;
      sal_argv[size] = cmd;
      size++;
     
      while(getNextToken(rest,&cmd,&rest) == 1){
	if (SAL_ACTOR_DEBUG) fprintf(stderr,"Next token: %s\n",cmd);
	sal_argv[size] = cmd;
	size++;
	if (size > MAX_ARGUMENTS){
	  fprintf(stderr,"Less then %d arguments please\n",size);
	  exit(EXIT_FAILURE);
	}
      }
      sal_argv[size]=NULL;
      if((pipe(pin) != 0) || 
	 (pipe(perr) != 0) ||
	 (pipe(pout) != 0)){
	perror("couldn't make pipes");
	return -1;
      } 
      /*it's time to fork */
      child = fork();
      if(child < 0){
	perror("couldn't fork");
	return -1;
      } else if(child == 0){
	/* i'm destined to be sal */
	if((dup2(pin[0],  STDIN_FILENO) < 0)  ||
	   (dup2(perr[1], STDERR_FILENO) < 0) ||
	   (dup2(pout[1], STDOUT_FILENO) < 0)){
	  perror("couldn't dup fd's");
	  return -1;
      } else if((close(pin[0]) !=  0) ||
                (close(perr[1]) !=  0) ||
                (close(pout[1]) !=  0)){
        perror("couldn't close fd's");
        return -1;
      } else {
	execvp(sal_exe, sal_argv);
        perror("Sal_Actor:couldn't execvp");
        return -1;
      }
    }
      else{ /* I am the boss */
	pthread_t errThread;
	
	if(pthread_create(&errThread, NULL, echoErrors, &perr[0])){
	  fprintf(stderr,"Could not spawn echoErrors thread\n");
	  return -1;
	}
	/*      sleepagain:*/
	if (!SAL_DIED) {
	  fprintf(stderr,"Doing a sleep\n");
	  sleep(5);
	  fprintf(stderr,"sleep returned\n");
	  fprintf(stderr,"\n\n SAL_DIED(main) is %d\n\n \n",SAL_DIED);
	  /*	  if (!SAL_DIED) goto sleepagain;*/
	  }
	while(1){
	  int length;
	   msg *response = NULL;
	   if (SAL_ACTOR_DEBUG)
	   fprintf(stderr,"\nINSIDE WHILE \n");
	   response = readSALMsg(pout[0]);
	   if(response != NULL){
	     length = parseString(response->data, response->bytesUsed);
	     response->bytesUsed = length;
	   writeagain:
	     if(( writeSALMsg(STDERR_FILENO, response) == -1) && (errno == EINTR)) goto writeagain;
	   }
	   if (response == NULL) break;
	}
	sendFormattedMsgFD(STDOUT_FILENO, "system\n%s\nstop %s\n", myname, myname);
      }
    }   
    else {
      fprintf(stderr, "didn't understand: (command)\n\t \"%s\" \n", messageIn->data);
      continue;
    }
  }
}
