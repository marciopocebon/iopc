#include "cheaders.h"
#include "constants.h"
#include "msg.h"
#include "actor.h"
#include "dbugflags.h"
#include "sal_lib.h"
#include "ec.h"
#include "iop_lib.h"
#include "wrapper_lib.h"

msg* readSALMsg(fdBundle* fdB){
  fd_set set;
  struct timeval tv;
  int bytes = 0;
  char buff[BUFFSZ];
  msg* retval = makeMsg(BUFFSZ);
  int errcode;
  int fd = fdB->fd;

  if(retval == NULL){
    fprintf(stderr, "makeMsg in %d failed\n", getpid());
    goto fail;
  }
  
  while(1){
  restart: 
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    FD_ZERO(&set); 
    FD_SET(fd, &set); 
    errcode = select(fd + 1, &set, NULL, NULL, &tv);
    if(errcode == -1){
      if(errno == EINTR){ continue; }
      perror("select error:");
      goto fail;
    }
    if(FD_ISSET(fd, &set)){
      if((bytes = read(fd, buff, BUFFSZ)) < 0){
	if(errno == EINTR){ 
	  goto restart; 
	} else { 
	  fprintf(stderr, "read in readSALMsg failed\n");
	  goto fail;
	}
      }
      if(bytes > 0){
	if(addToMsg(retval, bytes, buff) != 0){
	  goto fail;
	}
      } else {
	/* no bytes there      */
	if(*(fdB->exit)){ break; } else { continue; }
      }
    } else {
      /* nothing to read of fd */
      if(*(fdB->exit)){ break; } else { continue; }
    }
  }

  return retval;
  
 fail:
  return NULL;
}

void parseSalThenEcho(int from, int to){
  msg* message;
  int length;
  announce("parseSalThenEcho\t:\tCalling wrapper_readSalMsg\n");
  message = wrapper_readSalMsg(from);
  if(message != NULL){
    announce("parseSalThenEcho\t:\twrapper_readSalMsg returned %d bytes\n", message->bytesUsed);
    length = parseString(message->data, message->bytesUsed);
    message->bytesUsed = length;
    if(sendMsg(to, message) < 0){
      fprintf(stderr, "sendMsg in parseSalThenEcho failed\n");
    } else {
      if(SALWRAPPER_DEBUG)writeMsg(STDERR_FILENO, message);
      announce("\nparseThenEcho wrote %d bytes\n", message->bytesUsed);
    }
    freeMsg(message);
  }
}

msg* wrapper_readSalMsg(int fd){
  char prompt[] = "sal > ";
  char *promptPointer = NULL;
  int bytes = 0, iteration = 0;
  msg* retval = makeMsg(BUFFSZ);
  if(retval == NULL){
    fprintf(stderr, "makeMsg in %d failed\n", getpid());
    goto fail;
  }

  while(1){
    char buff[BUFFSZ];
    announce("wrapper_readSalMsg\t:\tcommencing a read\n");
  restart:
    if((bytes = read(fd, buff, BUFFSZ)) < 0){
      announce("wrapper_readSalMsg\t:\tread error read returned %d bytes\n", bytes);
      if(errno == EINTR){
	announce("readMsg  in %d restarting after being interrupted by a signal\n", getpid());
	goto restart;
      }
      if(errno == EBADF){
	fprintf(stderr, "readMsg  in %d failing because of a bad file descriptor\n", getpid());
	goto fail;
      }
      fprintf(stderr, "Read  in %d returned with nothing\n", getpid());
      return retval;
    } 

    announce("wrapper_readSalMsg\t:\tread read %d bytes\n", bytes);
    announce("Read the following: buff \t = \t %s",buff);

    if(addToMsg(retval, bytes, buff) != 0){
      fprintf(stderr, "addToMsg (wrapper_readSalMsg) in %d failed\n", getpid());
      goto fail;
    }

    iteration++;

    if((promptPointer = strstr(retval->data, prompt)) != NULL){
      fd_set readset;
      struct timeval delay;
      int sret;
      announce("wrapper_readSalMsg\t:\tsaw the prompt, making sure!\n");
      FD_ZERO(&readset);
      FD_SET(fd, &readset);
      delay.tv_sec = 0;
      delay.tv_usec = 0;
      sret = select(fd + 1, &readset, NULL, NULL, &delay);
      if(sret < 0){
	fprintf(stderr, "wrapper_readSalMsg\t:\tselect error\n");
	goto fail;
      } else if(sret == 0){
	announce("wrapper_readSalMsg\t:\tdefinitely the prompt!\n");
	break;
      } else {
	announce("wrapper_readSalMsg\t:\tsret = %d more coming! TOO CONFUSING\n", sret);
	goto fail;
      }
    }

  }/* while */

  if(retval != NULL){
    announce("wrapper_readSalMsg\t:\tretval->bytesUsed = %d\n", retval->bytesUsed);
    announce("wrapper_readSalMsg\t:\tretval->data = \n\"%s\"\n", retval->data);
    announce("==================================================\n");
    promptPointer[0] = '\0';                   /* chomp the prompt I           */
    retval->bytesUsed -= strlen(prompt)    ;   /* chomp the prompt II          */
    announce("wrapper_readSalMsg\t:\tretval->bytesUsed = %d\n", retval->bytesUsed);
    announce("wrapper_readSalMsg\t:\tretval->data = \n\"%s\"\n", retval->data);
    announce("==================================================\n");
    if((retval->bytesUsed == 0) || 
       ((retval->bytesUsed == 1) && (retval->data[0] == '\n'))){
      sprintf(retval->data, "OK\n");
      retval->bytesUsed = strlen("OK\n");
    }
  }
  announce("\nwrapper_readSalMsg\t:\tfinishing a read\n");
  return retval;

 fail:
  freeMsg(retval);
  retval = NULL;
  return retval;
}
