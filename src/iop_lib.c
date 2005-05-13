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
#include "registry_lib.h"
#include "iop_lib.h"
#include "msg.h"
#include "argv.h"
#include "socket_lib.h"
#include "dbugflags.h"
#include "ec.h"


extern int   iop_debug_flag;
extern int   iop_no_windows_flag;
extern int   iop_hardwired_actors_flag;
extern int   iop_remote_fd;
extern int   iop_server_mode;
extern char *iop_port;
extern char *iop_gui_debug_port;
extern char *iop_jlambda_debug_port;

extern pid_t registry_pid;
extern char* registry_fifo_in;
extern char* registry_fifo_out;
extern int   in2RegPort;
extern int   in2RegFd;
extern pid_t iop_pid;
extern char* iop_bin_dir;

/* statics */
static actor_spec *launchActor(int notify, char* name, char* exe, char** argv);
static actor_spec *launchMaude(int argc, char** argv);
static actor_spec *launchGUI(char* code_dir, char* pid_str, char* port_str);
/* static actor_spec *launchGraphics(char* code_dir); */
static actor_spec *launchGraphics2d(char* code_dir);
static actor_spec *launchExecutor(void);
static actor_spec *launchFilemanager(void);
static actor_spec *launchSocketfactory(void);
static actor_spec *launchRemoteApplet(int remoteFd);
/* static actor_spec *launchPVS(void); */
static char** mkRegistryArgv(int, char**, char*, char*, char*, char*, char*);
static void iop_sigint_handler(int);
static void iop_sigchld_handler(int);
static int iop_installHandler();
static int findListeningPort4In2Reg(int *, int *);
static void chatter();
static void registryDump(FILE*);
static void killActors();
static char* fetchActorName(int);
/* static int fetchRegistrySize(); */
static int waitForRegistry();

static pthread_mutex_t iop_err_mutex = PTHREAD_MUTEX_INITIALIZER;

/* externs used in the announce routine */
extern int   self_debug_flag;
extern char* self;

void announce(const char *format, ...){
  va_list arg;
  va_start(arg, format);
  if(self_debug_flag  && (format != NULL)){
    ec_rv( pthread_mutex_lock(&iop_err_mutex) );
    fprintf(stderr, "%s(%ld)\t:\t", self, (long)pthread_self());
    vfprintf(stderr, format, arg);
    ec_rv( pthread_mutex_unlock(&iop_err_mutex) );
  }
  va_end(arg);
  return;
EC_CLEANUP_BGN
  va_end(arg);
  return;
EC_CLEANUP_END
}

void spawnServer(int argc, char** argv){
  char *server_argv[] = {"iop_appletServer", NULL,  NULL, NULL, NULL};
  server_argv[1] = iop_port;
  server_argv[2] = argv[argc - 2];
  server_argv[3] = argv[argc - 1];
  fprintf(stderr, 
	  "Attempting to spawn iop_appletServer\n\tport      = %s\n\tiop_dir   = %s\n\tmaude_dir = %s\n", 
	  server_argv[1], server_argv[2], server_argv[3]);
  spawnProcess(server_argv[0], server_argv);
}



