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
#include <unistd.h>
#include "parse.h"

#ifdef __cplusplus
}
#endif

void HandleExecutable(Cmd&);
void HandleSetEnv(Cmd&);
void HandleUnsetEnv(Cmd&);

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
    else if(strcmp(c->args[0],"setenv")==0)
    	HandleSetEnv(c);
    else if(strcmp(c->args[0],"unsetenv")==0)
		HandleUnsetEnv(c);
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
		int res = unsetenv(c->args[1]);
	}
}

/*
 * Look in absolute or relative path for the executable
 * If not found, search in PATH for the executable
 */
void HandleExecutable(Cmd& c)
{
	int retStat;
	pid_t cpid = fork();

	// Child
	if(cpid == 0)
	{
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

  while ( 1 ) {
    printf("%s%% ", host);
    p = parse();
    prPipe(p);
    freePipe(p);
  }
}

/*........................ end of main.c ....................................*/
