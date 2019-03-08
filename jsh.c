#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "fields.h"
#include "jrb.h"

#define empty new_jval_i(0)

//gets command left of |
void get_left(char **argv, char **left, int pipe_char) {
	int i = pipe_char - 1;
	int j = 0;

	while (i > 0 && strcmp(argv[i-1], "|") != 0)
		i--;
	for (i; i < pipe_char; i++)
		left[j++] = argv[i];
	left[j] = NULL;
}

//gets command right of |
int get_right(char **argv, char **right, int pipe_char) {
	int i = pipe_char + 1;
	int j = 0;

	while (argv[i] != NULL && strcmp(argv[i], "|") != 0)
		right[j++] = argv[i++];
	right[j] = NULL;
	//return 0 if end of pipe, else, return 1
	if (argv[i] == NULL)
		return 0;
	return 1;
}

//returns 1 if & detected, else 0. Removes & symbol if found
int find_amp(char **argv) {
	int i = 0;

	while (argv[i] != NULL) {
		if (strcmp(argv[i], "&") == 0) {
			argv[i] = NULL;
			return 1;
		}
		i++;
	}
	return 0;
}

//sets file contents as std io
void file_red(const char *filename, int red, int flags) {
	int fd;

	if (red == 0)
		fd = open(filename, flags);
	else if (red == 1)
		fd = open(filename, flags, 0644);

	if (fd < 0) {
		fprintf(stderr, "error opening %s\n", filename);
		exit(1);
	}
	if (dup2(fd, red) != red) {
		fprintf(stderr, "error using dup2(fd, %d)\n", red);
		exit(1);
	}
	close(fd);
}

//parses argument and redirects files as needed
void parse_arg(char **newargv) {
	int i = 0;

	while (newargv[i] != NULL) {
		if (strcmp(newargv[i], "<") == 0) {
			file_red(newargv[i + 1], 0, O_RDONLY);
			newargv[i] = NULL;
		}
		else if (strcmp(newargv[i], ">") == 0) {
			file_red(newargv[i + 1], 1, O_WRONLY | O_TRUNC | O_SYNC | O_CREAT);
			newargv[i] = NULL;
		}
		else if (strcmp(newargv[i], ">>") == 0) {
			file_red(newargv[i + 1], 1, O_WRONLY | O_SYNC | O_APPEND);
			newargv[i] = NULL;
		}
		i++;
	}
}

//sets pipe to indicated std io
void set_pipe(int *pipefd, int io) {
	if (dup2(pipefd[io], io) != io) {
		fprintf(stderr, "dup2(pipefd[%d])\n", io);
		exit(1);
	}
	close(pipefd[1]);
	close(pipefd[0]);
}

//executes command and pipes 
void execute(JRB processes, char **arg, int *pipefd, int *pipefd2, int io, int amp_flag) {
	int status;
	JRB tmp;

	int i = fork();

	//create new process and establish pipes accordingly. Then execute command
	if (i == 0) {
		if (io == 2) {
			set_pipe(pipefd2, 1);		
			set_pipe(pipefd, 0);
		}
		else if (io != -1)
			set_pipe(pipefd, io);
		//parses argument and see if <, >, >> specified. Redirect file to processes' io accordingly
		parse_arg(arg);

		execvp(arg[0], arg);
		fprintf(stderr, "%s: ", arg[0]);
		perror("");
		exit(1);
	}
	//parent process waits for child process to finish. Closes all used pipes
	else {
		if (io == 2) {
			close(pipefd2[1]);
			close(pipefd[0]);
		}
		else if (io != -1)
			close(pipefd[io]);
		//if no & character specified, store pid
		if (amp_flag == 0)
			jrb_insert_int(processes, i, empty);
	}
}

void execute_arg(char **newargv, JRB processes) {
	int i = 0;
	char *tmp[256];
	int pipe_count = 0;
	int another_pipe_flag;
	int pipefd[256][2];
	int status;

	int amp_flag = find_amp(&newargv[0]);

	//parse through entire argument line. If | specified, pipe accordingly
	while (newargv[i] != NULL) {
		if (strcmp(newargv[i], "|") == 0) {
			if (pipe_count == 0) {
				if (pipe(pipefd[pipe_count]) < 0) {
					perror("pipe");
					exit(1);
				}
			}
			//start flow if first pipe
			if (pipe_count == 0) {
				get_left(newargv, &tmp[0], i);
				execute(processes, tmp, pipefd[0], NULL, 1, amp_flag);
			}
			//if more pipes, initialize new pipe and store output into new pipe
			another_pipe_flag = get_right(newargv, &tmp[0], i);
			if (another_pipe_flag == 1) {
				if (pipe(pipefd[pipe_count + 1]) < 0) {
					perror("pipe");
					exit(1);
				}
				execute(processes, tmp, pipefd[pipe_count], pipefd[pipe_count + 1], 2, amp_flag);
			}
			//else, read from pipe and execute. Output ends up in stdout
			else
				execute(processes, tmp, pipefd[pipe_count], NULL, 0, amp_flag);
			pipe_count++;
		}
		i++;
	}
	//if no |'s detected, simply run command
	if (pipe_count == 0)
		execute(processes, newargv, NULL, NULL, -1, amp_flag);
}

int main(int argc, char **argv) {
	char *prompt = "jsh";
	char *newargv[256];
	int i;
	int status, wait_pid;
	JRB tmp;
	IS is = new_inputstruct(NULL);
	JRB processes =  make_jrb();

	if (argc == 2)
		prompt = argv[1];

	while(1) {
		if (strcmp(prompt, "-") != 0)
			printf("%s: ", prompt);

		//read entire command line and NULL terminate, then execute
		get_line(is);
		//end jsh if end of input or user types in exit
		if (is->NF == -1)
			break;
		else if (is->NF > 0) {
			for (i = 0; i < is->NF; i++)
				newargv[i] = is->fields[i];
			newargv[i] = NULL;

			if (strcmp(newargv[0], "exit") == 0)
				break;

			if (newargv[0] != NULL)
				execute_arg(newargv, processes);

			//wait for all running processes before returning prompt. JRB tree is empty if a & was specified.
			while (!jrb_empty(processes)) {
				wait_pid = wait(&status);
				tmp = jrb_find_int(processes, wait_pid);	
				if (tmp)
					jrb_delete_node(tmp);
			}
		}
	}
	//clean up
	jettison_inputstruct(is);
	jrb_free_tree(processes);
}