void iop_init(int argc, char** argv, int optind, int remoteFd){  
  char reg_in[SIZE], reg_out[SIZE], iopPid[SIZE];
  char in2RegPortString[SIZE], in2RegFdString[SIZE];
  char** registry_argv;
  actor_spec *registry_spec;

  announce("commencing\n");
  announce("optind = %d\n", optind);
   
  if((argc - optind) < 1){
    fprintf(stderr, 
	    "Usage: %s  <iop_executable_dir> <maude_executable_dir>\n",
	    argv[0]);
    exit(EXIT_FAILURE);
  }

  iop_bin_dir = argv[argc - 2];

  iop_pid = getpid();


  if(findListeningPort4In2Reg(&in2RegFd , &in2RegPort) != 1){
    fprintf(stderr, "couldn't find an open port!\n");
    exit(EXIT_FAILURE);
  }

  snprintf(in2RegFdString, SIZE, "%d", in2RegFd);
  snprintf(in2RegPortString, SIZE, "%d", in2RegPort);

  announce("in2RegPort = %d\n", in2RegPort);


  announce("figuring out fifo names etc\n");
  snprintf(iopPid, SIZE, "%d", iop_pid);
  snprintf(reg_in,  SIZE, "/tmp/iop_%d_registry_IN", iop_pid);
  snprintf(reg_out, SIZE, "/tmp/iop_%d_registry_OUT", iop_pid);
  registry_fifo_in  = reg_in;
  registry_fifo_out = reg_out;
  announce("setting fifo names etc\n");



  announce("installing signal handler\n");
  if(iop_installHandler() != 0){
    perror("could not install signal handler");
    exit(EXIT_FAILURE);
  }
  announce("installed signal handler\n");
  

  announce("making fifos for the registry\n");
  if(makeRegistryFifos() < 0){
    fprintf(stderr, "makeRegistryFifos() failed, exiting\n");
    exit(EXIT_FAILURE);
  }

  announce("made fifos\n");


  registry_argv = 
    mkRegistryArgv(argc, argv, 
		   registry_fifo_in, registry_fifo_out, 
		   in2RegPortString, in2RegFdString,
		   iop_bin_dir);

  announce("spawning registry\n");
  
  {
    char* exe = registry_argv[0];
    registry_argv[0] = "system";
    registry_spec = launchActor(0, registry_argv[0], exe, registry_argv);
    if(registry_spec == NULL){
      fprintf(stderr, "launchActor(registry_argv) failed, exiting\n");
      exit(EXIT_FAILURE);
    }
    registry_pid = registry_spec->pid;
    /*
      if((registry_pid = spawnProcess(registry_argv[0], registry_argv)) < 0){
      fprintf(stderr, "spawnProcess(registry_argv) failed, exiting\n");
      exit(EXIT_FAILURE);
      }
    */
  }
  

  announce("spawned registry\n");

  announce("closing in2RegFd\n");
  if(close(in2RegFd) != 0){
    fprintf(stderr, "closing in2RegFd failed, exiting\n");
    goto killReg;
  }


  announce("waiting for the registry to be ready\n");
  if(waitForRegistry() != 1){
    fprintf(stderr, "waitForRegistry() failed, exiting\n");
    goto killReg;
  };
  
  announce("notifying registry about itself!\n");
  if(notifyRegistry(registry_spec) < 0){
    goto killReg;
  }
  announce("notified registry about itself!\n");

  announce("spawning GUI\n");
  if(!iop_no_windows_flag){
    if(launchGUI(iop_bin_dir, iopPid, in2RegPortString) == NULL)
      goto bail;
  }
  announce("spawned GUI\n");

  announce("spawning hardwired actors actors\n");


  if(iop_hardwired_actors_flag && launchMaude(argc, argv) == NULL) goto bail;

  /*
    if((remoteFd == 0) && iop_hardwired_actors_flag && launchGraphics(iop_bin_dir) == NULL) 
      goto bail;
  */
  
  if((remoteFd == 0) && iop_hardwired_actors_flag && launchGraphics2d(iop_bin_dir) == NULL) goto bail;
   
  if(iop_hardwired_actors_flag && launchExecutor() == NULL) goto bail;
  
  if(iop_hardwired_actors_flag && launchFilemanager() == NULL) goto bail;

  if(iop_hardwired_actors_flag && launchSocketfactory() == NULL) goto bail;

  if((remoteFd > 0) && (launchRemoteApplet(remoteFd) == NULL)) goto bail;

  /* 
     if(iop_hardwired_actors_flag && launchPVS() == NULL) goto bail;
  */

  announce("spawned actors\n");

 

  if(!iop_no_windows_flag){ 
    if(CHATTER){
      registryDump(stderr);
      chatter();
    }
    while(1){
      int status;
      int retval = waitpid(registry_pid, &status, 0);
      /*      int retval = waitpid(0, &status, 0); */
      if((retval < 0) && (errno == EINTR)) continue;
	announce("registry wait returned;\n");
	announce("wait returned;\n");
	announce("WIFEXITED(status) = %d\n", WIFEXITED(status)); 
	announce("WIFSIGNALED(status) = %d\n", WIFSIGNALED(status)); 
	if(WIFSIGNALED(status))
	  announce("WTERMSIG(status) = %d\n", WTERMSIG(status)); 
	/*      if(retval == registry_pid)break; */
	break;
    }
    exit(EXIT_SUCCESS);
  } else {
    announce("doing a registryDump(stderr);\n");
    registryDump(stderr);
    
    announce("chattering\n");
    chatter();
    announce("no longer chattering\n");
  }

 bail:
  killActors();
  exit(EXIT_FAILURE);

 killReg:
  kill(registry_pid, SIGKILL);
  exit(EXIT_FAILURE);

}


