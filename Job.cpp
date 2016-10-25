/*
 * Job.cpp
 *
 *  Created on: Oct 21, 2016
 *      Author: guru
 */

#include "Job.h"
#include <vector>
#include<iostream>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <map>

using namespace std;

int Job::num_jobs = 1;

map<pid_t,Job*> sPid2Job;

extern map<int,Job*> BackgroundJobs;
extern map<int,Job*> SuspendedJobs;

Job::Job(State s, Pipe p)
{
	mJobId = Job::num_jobs++;
	mPgid = -1;
	state = s;

	if(p)
		ConstructCmdStr(p);
}

void Job::ConstructCmdStr(Pipe p)
{
	for(Cmd c= p->head; c ; c=c->next)
	{
		for(int i=0;i<c->nargs;i++)
			mCmdStr<<" "<<c->args[i]<<" ";

		if(c->out==Tpipe)
			mCmdStr<<"|";
		else if(c->out==TpipeErr)
			mCmdStr<<"|&";
	}


}

void Job::DumpJobStr()
{
	cout<<"["<<mJobId<<"]\t";
	for(int i=0; i<mProcesses.size();i++)
	{
		cout<<mProcesses[i]<<" ";
	}

	if(mProcesses.size()>0)
		cout<<"\tPGID="<<getpgid(mProcesses[0]);
	if(state == Background)
		cout<<" &";
	cout<<endl;
}

void Job::AddProcess(pid_t p)
{

	//Make the first process as the process group leader
/*	if(mProcesses.empty())
	{
		mPgid = p;
		setpgid(p, 0);//PGID = PID
	}
	//Make all processes to have same pgid
	else
		setpgid(p, mPgid);*/

	mProcesses.push_back(p);
	mProcessesState.push_back(Running);
	sPid2Job[p] = this;
}

void Job::SetPgid(pid_t p)
{
	if(setpgid(mProcesses[0],0)<0)
		cout<<"Unable to set PGID of PID "<<mProcesses[0]<<" errno = "<<errno<<"\nMSG = "<<strerror(errno)<<endl;

	for(int i=1; i<mProcesses.size(); i++)
	{
		setpgid(mProcesses[i],p);
	}
	mPgid = p;
	cout<<"Setting Job["<<mJobId<<"] PGID to "<<getpgid(mProcesses[0])<<endl;
}

void Job::SetPgid2Master()
{
	SetPgid(mProcesses[0]);
}


void Job::SendSignal(int sig_no)
{
	kill(-mPgid, sig_no);

	if(sig_no == SIGINT)
	{
		cout<<"["<<mJobId<<"]\tTerminated"<<endl;
	}

	if(sig_no == SIGTSTP)
	{
		cout<<"["<<mJobId<<"]\tStopped"<<endl;
	}
}

bool Job::UpdateProcState(pid_t pid, ProcState state)
{
	for(int i=0; i<mProcesses.size();i++)
	{
		if(pid == mProcesses[i])
		{
			mProcessesState[i] = state;
			return true;
		}
	}

	return false;
}

bool Job::IsTerminated()
{
	for(int i=0;i<mProcessesState.size();i++)
	{
		if(mProcessesState[i]!=Dead)
		{
			return false;
		}
	}

	state = Terminated;
	return true;
}

bool Job::IsSuspended()
{
	for(int i=0;i<mProcessesState.size();i++)
	{
		if(!(mProcessesState[i]==Sleeping || mProcessesState[i]==Dead))
		{
			return false;
		}
	}

	state = Stopped;
	return true;
}

Job::~Job()
{
	map<pid_t,Job*>::iterator it;

	if(!IsTerminated())
		clog<<"Job deletion called without terminating all the processess"<<endl;

	for(int i=0;i<mProcesses.size();i++)
	{
		sPid2Job.erase(mProcesses[i]);
	}

	if(state==Background && BackgroundJobs.find(mJobId)!=BackgroundJobs.end())
	{
		BackgroundJobs.erase(mJobId);
	}



//	if(mJobId == num_jobs-1) num_jobs--;
}
