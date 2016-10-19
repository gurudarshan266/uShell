#include <iostream>

using namespace std;

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "parse.h"

#ifdef __cplusplus
}
#endif

#define DISP_ERR cout<<strerror(errno)<<endl;
#define IN 0
#define OUT 1
#define ERR 2

#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

void HandleExecutable(Cmd&);
void HandleSetEnv(Cmd&);
void HandleUnsetEnv(Cmd&);
void HandleLogout(Cmd&);
void HandleCd(Cmd&);

void ManageIO(Cmd&);
void ManagePipedCmd(Cmd&,int*,int*);


void prCmd(Cmd c)
{
  int i;

  if ( c ) {
    printf("%s%s ", c->exec == Tamp ? "BG " : "", c->args[0]);
    if ( c->in == Tin )
      printf("<(%s) ", c->infile);
    if ( c->out != Tnil )
      switch ( c->out ) {
      case Tout:
	printf(">(%s) ", c->outfile);
	break;
      case Tapp:
	printf(">>(%s) ", c->outfile);
	break;
      case ToutErr:
	printf(">&(%s) ", c->outfile);
	break;
      case TappErr:
	printf(">>&(%s) ", c->outfile);
	break;
      case Tpipe:
	printf("| ");
	break;
      case TpipeErr:
	printf("|& ");
	break;
      default:
	fprintf(stderr, "Shouldn't get here\n");
	exit(-1);
      }

    if ( c->nargs > 1 ) {
      printf("[");
      for ( i = 1; c->args[i] != NULL; i++ )
	printf("%d:%s,", i, c->args[i]);
      printf("\b]");
    }
    putchar('\n');


    if ( !strcmp(c->args[0], "end") )
      exit(0);
    else if(strcmp(c->args[0],"logout")==0)
		HandleLogout(c);
	else if(strcmp(c->args[0],"setenv")==0)
    	HandleSetEnv(c);
    else if(strcmp(c->args[0],"unsetenv")==0)
		HandleUnsetEnv(c);
    else if(strcmp(c->args[0],"cd")==0)
		HandleCd(c);
    else
    	HandleExecutable(c);
  }
}

void prPipe(Pipe p)
{
  int i = 0;
  Cmd c;

  if ( p == NULL )
    return;

  printf("Begin pipe%s\n", p->type == Pout ? "" : " Error");
  for ( c = p->head; c != NULL; c = c->next ) {
    printf("  Cmd #%d: ", ++i);
    prCmd(c);
  }
  printf("End pipe\n");
  prPipe(p->next);
}

void HandleSetEnv(Cmd& c)
{
	//No arguments so display the list
	if(c->nargs==1)
	{
		char** list = environ;
		for(int i=0; list[i]!=NULL; i++)
		{
			cout<<list[i]<<endl;
		}
	}
	//No value, so set it to ""
	else if(c->nargs==2)
	{
		setenv(c->args[1],"",1);
	}
	else
	{
		setenv(c->args[1],c->args[2],1);
	}

}

void HandleUnsetEnv(Cmd& c)
{
	if(c->nargs < 2)
		cout<<"unsetenv: variable name missing"<<endl;
	else
	{
		unsetenv(c->args[1]);
	}
}

void HandleLogout(Cmd& c)
{
	exit(0);
}

void HandleCd(Cmd& c)
{
	int res;

	if(c->nargs == 1)
	{
		res=chdir(getenv("HOME"));
	}
	else
	{
		res=chdir(c->args[1]);
	}
	if(res<0)
		cout<<strerror(errno)<<endl;
}


void ManageIO(Cmd& c)
{
	int fd[2];

	if(c->in == Tin)//If input from a file
	{
		fd[IN] = open(c->infile,O_RDONLY);
		if(fd[IN] < 0)
		{
			DISP_ERR
			exit(-1);
		}

		dup2(fd[IN], IN);//Make the new fd, the stdin for this process
	}

	if(c->out == Tout || c->out == Tapp || c->out == ToutErr || c->out == TappErr)//If output to a file
	{
		int flags= O_WRONLY | O_CREAT;
		flags |= ( (c->out==Tapp)? O_APPEND : 0 );

		int mode = S_IRUSR | S_IWUSR;

		fd[OUT] = open(c->outfile,flags, mode);

		if(fd[OUT] < 0)
		{
			DISP_ERR
			exit(-1);
		}

		dup2(fd[OUT],OUT);//Make the new fd, the stdout for this process

		if(c->out == ToutErr || c->out == TappErr)
		{
			dup2(fd[OUT],ERR);
		}
	}
}

void ManagePipedCmd(Cmd& c, int* prevPipe, int* nextPipe)
{
	ManageIO(c);

	if(c->in == Tpipe)
	{
		close(prevPipe[OUT]);
		dup2(prevPipe[IN],IN);
	}

	if(c->out == Tpipe || c->out == TpipeErr)
	{

	}

}

/*
 * Look in absolute or relative path for the executable
 * If not found, search in PATH for the executable
 */
void HandleExecutable(Cmd& c)
{
	int retStat=0;
	pid_t cpid = fork();

	// Child
	if(cpid == 0)
	{
		ManageIO(c);

		int res = execvp(c->args[0],c->args);
		cout<<c->args[0]<<": command not found"<<endl;
		exit(0);
	}
	else
	{
		waitpid(cpid, &retStat, 0);
		cout<<"Child process terminated"<<endl;
	}

}

int main(int argc, char *argv[])
{
  Pipe p;
  const char *host = getenv("USER");

  char cwd[1024] = "";

  while ( 1 ) {
	getcwd(cwd, sizeof(cwd));
	printf("%s: "ANSI_COLOR_CYAN"%s "ANSI_COLOR_RESET"%% ", host, cwd);
	p = parse();
	prPipe(p);
	freePipe(p);
  }
}

/*........................ end of main.c ....................................*/
