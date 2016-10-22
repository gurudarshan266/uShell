/*
 * Job.h
 *
 *  Created on: Oct 21, 2016
 *      Author: guru
 */

#ifndef JOB_H_
#define JOB_H_

#include <vector>
#include <stdlib.h>
#include <unistd.h>

typedef enum { Foreground, Background, Stopped, Terminated } State;

class Job {
public:
	Job(State s);
	Job(char*);
	virtual ~Job();

	void AddProcess(pid_t p);
	std::vector<pid_t>& GetProcesses(){ return mProcesses; };

	/* Send the signal to all the processes in the job */
	int GetJobID() { return mJobId; };
	void SendSignal(int sig_no);
	void DumpJobStr();
	pid_t GetPgid() { return getpgid(mProcesses[0]); };
	void SetPgid(pid_t p);
	void SetPgid2Master();

	State state;

private:
	std::vector<pid_t> mProcesses;
	int mJobId;
	char mCommandStr[1024];
	pid_t mPgid;

	static int num_jobs;
};

#endif /* JOB2_H_ */