static actor_spec *launchActor(int notify, char* name, char* exe, char** argv){
  actor_spec *retval = NULL;
  announce("spawning %s\n", name);
  if((retval  = newActor(notify, exe, argv)) == NULL){
    fprintf(stderr, "spawning %s failed\n", name);
    return NULL;
  }
  announce("spawned %s\n", name);
  return retval;
}

static actor_spec *launchMaude(int argc, char** argv){
  char  maude_exe[] = "iop_maude_wrapper";
  char* maude_argv[] = {"maude",  NULL, NULL, NULL};
  maude_argv[1] = argv[argc - 1];
  if((argc - optind) >  2){  
    maude_argv[2] = argv[optind];
    if(IOP_DEBUG || iop_debug_flag)
      fprintf(stderr, "(argc - optind) = %d, maude_argv[2] = %s\n",
	      (argc - optind), argv[optind]);
  }
  return launchActor(1, "maude", maude_exe, maude_argv);
}

char* iop_alloc_jarpath(char* code_dir, char* who){
  char *retval = calloc(strlen(code_dir) + strlen(JARPATH) + 1, sizeof(char));
  if(retval == NULL){
    fprintf(stderr, 
	    "calloc failed in iop_alloc_jarpath called by %s: %s\n",
	    who, strerror(errno));
  } else {
    strcpy(retval, code_dir);
    strcat(retval, JARPATH);
  }
  return retval;
}

static actor_spec *launchGUI(char* code_dir, char* pid_str, char* port_str){
  int    input_argc  = 0;
  char   input_exe[] = "java";
  char** input_argv  = NULL;
  /* N is for normal */
  char* input_argvN[] = {INWINDOW, 
			 "-cp", 
			 NULL,
			 "GUI.Editor", NULL, NULL, NULL};
  /* D is for debug  */
  char* input_argvD[] = {INWINDOW, 
			 "-cp", 
			 NULL, 
			 "-Xdebug", 
			 "-Xnoagent",
			 "-Djava.compiler=NONE",
			 NULL,
			 "GUI.Editor", NULL, NULL, NULL};
  if(iop_gui_debug_port == NULL){
    /* normal mode */
    input_argv = input_argvN;
    input_argc = 6;
    input_argv[4] = pid_str;
    input_argv[5] = port_str;
  } else {
    /* debug mode */
    char buff[BUFFSZ];
    input_argv = input_argvD;
    input_argc = 10;
    snprintf(buff, BUFFSZ,
	     "-Xrunjdwp:transport=dt_socket,server=y,suspend=n,address=%s", 
	     iop_gui_debug_port); 
    input_argv[6] = buff;
    input_argv[8] = pid_str;
    input_argv[9] = port_str;
  }
  if((input_argv[2] = iop_alloc_jarpath(code_dir, "launchGUI")) == NULL){
    exit(EXIT_FAILURE);
  }
  if(self_debug_flag)printArgv(stderr, input_argc, input_argv, "input_argv");
  return launchActor(1, "input", input_exe, input_argv);
}

