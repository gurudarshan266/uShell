#include <iostream>
#include <vector>
#include <map>
#include "Job.h"
#include <string>
#include <sstream>
#include <fstream>

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
#include <sys/resource.h>
#include "parse.h"

#ifdef __cplusplus
}
#endif

#define DISP_ERR cout<<"errno = "<<errno<<" "<<strerror(errno)<<endl;
#define IN 0
#define OUT 1
#define ERR 2

#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

ofstream logfile;

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
void DumpJob(Job* j, ostream& os);
Job* BringToFg(int jobId);
Job* SendToBg(int jobId);
Job* Kill(int jobId);

Job* ForegroundJob;
map<int,Job*> BackgroundJobs;
map<int,Job*> SuspendedJobs;
extern std::map<pid_t,Job*> sPid2Job;
vector<char*> Builtins;
vector<char*> ExtendedBuiltins;

pid_t origPgid;

#define clog logfile

bool IsBuiltin(char* str, bool extended=false)
{
	for(int i=0;i < Builtins.size(); i++)
	{
		if(!strcmp(Builtins[i],str))
			return true;
	}

	if(extended)
	{
		for(int i=0;i < ExtendedBuiltins.size(); i++)
		{
			if(!strcmp(ExtendedBuiltins[i],str))
				return true;
		}
	}

	return false;
}

bool IsNumber(char* c)
{
	int x;
	return (sscanf(c,"%d",&x)>0);
}

bool IsSubshellReq(Cmd c)
{
	if(!strcmp("nice",c->args[0]))
	{
		switch(c->nargs)
		{
			case 1:
				return false;
				break;
			case 2:
				if(IsNumber(c->args[1])) return false;
				else return true;
				break;

			default:
				return true;
		}
	}
	return !(IsBuiltin(c->args[0]) && (c->next==NULL));
}

void DisplaceArgs(Cmd c, int dispacement)
{
	c->nargs -= dispacement;
	for(int i=0;i<c->nargs;i++)
	{
		strcpy(c->args[i],c->args[i+dispacement]);
	}
	c->args[c->nargs]=NULL;
}

void HandleNice(Cmd c)
{
	int priority = 4;

	if(getpgrp()==origPgid)
	{
		if(c->nargs>2)
		{
			clog<<"More arguments in nice"<<endl;
			return;
		}

		if(c->nargs == 2)
			sscanf(c->args[1],"%d",&priority);
		setpriority(PRIO_PROCESS,0, priority);
		clog<<"Priority of "<<getpid()<<" set to "<<getpriority(PRIO_PROCESS,0)<<endl;
	}
	else
	{
		int start_pos = 1;
		if(c->nargs<2)
		{
			clog<<"Less arguments in nice"<<endl;
			return;
		}
		if(IsNumber(c->args[1]))
		{
			sscanf(c->args[1],"%d",&priority);
			start_pos = 2;
		}

		DisplaceArgs(c, start_pos);

		setpriority(PRIO_PROCESS,0, priority);
		clog<<"Priority of "<<getpid()<<" set to "<<getpriority(PRIO_PROCESS,0)<<endl;
	}

}

bool IsJobCtrlCmd(Cmd c)
{
	if( !strcmp(c->args[0],"fg") ||
		!strcmp(c->args[0],"bg") ||
		!strcmp(c->args[0],"jobs"))
		return true;

	return false;
}

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

void HandlePwd(Cmd c)
{
	char buff[1024]="";
	memset(buff,0,sizeof(buff));
	getcwd(buff, sizeof(buff));
	cout<<buff<<endl;
}

void HandleEcho(Cmd c)
{
	char *buff = (char*)calloc(1,1024);

	for(int i = 1; i<c->nargs;i++)
	{
//		memcpy(buff + strlen,c->args[0],strlen(c->args[0]));
		sprintf(buff,"%s",c->args[i]);
		if(i!=c->nargs-1)
			sprintf(buff," ");
	}
//	cout<<endl;
	printf("%s\n",buff);
	free(buff);
}


