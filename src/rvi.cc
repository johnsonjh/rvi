// rvi - Revision Control System Interface; editor wrapper
// v1.0.0 - v1.0.2 by Melekam Mtsegaye <mtsegaye-fm@rucus.ru.ac.za>
// v1.1.0 - v1.1.1 by Jeffrey H. Johnson <trnsz@pobox.com>

#define RVI_VERSION "v1.1.1 (2021-03-04)"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef linux
#include <linux/binfmts.h>
#include <linux/limits.h>
#define D_ARG_MAX MAX_ARG_STRLEN
#else
#define D_ARG_MAX sysconf(_SC_ARG_MAX)
#endif /* linux */

#ifndef D_ARG_MAX
#define D_ARG_MAX 255
#endif /* !D_ARG_MAX */

#ifdef linux
#ifndef PAGE_SIZE
#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#endif  /* !PAGE_SIZE */
#endif  /* linux */

#define RCSDIR "RCS/\0"
#define RCSEXT ",v\0"

#define UNLOCK "/usr/bin/rcs -u -q \"\0"
#define CHECK_OUT "/usr/bin/co -l -q \"\0"
#define CHECK_OUT_R "/usr/bin/co -q \"\0"
#define CHECK_IN "/usr/bin/ci -u -q \"\0"

#define REEDIT 2
#define ABORT 4
#define DG_YES 6
#define DG_NO 8
#define DG_OTHER 0

// protos
void handle_error(int);
void do_filecheck(char *);
int do_filechange_check(char *, struct stat *);
char *exec_cmd(char **, int, int flag = 0);
int dialog();

int main(int argc, char **argv)
{
	fprintf(stdout, "%s: Revision Control System Interface %s\n", argv[0],
		RVI_VERSION);
	if (argc < 2 || argc > 3) {
		fprintf(stdout, "Usage: %s <filename>\n", argv[0]);
		exit(1);
	}

	// check that
	// 1) the file isn't already locked out
	// 2) check/create /RCS exists, if it doesn't create it w/user's consent
	do_filecheck(argv[1]);

	// save the uid and gid of the file being opened
	struct stat fileinfo;

	stat(argv[1], &fileinfo);

#ifdef DEBUG
	fprintf(stderr, "UID: %d EUID:%d GID:%d EGID:%d\n", fileinfo.st_uid,
		getenv("UID"), fileinfo.st_gid, getenv("GID"));
#endif

	// get editor
	char *editor;
	editor = getenv("EDITOR");
	if (!editor) {
		fprintf(stderr, "Error: EDITOR variable undefined\n");
		exit(1);
	}

	// multiple editing

	// go ahead and check the file out (co -l filename
	{
		char *params[3];
		params[0] = CHECK_OUT;
		params[1] = argv[1];
		params[2] = "\"";
		exec_cmd(params, 3);
		// save modification time
		struct stat tmp;
		stat(argv[1], &tmp);
		fileinfo.st_mtime = tmp.st_mtime;
	}

	do {
		// spawn editor
		char *params[5];
		params[0] = editor;
		params[1] = " \0";
		params[2] = "\"";
		params[3] = argv[1];
		params[4] = "\"";
		exec_cmd(params, 5);

		// ask user if he/she wants to keep the changes he made
	} while (do_filechange_check(argv[1], &fileinfo) == REEDIT);

	// check in file into RCS and check out a read_only copy
	char *params[3];
	params[0] = CHECK_IN;
	params[1] = argv[1];
	params[2] = "\"";
	exec_cmd(params, 3);

	// restore the uid_gid of the file
	chown(argv[1], fileinfo.st_uid, fileinfo.st_gid);

	return 0;
}