/*
static actor_spec *launchGraphics(char* code_dir){
  char  graphics_exe[] = "iop_graphics_wrapper";
  char* graphics_argv[] = {"graphics", NULL, NULL};
  graphics_argv[1]  = code_dir;
  return launchActor(1, "graphics", graphics_exe, graphics_argv);
}
*/

static actor_spec *launchGraphics2d(char* code_dir){
  char  graphics2d_exe[] = "iop_graphics2d_wrapper";
  char* graphics2d_argv[] = {"graphics2d", NULL, NULL, NULL};
  graphics2d_argv[1]  = code_dir;
  if(iop_jlambda_debug_port != NULL){
    graphics2d_argv[2] = iop_jlambda_debug_port;
  }
  return launchActor(1, "graphics2d", graphics2d_exe, graphics2d_argv);
}


static actor_spec *launchExecutor(){
  char  executor_exe[] = "iop_executor";
  char* executor_argv[] = {"executor", NULL};
  return launchActor(1, "executor", executor_exe, executor_argv);
}

static actor_spec *launchFilemanager(){
    char  filemanager_exe[] = "iop_filemanager";
    char* filemanager_argv[] = {"filemanager", NULL};
    return launchActor(1, "filemanager", filemanager_exe, filemanager_argv);
}

static actor_spec *launchSocketfactory(){
  char  socketfactory_exe[] = "iop_socketfactory";
  char* socketfactory_argv[] = {"socketfactory", NULL, NULL, NULL};
  socketfactory_argv[1] = registry_fifo_in;
  socketfactory_argv[2] = registry_fifo_out;
  return launchActor(1, "socketfactory", socketfactory_exe, socketfactory_argv);
}

static actor_spec *launchRemoteApplet(int remoteFd){
  char  fdString[SIZE];
  char  remoteApplet_exe[] = "iop_remoteApplet";
  char* remoteApplet_argv[] = {"graphics", NULL, NULL, NULL, NULL};
  sprintf(fdString, "%d", remoteFd);
  remoteApplet_argv[1] = fdString;
  remoteApplet_argv[2] = registry_fifo_in;
  remoteApplet_argv[3] = registry_fifo_out;
  return launchActor(1, "remoteApplet", remoteApplet_exe, remoteApplet_argv);
}

/*
static actor_spec *launchPVS(){
  char  pvs_exe[] = "iop_pvs_wrapper";
  char* pvs_argv[] = {"pvs",  NULL};
  return launchActor(1, "pvs", pvs_exe, pvs_argv);
}
*/

