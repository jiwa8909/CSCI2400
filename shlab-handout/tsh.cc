//
// tsh - A tiny shell program with job control
//
// <Jiahao Wang; jiwa8909@colorado.edu>
// <Huaiqian Yan; Huaiqian.Yan@colrado.edu>

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
//

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine
//
int main(int argc, char **argv)
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler);

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  }

  exit(0); //control never reaches here
}

/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
//
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline)//The eval function parses the command line and executes related commands
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE]; /* Holds modified command line */
    int bg; /* Should the job run in bg or fg? */
    pid_t pid; /* Process id*/
    sigset_t mask;
  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
    stpcpy(buf,cmdline);
    bg = parseline(cmdline, argv);
    /*Its first task is to call the parseline function,
    which parses the space-separated command-line arguments and builds the argv vector that will eventually be passed to execve.
    The first argument is assumed to be either the name of a built-in shell command that is interpreted immediately,
    or an executable object file that will be loaded and run in the context of a new child process.*/

    if (argv[0] == NULL)
        return;   /* ignore empty lines */

    if(!builtin_cmd(argv)) /*not a build in cmd*/
    {
        sigemptyset(&mask);
        sigaddset(&mask,SIGCHLD);
        sigprocmask(SIG_BLOCK,&mask,NULL); /*block the SIGCHLD signal*/
        //by blocking SIGCHLD signals before the call to fork and then unblocking
        //them after we have called addjob, we guarantee that the child will be reaped
        //after it is added to the joblist
        if((pid = fork())==0) // Creating and Running the child process.
        {
            sigprocmask(SIG_UNBLOCK,&mask,NULL); // child process, it will first unblock SIGCHLD
            //

            setpgid(0,0); // after the fork, before the execve, the child call setpgid(0,0), put the child in the new
                         // process group whose group ID is identical to the child's PID
            if(execve(argv[0],argv,environ)<0)
            {
                //execve function loads and runs a new program in the context of the current process argument
                // list argv, execve returns to the calling program only if there is an error, such as not be
                // able to find filename, call once never return. argv[0] is the name of the executable object file.
                printf("%s: Command not found\n",argv[0]);
                exit(0); //Be sure to add exit(0) here, or the child process will start running the main process's code with unpredictable errors when execve's function fails to execute
            }
        }

        //The parent process determines whether the child process is the foreground process or the background process.
        //If it is the foreground process, waitpid is called to wait for the foreground process.
        //If it is the background process, relevant process information is printed out.
        //At the same time, add the newly added process to the group using addjob.

        //Parent process:add new jobs and printf message
        if(!bg)
        {
            if(addjob(jobs, pid, FG, cmdline))
            {
                sigprocmask(SIG_UNBLOCK, &mask,NULL);
                waitfg(pid);  //the parent process waits for the previous process to finish

            }
            else
            {
                kill(-pid, SIGINT); // sending sig to all of process of PID
            }

       }

       else
       {
           if(addjob(jobs, pid, BG, cmdline))
           {
               sigprocmask(SIG_UNBLOCK, &mask,NULL);
               printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
           }
           else
           {
               kill(-pid, SIGINT);
           }

       }


    }

    return;

}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) //This function determines which built-in command is which case
{

  if(!strcmp(argv[0],"quit")) //Exit the command
    {
        exit(0);
    }
    else if(!strcmp(argv[0],"&")) //Ignore singleton,
    {
        return 1;
    }
    /*
    If the last argument is an “&” character,
    then parseline returns 1,
    indicating that the program should be executed in the background (the shell does not wait
    for it to complete). Otherwise it returns 0,
    indicating that the program should be run in the foreground (the shell waits for it to complete).
    */
    else if(!strcmp(argv[0],"jobs")) //job order
    {
        listjobs(jobs);
        return 1;
    }
    else if(!strcmp(argv[0],"bg") || !strcmp(argv[0],"fg"))
    {
        do_bgfg(argv);
        return 1;
    }

  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv)
{
  struct job_t *jobp=NULL;

  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }

  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //

  //FG and BG perform two operations
  string cmd(argv[0]);
  if (jobp)
    {
      kill(-(jobp->pid), SIGCONT);

      if (cmd == "bg") //If it's bg, it means to revert back to the background process, which is to change the state of the job
      {
        jobp->state = BG;
        printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
      }
      else if (cmd=="fg")
      {
        jobp->state = FG; //If fg, it means to restore to the foreground process, that is, change the state of job, and then call waitfg;
        //Wait for the foreground process to finish running
        waitfg(jobp->pid);
      }
    }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  while (pid == fgpid(jobs)) //
  {
      sleep(0);// suspend all of the process of PID group
  }
  return;
}
/*(pause and sleep)
Sleep returns zero if the requested amount of time has elapsed, and the number of seconds still left to sleep otherwise.
The latter case is possible if the sleep function returns prematurely because it was interrupted by a signal.

Another function that we will find useful is the pause function,
which puts the calling function to sleep until a signal is received by the process.
*/

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.
//
void sigchld_handler(int sig)
{
    pid_t pid;
    int status;
    /*WNOHANG|WUNTRACED: Return immediately, with a return value of 0, if none of the children in the wait set has stopped or terminated,
    or with a return value equal to the PID of one of the stopped or terminated children.*/
	while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) //Reap a zombie child,Using waitpid to reap zombie children in the order they were created
    {
		if(WIFSTOPPED(status)) //  WIFSTOPPED(status): Returns true if the child that caused the return is currently stopped.
		{
			getjobpid(jobs, pid)->state = ST;
			printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
			//WSTOPSIG(status): Returns the number of the signal that caused the child to stop. This status is only defined if WIFSTOPPED(status) returned true.
			continue;
		}
		if (WIFSIGNALED(status)) //WIFSIGNALED(status): Returns true if the child process terminated be- cause of a signal that was not caught.
			printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
			//WTERMSIG(status): Returns the number of the signal that caused the child process to terminate. This status is only defined if WIFSIGNALED(status) returned true.
		deletejob(jobs,pid);
	}

  return;

}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.
//
/*
ctrl + c cause the kernel to send a SIGINT signal to every process in
the foreground process group. In the default case, the result is to terminate
the foreground job
*/

void sigint_handler(int sig)
{
    if (fgpid(jobs)!=0)
    {
        kill (-fgpid(jobs), sig);// sent sig to all the process in the group
    }

    return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.
//
/*
ctrl + z cause the kernel to send a SIGINT signal to every process in
the foreground process group. In the default case, the result is to stop
the foreground job
*/
void sigtstp_handler(int sig)
{
    if (fgpid(jobs)!=0)
    {
        kill (-fgpid(jobs), sig);// sending sig to all of process of PID group  - all of
    }

    return;
}

/*********************
 * End signal handlers
 *********************/




