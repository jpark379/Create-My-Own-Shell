/* $begin shellmain */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <fcntl.h>


#define MAXARGS 512
#define MAXLINE 8192                              /* Max text line length */
#define SIGINT_MES "\ncaught sigint\nCS 361 > "   /* Message when sigint is caught */
#define SIGINT_MES_LEN 25                         /* Length of the message */
#define SIGTSTP_MES "\ncaught sigtstp\nCS 361 > " /* Message when sigstp is caught */
#define SIGTSTP_MES_LEN 25                        /* Length of the message */

extern char **environ;    /* Defined by libc */
pid_t globalPID;          /* Global variable to hold the pid */
int globalStatus;         /* Global variable to hold the status */

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
int builtin_command(char **argv);

void unix_error(char *msg) /* Unix-style error */
{
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
}

/* This is a handler that catches the SIGINT signal */
static void sig_handler(int this_signal) {
  /* Write out the message that SIGINT was caught */
  if (this_signal == SIGINT) write(STDOUT_FILENO, SIGINT_MES, SIGINT_MES_LEN);

  /* Write out the message that SIGSTP was caught */
  if (this_signal == SIGTSTP) write(STDOUT_FILENO, SIGTSTP_MES, SIGTSTP_MES_LEN);
}


/* MAIN */
int main() {
  char cmdline[MAXLINE]; /* Command line */

  /* Ignore if SIGINT is caught */
  signal(SIGINT, sig_handler);

  /* Ignore if SIGSTOP is caught */
  signal(SIGTSTP, sig_handler);

  while (1) {
    char *result;

    /* Read */
    printf("CS 361 > "); /* Command prompt for the shell */
    result = fgets(cmdline, MAXLINE, stdin);

    if (result == NULL && ferror(stdin)) {
      fprintf(stderr, "fatal fgets error\n");
      exit(EXIT_FAILURE);
    }

    if (feof(stdin)) exit(0);

    /* Evaluate */
    eval(cmdline);
  }
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) {
  char *argv[MAXARGS];            /* Argument list execve() */
  char buf[MAXLINE];              /* Holds modified command line */
  int i = 0;                      /* Used to iterate through argv */
  int bg;                         /* Should the job run in bg or fg? */
  pid_t pid;                      /* Process id */

  int inRedirectionCount = 0;     /* Count how many time there is input */
  int outRedirectionCount = 0;    /* Count how many time there is output */
  int pipeCount = 0;              /* Count how many time there is piping */
  int semicolCount = 0;           /* Count how many time there is semicolon */

  char* inputFile;                /* Hold name of input file */
  char* outputFile;               /* Hold name of input file */

  char *semicolCommand1[MAXARGS]; /* An array of char pointers that will hold the 1st command & arguments for semicolon */
  char *semicolCommand2[MAXARGS]; /* An array of char pointers that will hold the 2nd command & arguments for semicolon */

  int child_status;                              /* Status of child process */
  posix_spawn_file_actions_t actions1, actions2; /* Variables for actions */
  int pipe_fds[2];                               /* Array of ints to hold pipe file descriptors */
  int pid1, pid2;                                /* Ints to hold the proccess ids */
  char *pipeCommand1[MAXARGS];                   /* An array of char pointers that will hold the 1st command & arguments for pipe */
  char *pipeCommand2[MAXARGS];                   /* An array of char pointers that will hold the 2nd command & arguments for pipe */

  strcpy(buf, cmdline);
  bg = parseline(buf, argv);
  if (argv[0] == NULL) return; /* Ignore empty lines */

  /* Loop through argv */
  while (argv[i] != NULL) {

    /* If input sign is in the line */
    if (strcmp(argv[i], "<") == 0) {

      /* Increment inRedirectionCount */
      inRedirectionCount++;

      /* Store the input file name into the variable */
      inputFile = argv[i+1];

      /* Set the element in array with "<" as NULL */
      argv[i] = NULL;
    }

    /* If output sign is in the line */
    if (argv[i] != NULL) {

      /* Output sign is in the line */
      if (strcmp(argv[i], ">") == 0) {

        /* Increment outRedirectionCount */
        outRedirectionCount++;

        /* Store the output file name into the variable */
        outputFile = argv[i+1];

        /* Set the element in array with ">" as NULL */
        argv[i] = NULL;
      }
    }

    /* If piping sign is in the line */
    if (argv[i] != NULL) {

      /* Piping sign is in the line */
      if (strcmp(argv[i], "|") == 0) {

        /* Increment the pipeCount */
        pipeCount++;

        /* Store the first command along with its arguments into pipeCommand1 array */
        int j;
        for (j = 0; j < i; j++) {
          pipeCommand1[j] = argv[j];
        }
        pipeCommand1[j] = NULL;   // Have the last index in array as to NULL

        /* Store the second command along with its arguments into pipeCommand2 array */
        int k = 0;
        i++;    // Increment i so that it will start with the 2nd command, skipping over the NULL
        while ( argv[i] != NULL) {
          pipeCommand2[k] = argv[i];
          k++;
          i++;
        }
        pipeCommand2[k] = NULL;   // Have the last index in array as to NULL
      }
    }

    /* If ; sign is in the line */
    if (argv[i] != NULL ) {

      /* Semicolon sign is in the line */
      if (strcmp(argv[i], ";") == 0) {

        /* Increment the pipeCount */
        semicolCount++;

        /* Store the first command along with its arguments into semicolCommand1 array */
        int j;
        for (j = 0; j < i; j++) {
          semicolCommand1[j] = argv[j];
        }
        semicolCommand1[j] = NULL;   // Have the last index in array as to NULL

        /* Store the second command along with its arguments into pipeCommand2 array */
        int k = 0;
        i++;    // Increment i so that it will start with the 2nd command, skipping over the NULL
        while ( argv[i] != NULL) {
          semicolCommand2[k] = argv[i];
          k++;
          i++;
        }
        semicolCommand2[k] = NULL;   // Have the last index in array as to NULL
      }
    }
    i++;
  }

  /* Store 0 and 1 into stdin and stdout variables */
  int stdin = dup(0);
  int stdout = dup(1);

  /* Case where there is a file to be input redirected */
  if (inRedirectionCount != 0) {

    /* Open the file and store the resulting file descriptor into fdIn */
    int fdIn = open(inputFile, O_RDONLY, 0);

    /* If the fdIn is less than 0, an error occurred, exit */
    if ( fdIn < 0) {
      perror("could not open input file");
      exit(1);
    }

    /* Copy file descriptor to stdin */
    dup2(fdIn, 0);

    /* Close the file */
    close(fdIn);  
  }

  /* Case where there is a file to be input redirected */
  if (outRedirectionCount != 0) {

    /* Create the file and store the resulting file descriptor into fdOut */
    int fdOut = creat(outputFile, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    /* If fdOut is less than 0, there has been an error, exit */
    if ( fdOut < 0) {
      perror("could not open output file");
      exit(1);
      
    }

    /* Copy file descriptor to stdout */
    dup2(fdOut, 1);

    /* Close the file */
    close(fdOut);
  }

  /* Check if question mark is here and is on its own line */ 
  if (strcmp(argv[0], "?") == 0 && argv[1] == NULL) {
    /* Print out the pid and the status */
    printf("pid:%d status:%d\n", globalPID, globalStatus);
  }

  /* Case if the semicolon is in the command */
  if (!builtin_command(argv)) {
    if (semicolCount != 0) {
      /* Run the command semicolCommand1 using posix_spawnp */
      if (0 != posix_spawnp(&pid, semicolCommand1[0], NULL, NULL, semicolCommand1, environ)) {
        perror("spawn failed");
      } 

      /* Parent waits for foreground job to terminate */
      if (!bg) {
        int status;
        if (waitpid(pid, &status, 0) < 0) unix_error("waitfg: waitpid error");
        globalPID = pid;
        globalStatus = status;
      }
      else {
        printf("%d %s", pid, cmdline);
      }

      /* Run the command semicolCommand2 using posix_spawnp */
      if (0 != posix_spawnp(&pid, semicolCommand2[0], NULL, NULL, semicolCommand2, environ)) {
        perror("spawn failed");
      } 

      /* Parent waits for foreground job to terminate */
      if (!bg) {
        int status;
        if (waitpid(pid, &status, 0) < 0) unix_error("waitfg: waitpid error");
        globalPID = pid;
        globalStatus = status;
      }
      else {
        printf("%d %s", pid, cmdline);
      }
    }

    /* If the pipe sign was present */
    else if (pipeCount != 0) {

        /* Initialize spawn file actions object for bboth the processes */
        posix_spawn_file_actions_init(&actions1);
        posix_spawn_file_actions_init(&actions2);

        /* Create a unidirectional pipe for interprocess communication with a read and write end */
        pipe(pipe_fds);

        /* Add duplication action of copying the write end of the pipe to the standard out fd */
        posix_spawn_file_actions_adddup2(&actions1, pipe_fds[1], STDOUT_FILENO);

        /* Add action of closing the read end of the pipe */
        posix_spawn_file_actions_addclose(&actions1, pipe_fds[0]);

        /* Add duplication action of copying the write end of the pipe to the standard out fd */
        posix_spawn_file_actions_adddup2(&actions2, pipe_fds[0], STDIN_FILENO);

        /* Add action of closing the write end of the pipe */
        posix_spawn_file_actions_addclose(&actions2, pipe_fds[1]);

        /* Create the first child process */
        if (0 != posix_spawnp(&pid1, pipeCommand1[0], &actions1, NULL, pipeCommand1, environ)) {
          perror("spawn failed");
        }

        /* Create the second child process */
        if (0 != posix_spawnp(&pid2, pipeCommand2[0], &actions2, NULL, pipeCommand2, environ)) {
          perror("spawn failed");
        }

        /* Create the read and write end in the parent process */
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        /* Wait for the first and second child to complete */
        waitpid(pid1, &child_status, 0);
        waitpid(pid2, &child_status, 0);
    }

    /* Any other command--input redirection, output redirection, normal command */
    else {
      /* Run the command argv using posix_spawnp */
      if (0 != posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ)) {
        perror("spawn failed");
      } 

      /* Parent waits for foreground job to terminate */
      if (!bg) {
        int status;
        if (waitpid(pid, &status, 0) < 0) unix_error("waitfg: waitpid error");
        globalPID = pid;
        globalStatus = status;
      }
      else {
        printf("%d %s", pid, cmdline);
      }
    }
  } 

  /* Duplicate stdin to 0 */
  dup2(stdin, 0);

  /* Duplicate stdout to 1 */
  dup2(stdout,1);
  return;
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv) {
  if (!strcmp(argv[0], "exit")) /* exit command */
    exit(0);
  if (!strcmp(argv[0], "&")) /* Ignore singleton & */
    return 1;
  if (!strcmp(argv[0], "?")) /* Ignore ? */
    return 1;
  return 0; /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) {
  char *delim; /* Points to first space delimiter */
  int argc;    /* Number of args */
  int bg;      /* Background job? */

  buf[strlen(buf) - 1] = ' ';   /* Replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* Ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  while ((delim = strchr(buf, ' '))) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* Ignore spaces */
      buf++;
  }

  argv[argc] = NULL;

  if (argc == 0) /* Ignore blank line */
    return 1;

  /* Should the job run in the background? */
  if ((bg = (*argv[argc - 1] == '&')) != 0) argv[--argc] = NULL;

  return bg;
}
/* $end parseline */