#ifdef _LINUX
void parseOptions(int argc, char** argv, char* short_options,  const struct option *long_options){
  int next_option;
  char* caller = argv[0];
  do {
    next_option = getopt_long(argc, argv, short_options, long_options, NULL);
    switch(next_option){
    case 'a': {
      iop_hardwired_actors_flag = 1; 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\thardwired actors option selected\n", caller);
      break;
    }
    case 'd': {
      iop_debug_flag = 1; 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tdebug option selected\n", caller);
      break;
    }
    case 'n': {
      iop_no_windows_flag = 1; 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tno windows option selected\n", caller);
      break;
    }
    case 'r': {
      iop_remote_fd = atoi(optarg); 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tremote fd = %d\n", caller, iop_remote_fd);
      break;
    }
    case 'p': {
      break;
    }
    case 's': {
      iop_server_mode = 1; 
      iop_port = optarg; 
      if(IOP_LIB_DEBUG){
	fprintf(stderr, "%s\t:\tserver mode selected\n", caller);
	fprintf(stderr, "%s\t:\tserver port = %s\n", caller, iop_port);
      }
      break;
    }
    case 'g': {
      iop_gui_debug_port = optarg; 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tgui port = %s\n", caller, iop_gui_debug_port);
      break;
    }
    case 'j': {
      iop_jlambda_debug_port = optarg; 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tjlambda port = %s\n", caller, iop_jlambda_debug_port);
      break;
    }
     case '?': {
      fprintf(stderr, IOP_USAGE); 
      exit(EXIT_SUCCESS);
    }
    case -1 : break;
    default : exit(EXIT_SUCCESS);
    }
  }while(next_option != -1);
}
#elif defined(_MAC)
void parseOptions(int argc, char** argv, const char* options){
  int next_option;
  char* caller = argv[0];
  do {
    next_option = getopt(argc, argv, options);
    switch(next_option){
    case 'a': {
      iop_hardwired_actors_flag = 1; 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\thardwired actors option selected\n", caller);
      break;
    }
    case 'd': {
      iop_debug_flag = 1; 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tdebug option selected\n", caller);
      break;
    }
    case 'n': {
      iop_no_windows_flag = 1; 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tno windows option selected\n", caller);
      break;
    }
    case 'r': {
      iop_remote_fd = atoi(optarg); 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tremote fd = %d\n", caller, iop_remote_fd);
      break;
    }
    case 's': {
      iop_server_mode = 1; 
      iop_port = optarg; 
      if(IOP_LIB_DEBUG){
	fprintf(stderr, "%s\t:\tserver mode selected\n", caller);
	fprintf(stderr, "%s\t:\tserver port = %s\n", caller, iop_port);
      }
      break;
    }
    case 'g': {
      iop_gui_debug_port = atoi(optarg); 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tgui port = %d\n", caller, iop_gui_debug_port);
      break;
    }
    case 'j': {
      iop_jlambda_debug_port = atoi(optarg); 
      if(IOP_LIB_DEBUG)
	fprintf(stderr, "%s\t:\tjlambda port = %d\n", caller, iop_jlambda_debug_port);
      break;
    }
    case '?': {
      fprintf(stderr, IOP_USAGE); 
      exit(EXIT_SUCCESS);
    }
    case -1 : break;
    default : exit(EXIT_SUCCESS);
    }
    argc -= optind;
    argv += optind;
  }while(next_option != -1);
}
#endif

char** mkRegistryArgv(int argc, char** argv, 
		      char* fifoIn, char* fifoOut, 
		      char* port, char* fd,
		      char* dir){
  int i;
  char ** retval = (char **)calloc(argc + 6, sizeof(char *));
  if(retval == NULL){
    fprintf(stderr, "calloc failed in mkRegistryArgv\n");
    exit(EXIT_SUCCESS);
  }
  retval[0] = "iop_registry";
  for(i = 1; i < argc; i++)
    retval[i] = argv[i];
  retval[argc + 0] = fifoIn;
  retval[argc + 1] = fifoOut;
  retval[argc + 2] = port;
  retval[argc + 3] = fd;
  retval[argc + 4] = dir;
  retval[argc + 5] = NULL;
  return retval;
}

void iop_sigint_handler(int sig){
  if(registry_pid > 0){
    if(IOP_DEBUG || iop_debug_flag)
      fprintf(stderr, "sending %d SIGUSR1\n", registry_pid);
    kill(registry_pid, SIGUSR1);
  }
  exit(EXIT_SUCCESS);
}

void iop_sigchld_handler(int sig){
  int status;
  pid_t child = waitpid(-1, &status, WNOHANG);
  announce("waited on child with pid %d with exit status %d\n", 
	    child, status);
  return;
}


int iop_installHandler(){
  struct sigaction sigactInt;
  struct sigaction sigactChld;
  sigactInt.sa_handler = iop_sigint_handler;
  sigactInt.sa_flags = SA_NOCLDSTOP;
  sigfillset(&sigactInt.sa_mask);
  if(sigaction(SIGINT, &sigactInt, NULL) != 0)
    return -1;
  sigactChld.sa_handler = iop_sigchld_handler;
  sigactChld.sa_flags = 0;
  sigfillset(&sigactChld.sa_mask);
  return sigaction(SIGCHLD, &sigactChld, NULL);
}

/* 
   N.B. A Mac OS X bug makes it necessary that C looks for odd ports, while Java
   looks for even ports.

*/
int findListeningPort4In2Reg(int *portFd, int* portNo){
  int p, retval = 0;
  if((portFd == NULL) || (portNo == NULL)) return retval;
  for(p = MINPORT; p < MAXPORT; p += 2){
    if(allocateListeningSocket(p, portFd) != 1)
      continue;
    retval = 1;
    *portNo = p;
    break;
  }
  return retval;
}

