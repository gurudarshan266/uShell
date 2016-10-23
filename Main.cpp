#include <iostream>
#include <vector>
#include <map>
#include "Job.h"
#include <string>
#include <sstream>

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
#include <signal.h>
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

void HandleExecutable(Cmd c,int*,int*,Job*,pid_t&);
void HandleSetEnv(Cmd);
void HandleUnsetEnv(Cmd);
void HandleLogout(Cmd);
void HandleCd(Cmd);

void ManageIO(Cmd);
void SetupPipes(Cmd,int*,int*);
void ManageCmdSeq(Pipe& p);
void DetachTerminal();
void AttachTerminal(pid_t);
void AssignPg(pid_t pid, pid_t& master);
void DumpJobs();

Job* ForegroundJob;
map<int,Job*> BackgroundJobs;
map<int,Job*> SuspendedJobs;
extern std::map<pid_t,Job*> sPid2Job;

pid_t origPgid;

#define clog cerr

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
    else{
//    	HandleExecutable(c, NULL, 0);
    }
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

void HandleSetEnv(Cmd c)
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

void HandleUnsetEnv(Cmd c)
{
	if(c->nargs < 2)
		cout<<"unsetenv: variable name missing"<<endl;
	else
	{
		unsetenv(c->args[1]);
	}
}

void HandleLogout(Cmd c)
{
	exit(0);
}

void HandleCd(Cmd c)
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


void ManageIO(Cmd c)
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

void SetupPipes(Cmd c, int* prevPipe, int* nextPipe)
{
	//Setup pipes
	if(c->in == Tpipe || c->in == TpipeErr)
	{
		close(prevPipe[OUT]);
		dup2(prevPipe[IN],IN);
		close(prevPipe[IN]);
	}

	if(c->out == Tpipe || c->out == TpipeErr)
	{
		close(nextPipe[IN]);
		dup2(nextPipe[OUT],OUT);

		if(c->out == TpipeErr)
			dup2(nextPipe[OUT],ERR);

		close(nextPipe[OUT]);
	}

	//Setup IO redirection
	ManageIO(c);


}

bool IsBgCmd(Pipe p)
{
	Cmd c = p->head;

	while(c!=NULL)
	{
		if(c->exec == Tamp)
			return true;
		c=c->next;
	}

	return false;
}

bool ExecuteBuiltIn(Cmd c)
{
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
    	return false;//Not a builtin

    return true;
}

void ManageCmdSeq(Pipe& p)
{
	int pipeFd[1024][2];

	int index=1;

	pid_t master_pid = 0;

	bool isBackgroundProc = IsBgCmd(p);

	Job* job = new Job( (isBackgroundProc) ? Background : Foreground , p);

	for (Cmd c = p->head; c != NULL; c = c->next )
	{
		if(strcmp(c->args[0],"end")==0 || strcmp(c->args[0],"logout")==0 )
			exit(0);

		if(c->out == Tpipe || c->out == TpipeErr)
			pipe(pipeFd[index]);//Next pipe

		HandleExecutable(c, pipeFd[index-1], pipeFd[index], job, master_pid);

		//Highly essential to send EOF
		if(index>1)
		{
			close(pipeFd[index-1][0]);
			close(pipeFd[index-1][1]);
		}

		index++;
	}//for

//	job->DumpJobStr();

	if(!isBackgroundProc)
	{
		ForegroundJob = job;
		clog << "STDIN currently owned by " << tcgetpgrp(0) << endl;
		AttachTerminal(job->GetPgid());

		int retStat = 0;
		pid_t pp = 1;
		while (pp > 0) {
			//Wait on the PG
			pp = waitpid(-1, &retStat, WUNTRACED);

			if(pp<0) break;

			if(WIFEXITED(retStat)|| WIFSIGNALED(retStat))
			{
				Job* j = sPid2Job[pp];
				j->UpdateProcState(pp, Dead);

				clog<<"EXITED PID = "<<pp<<" JobID = "<<j->GetJobID()<<" status = "<<retStat<<endl;

				State state = j->state;

				if(j->IsTerminated())
				{
					if(state==Background)
					{
						cout<<"Background ";
						cout<<"Job["<<j->GetJobID()<<"]\tTerminated"<<endl;
					}
//					cout<<"Job["<<j->GetJobID()<<"]\tTerminated"<<endl;
					j->state = Terminated;

					if(j==ForegroundJob)
						break;

					if(j!=ForegroundJob && j!=NULL)
						delete j;
				}
			}

			//Even if one process in the group is sent to sleep. Assume that all of them have been sent to sleep
			if (WIFSTOPPED(retStat)) {
				ForegroundJob->state = Stopped;
				SuspendedJobs[ForegroundJob->GetJobID()] = ForegroundJob;
				cout<<"Job["<<ForegroundJob->GetJobID()<<"]\tSuspended"<<endl;
				clog<< " Stopped by signal " << WSTOPSIG(retStat) << endl;
				break;
			}
		}

		//	while(wait(NULL)!=-1);

		ForegroundJob = NULL;

		DetachTerminal();
	}//if(!isBg)
	else
	{
		BackgroundJobs[job->GetJobID()] = job;
	}

}

/*
 * Look in absolute or relative path for the executable
 * If not found, search in PATH for the executable
 */