int stdin_cpy = 0;
int stdout_cpy = 1;
int stderr_cpy = 2;
void ManageIO(Cmd c)
{
	int fd[2];

	if(c->in == Tin && !(getpgrp()==origPgid))//If input from a file
	{
		fd[IN] = open(c->infile,O_RDONLY);
		if(fd[IN] < 0)
		{
			DISP_ERR
			exit(-1);
		}

		if(getpgrp()==origPgid)
			stdin_cpy = dup(STDIN_FILENO);

		dup2(fd[IN], IN);//Make the new fd, the stdin for this process
	}

	if(c->out == Tout || c->out == Tapp || c->out == ToutErr || c->out == TappErr)//If output to a file
	{
		int flags= O_WRONLY | O_CREAT;
		flags |= ( (c->out==Tapp || c->out==TappErr)? O_APPEND : O_TRUNC );

		int mode = S_IRUSR | S_IWUSR;

		fd[OUT] = open(c->outfile,flags, mode);

		if(fd[OUT] < 0)
		{
			DISP_ERR
			exit(-1);
		}

		if(getpgrp()==origPgid)
			stdout_cpy = dup(OUT);

		dup2(fd[OUT],OUT);//Make the new fd, the stdout for this process

		if(c->out == ToutErr || c->out == TappErr)
		{
			if(getpgrp()==origPgid)
				stderr_cpy = dup(ERR);

			dup2(fd[OUT],ERR);
		}
	}
}