// function to check for existance of filename, proper user permissions and
// locked out files
void do_filecheck(char *filename)
{
	// extract path to file
	char *rcspath;
	int fname_p;
	int str_ln = strlen(filename) - 1;

	for (fname_p = str_ln; fname_p > 0 && filename[fname_p] != '/'; fname_p--)
		;
	if (fname_p < 1 && filename[0] == '/') {
		rcspath = "/\0";
		fname_p++;
	} else if (fname_p < 1)
		rcspath = "./\0";
	else {
		rcspath = new char[fname_p + 1];
		strncpy(rcspath, filename, ++fname_p);
	}

	struct stat *filecheck = new struct stat;

	memset(filecheck, -1, sizeof(struct stat));
	stat(rcspath, filecheck);
	// check that user has write access
	if (!((filecheck->st_mode - S_IFDIR | S_IWUSR) ==
	      filecheck->st_mode - S_IFDIR)) {
		fprintf(stderr, "Error: No write permission for \"%s\"\n", rcspath);
		exit(1);
	}

	// check if RCS exits
	int rcsdir_exists = 0, rcsfile_exists = 0;

	filecheck->st_size = -1;
	memset(filecheck, -1, sizeof(struct stat));
	char *FULLRCSPATH;

	{
		char *params[2];
		params[0] = rcspath;
		params[1] = RCSDIR;
		FULLRCSPATH = exec_cmd(params, 2, 1);
	}
	stat(FULLRCSPATH, filecheck);
	if (S_ISDIR(filecheck->st_mode)) {
		rcsdir_exists = 1;
		if (rcsdir_exists) { // check RCS/filename,v exists
			char *param[3];
			param[0] = FULLRCSPATH;
			param[1] = filename + fname_p;
			param[2] = RCSEXT;
			// concat RCSDIR + filename + RCSEXT
			memset(filecheck, -1, sizeof(struct stat));
			char *fname = exec_cmd(param, 3, 1);
			char *qfnme = strstr(fname, "\"");
			if (!qfnme) {
				stat(fname, filecheck);
				if (S_ISREG(filecheck->st_mode))
					rcsfile_exists = 1;
			} else {
				fprintf(stderr, "Error: Filename may not contain double quotes.\n");
				exit(1);
			}
		}
	}

	// check if the file exists
	memset(filecheck, -1, sizeof(struct stat));
	stat(filename, filecheck);
	int file_exists = 0;

	if (S_ISREG(filecheck->st_mode))
		file_exists = 1;
	if (!file_exists && !rcsfile_exists) { // specified file doesn't exist
		fprintf(stdout, "Warning: \"%s\" could not be found.\n", filename);
		fprintf(stdout, "Create it and check it into RCS? [N] ");
		if (dialog() != DG_YES)
			exit(0);
#ifndef DEBUG
		FILE *f = fopen(filename, "a");
		if (!f) {
			fprintf(stderr, "Error: Failed to create \"%s\": ", filename);
			handle_error(-1);
		} else
			fclose(f);
		if (!rcsdir_exists)
			mkdir(FULLRCSPATH, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
		delete[] FULLRCSPATH;
		// check file in first time
		char *params[3];
		params[0] = CHECK_IN;
		params[1] = filename;
		params[2] = "\"";
		exec_cmd(params, 3);
	} else if (!rcsfile_exists && file_exists) {
		fprintf(stdout,
			"\"%s\" is not under RCS control; do initial check in? [N] ",
			filename);
		if (dialog() != DG_YES)
			exit(0);
#ifndef DEBUG
		if (!rcsdir_exists)
			mkdir(FULLRCSPATH, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
		delete[] FULLRCSPATH;
		// check file in first time
		char *params[3];
		params[0] = CHECK_IN;
		params[1] = filename;
		params[2] = "\"";
		exec_cmd(params, 3);
	} else if (file_exists & rcsfile_exists) {
		// check if file has been checked out aready/writable version exists
		memset(filecheck, -1, sizeof(struct stat));
		stat(filename, filecheck);
		if ((filecheck->st_mode - S_IFREG | S_IWUSR) ==
		    filecheck->st_mode - S_IFREG ||
		    (filecheck->st_mode - S_IFREG | S_IWGRP) ==
		    filecheck->st_mode - S_IFREG ||
		    (filecheck->st_mode - S_IFREG | S_IWOTH) ==
		    filecheck->st_mode - S_IFREG) {
			fprintf(stderr, "Error: \"%s\" already checked out.\n", filename);
			exit(1);
		}
	}
	delete filecheck;
}

// function to check in size & time of a file
int do_filechange_check(char *fname, struct stat *original)
{
	struct stat finfo;

	stat(fname, &finfo);
	if (finfo.st_mtime != original->st_mtime) {
		fprintf(stdout,
			"File \"%s\" has been modified.\n[A]ccept, [R]e-edit, or "
			"[D]iscard? [A] ",
			fname);
		int key = dialog();
		if (key == ABORT) { // unlock file and exit
			{
				char *params[3];
				params[0] = UNLOCK;
				params[1] = fname;
				params[2] = "\"";
				exec_cmd(params, 3);
			}
			remove(fname);
			// restore file (read-only)
			{
				char *params[3];
				params[0] = CHECK_OUT_R;
				params[1] = fname;
				params[2] = "\"";
				exec_cmd(params, 3);
			}
			exit(0);
		} else
			return key;
	}
	return DG_OTHER;
}

// function to execute a command || copy list into a linear array
// command[0] == command
// command[1..n] == args
char *exec_cmd(char **params, int len, int flag)
{
	char *command = new char[D_ARG_MAX];
	int buf_ptr = 0;

	for (int i = 0; i < len; i++) {
		strncpy(command + buf_ptr, params[i], D_ARG_MAX - buf_ptr);
		buf_ptr += strlen(params[i]);
	}

	strncpy(command + buf_ptr + 1, "\"", D_ARG_MAX - buf_ptr);

#ifndef DEBUG
	if (flag)
		return command;
	handle_error(system(command));
#else
	fprintf(stderr, "%s\n", command);
	if (flag)
		return command;
#endif
	delete[] command;
	return (char *)NULL;
}

// um.. just in case
void handle_error(int err)
{
	switch (err) {
	case 127:
		fprintf(stderr, "Error: execve call failed\n");
		exit(1);
	case -1:
		fprintf(stderr, "Error: %s\n", strerror(errno));
		exit(1);
	default:
		break;
	}
}

// handles simple y/n dialog w/user
int dialog()
{
	char response[10];

	fgets(response, 10, stdin);
	response[0] = toupper(response[0]);
	switch (response[0]) {
	case 'D':
		return ABORT;
	case 'R':
		return REEDIT;
	case 'Y':
		return DG_YES;
	case 'N':
		return DG_NO;
	}
	return DG_OTHER;
}