pid_t spawnProcess(char* exe, char* cmd[]){
  pid_t retval;
  retval = fork();
  if(retval < 0){
    perror("couldn't fork");
    return -1;
  } else if(retval == 0){
    execvp(exe, cmd);
    perror("couldn't execvp");
    return -1;
  } else {
    return retval;
  }
}

int parseActorMsg(char* buff, char** senderp, char** bodyp){
  int len;
  char *separator;
  if((senderp == NULL) || (bodyp == NULL))
    return -1;
  if(buff == NULL){
    goto fail;
  } else {
    len = strlen(buff);
    if(len < 3) goto fail;
    if(buff[0] != '(') goto fail;
    while((buff[len - 1] == '\n') && (len > 0)){
      len--;
    }
    if(buff[len - 1] != ')') goto fail;
    buff[len - 1] = '\0';
    separator = strchr(buff, ' ');
    if(separator == NULL) goto fail;
    *separator = '\0';
    *senderp = &buff[1];
    *bodyp = separator + 1;
    return 1;
  }

 fail:
  *senderp = NULL;
  *bodyp = NULL;
  return 0;

}

int getNextToken(char *buff, char** next, char** rest){
  int start = 0, end;
  if((next == NULL) || (rest == NULL)) return -1;
  if(buff == NULL) goto exit;
  while(isspace(buff[start])){
    start++;
  }
  if(buff[start] == '\0')  goto exit;
  end = start;
  while(!isspace(buff[end])){
    if(buff[end] != '\0'){ 
      end++; 
    } else {
      break;
    }
  }
  if(buff[end] == '\0'){
    *next = &buff[start];
    *rest = NULL;
    return 1;
  } else {
    buff[end] = '\0';
    *next = &buff[start];
    *rest = &buff[end + 1];
    return 1;
  }
 exit:
  *next = NULL;
  *rest = NULL;
  return 0;
}


static void  intructions(){
  fprintf(stderr, INSTRUCTIONS);
}

static void scursor(){
  fprintf(stderr, "selection> ");
}

static void acursor(char *name){
  fprintf(stderr, "message to %s> ", name);
}

static int isNumber(char* str){
  int i, len = strlen(str);
  if(len == 0) return 0;
  for(i = 0; i < len; i++)
    if(!isdigit(str[i])) return 0;
  return 1;
}

void chatter(){
  char buff[BUFFSZ + 1];
  int bytesin, actorId;
  intructions();
  while(1){
    scursor();
    if((bytesin = read(STDIN_FILENO, buff, BUFFSZ)) <= 0){
      fprintf(stderr, "read from STDIN_FILENO failed (buff 1) bytesin = %d\n", bytesin);
      return;
    }
    buff[bytesin - 1] = '\0';
    if(!isNumber(buff) && strlen(buff) != 1){
      fprintf(stderr, "Huh?\n");
      continue;
    }
    if(isNumber(buff)){
      char *name;
      actorId = atoi(buff);
      if((actorId < 0) ||
	 (actorId >= REGISTRYSZ)){
	fprintf(stderr, "Huh?\n");
	continue;
      } else {
	name = fetchActorName(actorId);
	if(!strcmp(name, UNKNOWNNAME)){
	  fprintf(stderr, "Huh?\n");
	  continue;
	}
	acursor(name);
	if((bytesin = read(STDIN_FILENO, buff, BUFFSZ)) <= 0){ 
	  fprintf(stderr, "read from STDIN_FILENO failed (buff 2) bytesin = %d\n", bytesin);
	  return;
	}
	/* doesn't seem necesssary any more	
	if(buff[bytesin - 1] == '\n'){
	  if(IOP_LIB_DEBUG || iop_debug_flag)
	    fprintf(stderr, "chomping in chatter\n");
	  buff[bytesin - 1] = '\0';
	  bytesin--;
	}
	*/
	if(IOP_LIB_DEBUG || iop_debug_flag)
	  fprintf(stderr, "sending....\"%s\"\n", buff);
	else
	  fprintf(stderr, "sending....\n");
	sendRequest(actorId, bytesin, buff);
      }
    } else {
      announce("switching on %c\n", buff[0]);
      switch(buff[0]){
      case 'q': killActors();  return;
      case 'h': intructions();  continue;
      case 'a': registryDump(stderr); continue;
      default : fprintf(stderr, "Huh?\n"); continue;
      }
    }
  }
}