void RestoreIO(Cmd c)
{
	if(c->out == Tout || c->out == Tapp)//If output to a file
	{
		close(OUT);
		dup2(stdout_cpy,OUT);
	}

	if(c->out == ToutErr || c->out == TappErr)
	{
		close(OUT);
		dup2(stdout_cpy,OUT);
		close(ERR);
		dup2(stderr_cpy,ERR);
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
		fflush(stdout);
		fflush(stderr);
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
	bool isJobCtrlCmd = false;
	Job* job;

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

    else if(strcmp(c->args[0],"jobs")==0)
    {
    	DumpJobs();
    }
    else if(!strcmp("nice",c->args[0]))
    	HandleNice(c);
    else if(strcmp(c->args[0],"pwd")==0)
    	HandlePwd(c);
    else if(!strcmp(c->args[0],"echo"))
    	HandleEcho(c);

	//For "fg" command
/*	if(strcmp(c->args[0],"fg")==0)
	{
		isJobCtrlCmd = true;
		int jobId;
		sscanf(p->head->args[1],"%%%d",&jobId);
		job = BringToFg(jobId);
//		if(!job) return;
	}
	//For "bg" command
	else if(strcmp(c->args[0],"bg")==0)
	{
		isJobCtrlCmd = true;
		int jobId;
		sscanf(p->head->args[1],"%%%d",&jobId);
		job = SendToBg(jobId);
//		return;
	}
	//For "kill" command
	else if(strcmp(c->args[0],"kill")==0)
	{
		isJobCtrlCmd = true;
		int jobId;
		sscanf(p->head->args[1],"%%%d",&jobId);
		job = Kill(jobId);
//		if(!job) return;
	}*/

    else
    	return false;//Not a builtin

    return true;
}

void WaitOnFg()
{
	pid_t pp = 1;
	int retStat=0;

	while (pp > 0)
	{
		pp = waitpid(-1, &retStat, WUNTRACED);

		if(pp<0) break;

		Job* j = sPid2Job[pp];
		if(WIFEXITED(retStat)|| WIFSIGNALED(retStat))
		{
			j->UpdateProcState(pp, Dead);

			clog<<"EXITED PID = "<<pp<<" JobID = "<<j->GetJobID()<<" status = "<<retStat<<" signaled = "<<WIFSIGNALED(retStat)<<endl;

			State state = j->state;

			if(j->IsTerminated())
			{
				if(state==Background)
				{
					cerr<<"Background ";
					cerr<<"Job["<<j->GetJobID()<<"]\tTerminated"<<endl;
				}
				else if(WIFSIGNALED(retStat))
				{
					cerr<<"Job["<<j->GetJobID()<<"]\tTerminated"<<endl;
				}
//					cout<<"Job["<<j->GetJobID()<<"]\tTerminated"<<endl;
				j->state = Terminated;

				if(j==ForegroundJob)
				{
					delete j;
					break;
				}

				if(j!=ForegroundJob && j!=NULL)
					delete j;
			}
		}

		//Even if one process in the group is sent to sleep. Assume that all of them have been sent to sleep
		else if (WIFSTOPPED(retStat))
		{
			j->UpdateProcState(pp, Sleeping);
			if(ForegroundJob->IsSuspended())
			{
				ForegroundJob->state = Stopped;
				SuspendedJobs[ForegroundJob->GetJobID()] = ForegroundJob;
				cout<<"Job["<<ForegroundJob->GetJobID()<<"]\tSuspended"<<endl;
				break;
			}
			clog<< " Stopped by signal " << WSTOPSIG(retStat) << endl;
		}
	}
}



void ManageCmdSeq(Pipe& p)
{
	int pipeFd[1024][2];

	int index=1;

	pid_t master_pid = 0;

	bool isBackgroundProc = IsBgCmd(p);

	bool isJobCtrlCmd = false;

	Job* job;

	//For "fg" command
	if(strcmp(p->head->args[0],"fg")==0)
	{
		isJobCtrlCmd = true;
		int jobId;
		sscanf(p->head->args[1],"%%%d",&jobId);
		job = BringToFg(jobId);
		if(!job) return;
	}
	//For "bg" command
	else if(strcmp(p->head->args[0],"bg")==0)
	{
		isJobCtrlCmd = true;
		int jobId;
		sscanf(p->head->args[1],"%%%d",&jobId);
		job = SendToBg(jobId);
		return;
	}
	//For "kill" command
	else if(strcmp(p->head->args[0],"kill")==0)
	{
		isJobCtrlCmd = true;
		int jobId;
		sscanf(p->head->args[1],"%%%d",&jobId);
		job = Kill(jobId);
		if(!job) return;
	}
	else if(!IsSubshellReq(p->head))
	{
		clog<<"Executing builtin commands in place"<<endl;
		ManageIO(p->head);
		ExecuteBuiltIn(p->head);
		RestoreIO(p->head);
		return;
	}
	else
	{
		job = new Job( (isBackgroundProc) ? Background : Foreground , p);
	}


	for (Cmd c = p->head; c != NULL && !isJobCtrlCmd; c = c->next )
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


	if(job->state==Background)
		DumpJob(job,cout);
	else
		DumpJob(job,clog);

	if(!isBackgroundProc)
	{
		ForegroundJob = job;
		clog << "STDIN currently owned by " << tcgetpgrp(0) << endl;
		AttachTerminal(job->GetPgid());

		int retStat = 0;
		pid_t pp = 1;

		WaitOnFg();

		//	while(wait(NULL)!=-1);

		ForegroundJob = NULL;

		DetachTerminal();
	}//if(!isBg)
	else
	{
		BackgroundJobs[job->GetJobID()] = job;
	}

}

Job* BringToFg(int jobId)
{
	if(getpgrp()!=origPgid)
	{
		cout<<"No job control in subshells"<<endl;
		exit(0);
	}

	//If it's a Background process
	if(BackgroundJobs.find(jobId) != BackgroundJobs.end())
	{
		Job* j = BackgroundJobs[jobId];
		BackgroundJobs.erase(jobId);
		clog<<"Job found in BackgroundJobs"<<endl;
		//Race condition during clean up. Avoiding terminated cases which aren't yet cleaned up
		if(j->state == Background)
		{
			j->state = Foreground;
			ForegroundJob = j;
			return ForegroundJob;
		}

	}

	//If it's a Suspended process
	else if(SuspendedJobs.find(jobId) != SuspendedJobs.end())
	{
		Job* j = SuspendedJobs[jobId];
		SuspendedJobs.erase(jobId);

		//Race condition during clean up. Avoiding terminated cases which aren't yet cleaned up
		if(j->state == Stopped)
		{
			kill(-j->GetPgid(),SIGCONT);
			clog<<"Job found in SuspendedJobs"<<endl;
			j->state = Foreground;
			ForegroundJob = j;
			return ForegroundJob;
		}

	}

	return NULL;

}

Job* SendToBg(int jobId)
{
	if(getpgrp()!=origPgid)
	{
		cout<<"No job control in subshells"<<endl;
		exit(0);
	}

	//If it's a Suspended process
	if(SuspendedJobs.find(jobId) != SuspendedJobs.end())
	{
		Job* j = SuspendedJobs[jobId];
		SuspendedJobs.erase(jobId);

		//Race condition during clean up. Avoiding terminated cases which aren't yet cleaned up
		if(j->state == Stopped)
		{
			kill(-j->GetPgid(),SIGCONT);
			clog<<"Job found in SuspendedJobs"<<endl;
			j->state = Background;
			BackgroundJobs[j->GetJobID()]=j;
			return j;
		}
	}

	return NULL;
}


Job* Kill(int jobId)
{
	//If it's a Background process
	if(BackgroundJobs.find(jobId) != BackgroundJobs.end())
	{
		Job* j = BackgroundJobs[jobId];
		BackgroundJobs.erase(jobId);
		clog<<"Job found in BackgroundJobs"<<endl;
		//Race condition during clean up. Avoiding terminated cases which aren't yet cleaned up
		if(j->state == Background)
		{
			kill(-j->GetPgid(),SIGKILL);
			j->state = Foreground;
			ForegroundJob = j;
			return ForegroundJob;
		}

	}

	//If it's a Suspended process
	else if(SuspendedJobs.find(jobId) != SuspendedJobs.end())
	{
		Job* j = SuspendedJobs[jobId];
		SuspendedJobs.erase(jobId);

		//Race condition during clean up. Avoiding terminated cases which aren't yet cleaned up
		if(j->state == Stopped)
		{
			kill(-j->GetPgid(),SIGKILL);
			clog<<"Job found in SuspendedJobs"<<endl;
			j->state = Foreground;
			ForegroundJob = j;
			return ForegroundJob;
		}

	}

	return NULL;

}

/*
 * Look in absolute or relative path for the executable
 * If not found, search in PATH for the executable
 */
void HandleExecutable(Cmd c, int* prevPipe,int* nextPipe,Job* job, pid_t& master)
{

	pid_t cpid = 0;

	if(!IsSubshellReq(c))
	{
		ManageIO(c);
		ExecuteBuiltIn(c);
		RestoreIO(c);
		return;
	}

	cpid= fork();

	// Child
	if(cpid == 0)
	{
		SetupPipes(c, prevPipe, nextPipe);

		signal(SIGINT, SIG_DFL);
		signal(SIGTSTP,SIG_DFL);
		signal(SIGTTOU,SIG_DFL);
		signal(SIGQUIT,SIG_DFL);

		AssignPg(getpid(),master);

		if(!strcmp("nice",c->args[0]))
		{
			HandleNice(c);
		}

		if(!strcmp(c->args[0],"where"))
		{
			if(c->nargs < 2 )
			{
				cerr<<"Missing arguments"<<endl;
				exit(0);
			}
			if(IsBuiltin(c->args[1],true))
			{
				cout<<c->args[1]<<": shell built-in command"<<endl;
			}
			else
			{
				strcpy(c->args[0],"which");
				execvp(c->args[0],c->args);
				exit(0);
			}
		}
		else if(IsBuiltin(c->args[0]))
		{
			ExecuteBuiltIn(c);
			clog<<"Executing built-in in sub-shell"<<endl;
		}
	    else//Run executable
	    {
			int res = execvp(c->args[0],c->args);
//			DISP_ERR;
			if(errno==EACCES)
				cerr<<"permission denied"<<endl;
			else if(errno==ENOENT)
				cerr<<c->args[0]<<": command not found"<<endl;
			kill(-getpgrp(),SIGKILL);
			exit(0);
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

  return;
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

	if (signal(SIGTSTP, sigtstp_handler) == SIG_ERR)
			printf("\ncan't catch SIGINT\n");

	signal(SIGTTOU, SIG_IGN);
	signal(SIGTSTP,SIG_IGN);
	signal(SIGINT,SIG_IGN);
	signal(SIGQUIT,SIG_IGN);
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
	os<<"["<<j->GetJobID()<<"] ";

	for(int i=0; i<j->mProcesses.size();i++)
	{
		os<<j->mProcesses[i]<<" ";
	}

	os<<"\t("<<j->mCmdStr.str()<<")\t";
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
		if(j)
			if( j->state!=Terminated && j->state!=Foreground)
			{
				DumpJob(j,cout);
			}
	}

	for (map<int,Job*>::iterator it=SuspendedJobs.begin(); it!=SuspendedJobs.end(); ++it)
	{
		Job* j = it->second;
		if(j)
			if(j->state!=Terminated && j->state!=Foreground)
			{
				DumpJob(j,cout);
			}
	}
}

int ReadCmdFile(const char* filename)
{
	clog<<"Reading file "<<filename<<endl;

	int fd = open(filename,O_RDONLY);

	if(fd<0) {
		DISP_ERR;
		return -1;

	}

	int stdin_cp = dup(STDIN_FILENO);

	dup2(fd, STDIN_FILENO);
	close(fd);

	Pipe p;

	int flag = 0;

	clog<<"Reading cmds"<<endl;
	while (!feof(stdin))
	{
		CheckBgJobsStatus();
		p = parse();
		while (p != NULL)
		{
			//When end of file is detected
			if(strcmp(p->head->args[0],"end")==0)
			{
				flag=1;
				break;
			}
			ManageCmdSeq(p);
			p = p->next;
		}
		freePipe( p);
		if(flag==1) break;
	}

	clog<<"Completed reading the file"<<endl;

	close(STDIN_FILENO);
	dup(stdin_cp);

	return 0;
}

void InitializeBuiltinList()
{
	Builtins.push_back("bg");
	Builtins.push_back("fg");
	Builtins.push_back("jobs");
	Builtins.push_back("setenv");
	Builtins.push_back("unsetenv");
	Builtins.push_back("nice");
	Builtins.push_back("cd");
	Builtins.push_back("logout");
	Builtins.push_back("kill");
	Builtins.push_back("pwd");
	Builtins.push_back("echo");

//	ExtendedBuiltins.push_back("echo");
//	ExtendedBuiltins.push_back("pwd");
	ExtendedBuiltins.push_back("where");

}

int main(int argc, char *argv[])
{
  logfile.open("err.txt");


  Pipe p;
  const char *host = getenv("USER");

  RegisterSigHandlers();
  InitializeBuiltinList();

  char cwd[1024] = "";

  origPgid = getpgrp();

  char filename[1024];
  strcat(filename,getenv("HOME"));
  strcat(filename,"/.ushrc");

//  ReadCmdFile(filename.str().c_str());
  ReadCmdFile("/home/guru/.ushrc");

  while ( 1 ) {
	getcwd(cwd, sizeof(cwd));
	CheckBgJobsStatus();
//	printf("%s: "ANSI_COLOR_CYAN"%s "ANSI_COLOR_RESET"%% ", host, cwd);
	printf("%s%% ",host);
	fflush( stdout );
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

