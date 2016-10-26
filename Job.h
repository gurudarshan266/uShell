/*
 * Job.h
 *
 *  Created on: Oct 21, 2016
 *      Author: guru
 */

#ifndef JOB_H_
#define JOB_H_

#include <vector>
#include <map>
#include <stdlib.h>
#include <unistd.h>
#include <sstream>

#ifdef __cplusplus
extern "C" {
#endif

#include "parse.h"

#ifdef __cplusplus
}
#endif

typedef enum { Foreground, Background, Stopped, Terminated } State;
typedef enum { Running, Sleeping, Dead} ProcState;

class Job {
public:
	Job(State s,Pipe p=NULL);
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

	bool UpdateProcState(pid_t pid, ProcState state);
	bool IsTerminated();
	bool IsSuspended();

	State state;
	std::ostringstream mCmdStr;

	std::vector<pid_t> mProcesses;
	std::vector<ProcState> mProcessesState;

private:
	void ConstructCmdStr(Pipe p);

	int mJobId;
	pid_t mPgid;

	static int num_jobs;

};

#endif /* JOB2_H_ */