void registryDump(FILE* targ){
  int reg_wr_fd, reg_rd_fd;
  struct flock wr_lock, rd_lock;
  registry_cmd_t cmd = DUMP;
  
  announce("opening Registry write fifo\n");  
  if((reg_wr_fd = open(registry_fifo_in,  O_RDWR)) < 0) 
    goto fail;
  announce("opened Registry write fifo\n");  

  
  announce("opening Registry read fifo\n");  
  if((reg_rd_fd = open(registry_fifo_out,  O_RDWR)) < 0) 
    goto fail;
  announce("opened Registry read fifo\n");  

  lockFD(&wr_lock, reg_wr_fd, "IOP\tregistryDump:\tregistry write  fifo");

  lockFD(&rd_lock, reg_rd_fd, "IOP\tregistryDump:\tregistry read  fifo");

  announce("writing cmd\n");  
  if(writeInt(reg_wr_fd, cmd) < 0) goto unlock;

  {
    int i, reg_size;
    
    if(readInt(reg_rd_fd, &reg_size) < 0 ) goto unlock;
    
    for(i = 0; i < reg_size; i++){
      int slot;
      char *actor;
      readInt(reg_rd_fd, &slot);
      actor = readline(reg_rd_fd);
      fprintf(targ, "\tregistry[%d] = %s\n", slot, actor);
      free(actor);
    }
  }

 unlock:
  
  unlockFD(&rd_lock, reg_rd_fd, "IOP\tregistryDump: Registry read fifo");

  unlockFD(&wr_lock, reg_wr_fd, "IOP\tregistryDump: Registry write fifo");
  
  if((close(reg_wr_fd) == -1) || (close(reg_rd_fd) == -1)) 
    goto fail;

  return;

 fail:
  
  fprintf(stderr, "failure in registryDump: %s\n", strerror(errno));
  return;
  
}

void killActors(){
  int reg_fd;
  struct flock lock;
  registry_cmd_t cmd = KILL;
  announce("opening Registry fifo\n");  
  if((reg_fd = open(registry_fifo_in,  O_RDWR)) < 0) 
    goto fail;
  announce("opened Registry fifo\n");  
  announce("locking Registry fifo\n");  

  lockFD(&lock, reg_fd, "IOP\tkillActors:\tregistry fifo");

  announce("writing cmd\n");  
  if(writeInt(reg_fd, cmd) < 0) 
    goto unlock;

 unlock:

  unlockFD(&lock, reg_fd, "IOP\tkillActors:\tregistry fifo");

  announce("closing fifo\n");  
  if(close(reg_fd) == -1)
    goto fail;
  return;

 fail:
  
  fprintf(stderr, "failure in killActors: %s\n", strerror(errno));
  return;

}


