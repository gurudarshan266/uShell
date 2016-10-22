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

using namespace std;

int Job::num_jobs = 1;

Job::Job(State s):state(s)
{
	mJobId = Job::num_jobs++;
	mPgid = -1;
}

Job::Job(char* commandStr)
{
	mJobId = Job::num_jobs++;
	strcpy(mCommandStr,commandStr);
	mPgid = -1;
}

void Job::DumpJobStr()
{
	cout<<"["<<mJobId<<"]\t";
	for(int i=0; i<mProcesses.size();i++)
	{
		cout<<mProcesses[i]<<" ";
	}

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

Job::~Job()
{
}