void HandleExecutable(Cmd c, int* prevPipe,int* nextPipe,Job* job, pid_t& master)
{

	pid_t cpid = fork();

	// Child
	if(cpid == 0)
	{
		SetupPipes(c, prevPipe, nextPipe);

		signal(SIGINT, SIG_DFL);
		signal(SIGTSTP,SIG_DFL);
		signal(SIGTTOU,SIG_DFL);

		AssignPg(getpid(),master);

		if(strcmp(c->args[0],"setenv")==0)
	    	HandleSetEnv(c);
	    else if(strcmp(c->args[0],"unsetenv")==0)
			HandleUnsetEnv(c);
	    else if(strcmp(c->args[0],"where")==0)
	    {
	    	strcpy(c->args[0],"which");
	    	execvp(c->args[0],c->args);
	    }
	    else if(strcmp(c->args[0],"jobs")==0)
	    {
	    	DumpJobs();
	    }
	    else//Run executable
	    {
			int res = execvp(c->args[0],c->args);
			cout<<c->args[0]<<": command not found"<<endl;
			exit(-1);
	    }

		exit(0);
	}
	//parent
	else
	{
		AssignPg(cpid,master);
		job->AddProcess(cpid);
	}

}

bool IsPgAssigned(pid_t pid)
{
	return (pid==getpgid(pid)) || (origPgid!=getpgid(pid));
}

void AssignPg(pid_t pid, pid_t& master)
{
	string prefix = (getpid()==origPgid) ? "shell":"child";

	if(!IsPgAssigned(pid))
	{
		if(master == 0)
		{
			setpgid(pid, 0);
			master = getpgid(pid);
			clog<<"In "<<prefix<<" : Modified Master = "<<master<<" PG= "<<getpgid(pid)<<" assigned to "<<pid<<endl;
		}
		else
		{
			setpgid(pid, master);
			clog<<"In "<<prefix<<" : Unmodified Master = "<<master<<" PG= "<<getpgid(pid)<<" assigned to "<<pid<<endl;
		}
	}
	else
	{
		clog<<"In "<<prefix<<" : correct PG="<<getpgid(pid)<<" already assigned to "<<pid<<endl;
	}
}

void sigint_handler(int signo)
{
  if (signo == SIGINT)
    printf("\nReceived SIGINT\n");

  if(ForegroundJob)
  {
	  cout<<"["<<ForegroundJob->GetJobID()<<"] Terminated"<<endl;
  }
}

void sigtstp_handler(int signo)
{
  if (signo == SIGTSTP)
    printf("\nReceived SIGTSTP\n");

  if(ForegroundJob)
  {
	  cout<<"["<<ForegroundJob->GetJobID()<<"] Stopped"<<endl;

	  ForegroundJob->state = Stopped;

	  SuspendedJobs[ForegroundJob->GetJobID()] = ForegroundJob;
  }
}

void sigttin_handler(int signo)
{
    printf("Ignoring SIGTTIN\n");

}

void RegisterSigHandlers()
{
	if (signal(SIGINT, sigint_handler) == SIG_ERR)
		printf("\ncan't catch SIGINT\n");


	signal(SIGTTOU, SIG_IGN);


	if (signal(SIGTSTP, sigtstp_handler) == SIG_ERR)
			printf("\ncan't catch SIGINT\n");

//	signal(SIGTSTP,SIG_IGN);
}

void DetachTerminal()
{
	tcsetpgrp(STDIN_FILENO, getpgrp());
	clog << "Terminal state restored";
	clog << "\tPGID = " << getpgrp() << endl;
}

void AttachTerminal(pid_t pgid)
{
	tcsetpgrp(STDIN_FILENO, pgid);
	clog<<"Terminal attached to ";
	clog<<"\tPGID = "<<pgid<<endl;
}

void CheckBgJobsStatus()
{
	pid_t pp = 1;
	int retStat;
	while(pp>0)
	{
		pp = waitpid(-1, &retStat, WNOHANG );

		if(pp<=0) break;

		if(WIFEXITED(retStat) || WIFSIGNALED(retStat))
		{
			Job* j = sPid2Job[pp];
			j->UpdateProcState(pp, Dead);

			clog<<"EXITED PID = "<<pp<<" JobID = "<<j->GetJobID()<<" status = "<<retStat<<endl;

			State state = j->state;

			if(j->IsTerminated())
			{
				if(state==Background)
					cout<<"Background ";
				cout<<"Job["<<j->GetJobID()<<"] has Terminated/Completed"<<endl;

				delete j;
			}
		}
	}
}


void DumpJob(Job* j, ostream& os)
{
	os<<"["<<j->GetJobID()<<"]\t("<<j->mCmdStr.str()<<")\t";
	switch(j->state)
	{
		case Background: os<<"Background Running"; break;
		case Foreground: os<<"Foreground Running"; break;
		case Stopped: os<<"Suspended"; break;
		case Terminated: os<<"Terminated";break;
		default:break;
	}
	os<<endl;
}


void DumpJobs()
{
	for (map<int,Job*>::iterator it=BackgroundJobs.begin(); it!=BackgroundJobs.end(); ++it)
	{
		Job* j = it->second;
		if(j && j->state!=Terminated)
		{
			DumpJob(j,cout);
		}
	}

	for (map<int,Job*>::iterator it=SuspendedJobs.begin(); it!=SuspendedJobs.end(); ++it)
	{
		Job* j = it->second;
		if(j)
		{
			DumpJob(j,cout);
		}
	}
}

int main(int argc, char *argv[])
{
  Pipe p;
  const char *host = getenv("USER");

  RegisterSigHandlers();

  char cwd[1024] = "";

  origPgid = getpgrp();

  while ( 1 ) {
	getcwd(cwd, sizeof(cwd));
	CheckBgJobsStatus();
	printf("%s: "ANSI_COLOR_CYAN"%s "ANSI_COLOR_RESET"%% ", host, cwd);
	p = parse();
	while(p != NULL)
	{
		ManageCmdSeq(p);
		p = p->next;
	}
//	prPipe(p);
	freePipe(p);
  }
}