char* fetchActorName(int index){
  int reg_wr_fd, reg_rd_fd;
  struct flock wr_lock, rd_lock;
  registry_cmd_t cmd = NAME;
  char *retval = (char*)calloc(SIZE + 1, sizeof(char));
  if(retval == NULL) goto fail;
  announce("opening Registry write fifo\n");  
  if((reg_wr_fd = open(registry_fifo_in,  O_RDWR)) < 0) 
    goto fail;
  announce("opened Registry write fifo\n");  
  
  
  announce("opening Registry read fifo\n");  
  if((reg_rd_fd = open(registry_fifo_out,  O_RDWR)) < 0) 
    goto fail;
  announce("opened Registry read fifo\n");  


  lockFD(&wr_lock, reg_wr_fd, "fetchActorName: Registry write  fifo");

  lockFD(&rd_lock, reg_rd_fd, "fetchActorName: Registry read  fifo");

  announce("writing cmd\n");  
  if(writeInt(reg_wr_fd, cmd) < 0) goto unlock;

  announce("writing index\n");  
  if(writeInt(reg_wr_fd, index) < 0) goto unlock;

  {
    int bytes;
    
    if((bytes = read(reg_rd_fd, retval, SIZE)) <=  0)
      goto fail;
    
    retval[bytes] = '\0';
  }

 unlock:
  
  unlockFD(&rd_lock, reg_rd_fd, "fetchActorName: Registry read fifo");
  
  unlockFD(&wr_lock, reg_wr_fd, "fetchActorName: Registry write fifo");
  
  if((close(reg_wr_fd) == -1) || (close(reg_rd_fd) == -1)) 
    goto fail;

  return retval;

 fail:
  
  free(retval);
  fprintf(stderr, "failure in fetchActorName: %s\n", strerror(errno));
  return NULL;
  
}

/*
int fetchRegistrySize(){
  int reg_wr_fd, reg_rd_fd, rsize = -1;
  struct flock wr_lock, rd_lock;
  registry_cmd_t cmd = RSIZE;
  if(IOP_LIB_DEBUG || iop_debug_flag)
    fprintf(stderr, "opening Registry write fifo\n");  
  if((reg_wr_fd = open(registry_fifo_in,  O_RDWR)) < 0) 
    goto fail;
  if(IOP_LIB_DEBUG || iop_debug_flag)
    fprintf(stderr, "opened Registry write fifo\n");  
  
  
  if(IOP_LIB_DEBUG || iop_debug_flag)
    fprintf(stderr, "opening Registry read fifo\n");  
  if((reg_rd_fd = open(registry_fifo_out,  O_RDWR)) < 0) 
    goto fail;
  if(IOP_LIB_DEBUG || iop_debug_flag)
    fprintf(stderr, "opened Registry read fifo\n");  


  lockFD(&wr_lock, reg_wr_fd, "Registry write  fifo");

  lockFD(&rd_lock, reg_rd_fd, "Registry read  fifo");

  if(IOP_LIB_DEBUG || iop_debug_flag)
    fprintf(stderr, "writing cmd\n");  
  if(writeInt(reg_wr_fd, cmd) < 0) goto unlock;

  if(readInt(reg_rd_fd, &rsize) < 0)  goto unlock;
    
 unlock:
  
  unlockFD(&rd_lock, reg_rd_fd, "Registry read fifo");
  
  unlockFD(&wr_lock, reg_wr_fd, "Registry write fifo");
  
  if((close(reg_wr_fd) == -1) || (close(reg_rd_fd) == -1)) 
    goto fail;

  return rsize;
  
 fail:
  
  fprintf(stderr, "failure in fetchRegistrySize: %s\n", strerror(errno));
  return -1;
  
}
*/

int waitForRegistry(void){
  int  reg_rd_fd;
  struct flock rd_lock;
  char buff[SIZE];

  announce("opening Registry read fifo\n");  
  if((reg_rd_fd = open(registry_fifo_out,  O_RDWR)) < 0) 
    goto fail;
  announce("opened Registry read fifo\n");  


  lockFD(&rd_lock, reg_rd_fd, "waitForRegistry: Registry read  fifo");


  {
    int bytes;
    
    if((bytes = read(reg_rd_fd, buff, SIZE)) <=  0)
      goto fail;
    
    buff[bytes] = '\0';
  }

  unlockFD(&rd_lock, reg_rd_fd, "waitForRegistry: Registry read fifo");
  
  
  if(close(reg_rd_fd) == -1) 
    goto fail;

  return !strcmp(buff, REGREADY);

 fail:
  
  fprintf(stderr, "failure in waitForRegistry: %s\n", strerror(errno));
  return 0;
  
}
