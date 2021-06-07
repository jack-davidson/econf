/* See LICENSE file for copyright and license details.
 *
 * econf
 *
 * Automated deployment of configuration.
 *
 */

#define  _XOPEN_SOURCE 700
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <wordexp.h>
#include <stdarg.h>

#define DOT_OR_DOTDOT(name) strncmp(name, "..", 2) & strncmp(name, ".", 1)

#define VERSION "1.0"

#define PATH_SIZE	 512  /* maximum supported path size */
#define LINE_BUFFER_SIZE 512  /* maximum length of line in config file */
#define TOKEN_SIZE	 256  /* maximum length of a config token */
#define TOKENS		 128  /* maximum allowed config tokens */
#define HOSTNAME_SIZE	 48   /* maximum size of hostname */
#define COMMAND_SIZE	 512  /* maximum size of a command */

struct options;
struct status;

/* hold state of options */
struct options {
	unsigned force:1;
};

/* hold state of status */
struct status {
	unsigned symlinkstart:1;
	unsigned installstart:1;
	int nsymlinks;
	int nfailedsymlinks;
	int ninstallscripts;
};

static struct status sts; /* global state tracker */
static struct options opts;

int help();
int parseline(char tokens[TOKENS][TOKEN_SIZE], int t);
int confirm(char *confirm_message, char *item);
char *stripnewline(char *s);
int install(char tokens[TOKENS][TOKEN_SIZE], int t);
char *expandtilde(char *dest, char *src, int l);
int linkfiles(char *dest, char *src);
int linkdir(char *dest, char *src);
int readconfig(FILE *config);
int rm(char *path);
FILE *loadfile(char *file);
int strtolower(char *s, char *str, size_t size);
void version();
void usage();
char *strncomb(char *s, size_t n, ...);
int main(int argc, char **argv);

char *strncomb(char *s, size_t n, ...)
{
	va_list args;
	char *arg;

	va_start(args, n);
	arg = va_arg(args, char *);

	while (arg != (char *)NULL) {
		strncat(s, arg, n);
		arg = va_arg(args, char *);
	}
	va_end(args);
	return s;
}

/* recursively delete a directory or file */
int rm(char *path)
{
	struct stat path_stat;
	int is_dir;

	lstat(path, &path_stat);
	is_dir = S_ISDIR(path_stat.st_mode);

	if (is_dir) {
		struct dirent *entry;
		DIR *src_dir;

		src_dir = opendir(path);
		if (src_dir == NULL){
			fprintf(stderr, "cannot open %s\n", path);
			return -1;
		}
		while ((entry = readdir(src_dir)) != NULL) {
			if (DOT_OR_DOTDOT(entry->d_name)) {
				char new_path[PATH_SIZE];
				strncomb(new_path, PATH_SIZE - 1,
				       	 path, "/", entry->d_name, NULL);
				rm(new_path);
			}
		}
		closedir(src_dir);
	}
	remove(path);
	return 0;
}

/* link a directory src to another directory dest */
int linkdir(char *dest, char *src)
{
	char dest_dir[PATH_SIZE] = {0};
	char src_dir[PATH_SIZE] = {0};
	char cwd[PATH_SIZE];

	if (!sts.symlinkstart) {
		sts.symlinkstart = 1;
		printf(":: starting config symlinking\n\n");
	}

	if(access(src, F_OK)) {
		fprintf(stderr, "cannot open directory: %s\n", src);
		return -1;
	}

	getcwd(cwd, PATH_SIZE);

	strncomb(src_dir, PATH_SIZE - 1, cwd, "/", src, NULL);
	strncomb(dest_dir, PATH_SIZE - 1, dest, "/", src, NULL);

	rm(dest_dir);

	if(!symlink(src_dir, dest_dir)) {
		fprintf(stdout, "    symlinking %s -> %s\n",
			src_dir, dest_dir);
		sts.nsymlinks++;
	} else {
		fprintf(stderr, "    symlink failed: %s -> %s\n",
			src_dir, dest_dir);
		sts.nfailedsymlinks++;
	}

	return 0;
}

/* link all files within a directory src to another directory dest as hidden files */
int link_dotfiles(char *dest, char *src)
{
	char dest_filename[PATH_SIZE];
	char src_filename[PATH_SIZE];
	struct dirent *entry;
	char cwd[PATH_SIZE];
	DIR *src_dir;

	if (!sts.symlinkstart) {
		sts.symlinkstart = 1;
		printf(":: starting config symlinking\n\n");
	}

	src_dir = opendir(src);
	if (src_dir == NULL){
		fprintf(stderr, "cannot open directory: %s\n", src);
		return -1;
	}

	while ((entry = readdir(src_dir)) != NULL) {
		/* Exclude '.' and '..' files from source directory */
		if (DOT_OR_DOTDOT(entry->d_name)) {
			getcwd(cwd, PATH_SIZE);

			memset(dest_filename, 0, PATH_SIZE);
			memset(src_filename, 0, PATH_SIZE);

			strncomb(dest_filename, PATH_SIZE - 1,
				 dest, "/.", entry->d_name, NULL);
			strncomb(src_filename, PATH_SIZE - 1,
				 cwd, "/", src, "/", entry->d_name, NULL);

			rm(dest_filename);
			if (!symlink(src_filename, dest_filename)) {
				fprintf(stdout, "    symlinking %s -> %s\n",
					src_filename, dest_filename);
				sts.nsymlinks++;
			} else {
				fprintf(stderr,
					"    symlink failed: %s -> %s\n",
					src_filename, dest_filename);
				sts.nfailedsymlinks++;
			}

		}
	}

	closedir(src_dir);
	return 0;
}

void usage()
{
	printf("usage: econf [OPTIONS] [COMMANDS] \n");
}

void version()
{
	printf("econf-%s\n", VERSION);
}

int strtolower(char *s, char *str, size_t size)
{
	int i;
	for (i = 0; i < size; i++) {
		s[i] = tolower(s[i]);
	}
	return -1;
}

int help()
{
	usage();
	printf("\nOPTIONS:\n-v: version\n-h: help\n-f: force\n-C <directory>: set working directory\n\n");
	return 0;
}

int confirm(char *confirm_message, char *item) {
	char confirm[10] = {0};
	char *message_format;

	if (opts.force)
		return 1;

	if (item == NULL) {
		message_format = malloc(11);
		strcpy(message_format, "%s?");
	} else {
		message_format = malloc(14);
		strcpy(message_format, "%s %s?");
	}

	strcat(message_format, " [Y/n] ");

	printf(message_format, confirm_message, item);
	free(message_format);

	fgets(confirm, 10, stdin);
	strtolower(confirm, confirm, sizeof(confirm));
	if (confirm[0] == 'n') {
		return 0;
	} else {
		return 1;
	}
}

int install(char tokens[TOKENS][TOKEN_SIZE], int t)
{
	char cmd[COMMAND_SIZE] = {0};
	char cwd[PATH_SIZE];
	int i;

	printf("\n");

	if (!sts.installstart) {
		sts.installstart = 1;
		printf(":: beginning execution of installation scripts\n\n");
	}

	getcwd(cwd, PATH_SIZE);
	strncomb(cmd, PATH_SIZE - 1, cwd, "/install/", NULL);

	for (i = 1; i < t; i++) {
		strncomb(cmd, COMMAND_SIZE - 1, tokens[i], " ", NULL);
	}
	printf("    installing %s %s\n", tokens[1], *(tokens + 2));
	if (confirm("\n:: continue with installation", NULL)) {
		system(cmd);
		sts.ninstallscripts++;
	} else {
		printf("installation aborted\n");
	}
	return 0;
}

/* load config file, handle errors and return file descriptor */
FILE * loadfile(char *file)
{
	if (!access(file, F_OK)) {
		FILE *fd;
		fd = fopen(file, "r");
		return fd;
	} else {
		printf("failed to load config file\n");
		usage();
		return NULL;
	}
}

/* run command using argv with size TOKENS and each element size TOKEN_SIZE, and argc */
int command(char tokens[TOKENS][TOKEN_SIZE], int t)
{
	char command[COMMAND_SIZE] = {0};
	int i;

	for (i = 1; i < t; i++) {
		strncomb(command, COMMAND_SIZE - 1, tokens[i], " ", NULL);
	}
	system(command);
	return 0;
}

/* parse tokens of config line, called by readconfig for each line of config file
 *
 * tokens: array of strings containing tokens
 * t:      amount of tokens
 */
int parseline(char tokens[TOKENS][TOKEN_SIZE], int t)
{
	/* deprecated */
	if (tokens[1][strlen(tokens[1]) - 1] == '@') {
		printf("'@' notation is deprecated, please switch to host()\n");
		char hostname[HOSTNAME_SIZE];
		tokens[1][strlen(tokens[1]) - 1] = 0;
		gethostname(hostname, HOSTNAME_SIZE);
		strncomb(tokens[1], TOKEN_SIZE - 1, "-", hostname, NULL);
	}

	if (t >= 2) {
		if (!strcmp(tokens[0], "dir")) {
			linkdir(tokens[2], tokens[1]);
		} else if (!strcmp(tokens[0], "files")) {
			link_dotfiles(tokens[2], tokens[1]);
		} else if (!strcmp(tokens[0], "install")) {
			install(tokens, t);
		} else if (!strcmp(tokens[0], "sh")) {
			command(tokens, t);
		}
	}
	return 0;
}

/* expand tilde in src to HOME, place result in dest and return pointer to it */
char *expandtilde(char *dest, char *src, int size)
{
	char *tmp;
	int len;

	len = strlen(src); /* get length of src */
	/* add space for null character */
	tmp = calloc(len + 1, 1); /* get zeroed memory of size len */
	memset(dest, 0, size); /* zero dest */
	strncpy(tmp, src, len); /* copy src to tmp */
	memmove(tmp, tmp+1, len); /* omit first character */
	/* expand tilde */
	strncomb(dest, size - 1, getenv("HOME"), tmp, NULL);
	free(tmp);
	return dest;
}

/* strip trailing newline of string s and return pointer to s */
char *stripnewline(char *s)
{
	s[strcspn(s, "\n")] = 0;
	return s;
}

/* tokenize lines of config file and call parseline() on each line */
int readconfig(FILE *config)
{
	char line_buffer[LINE_BUFFER_SIZE]; /* holds line before being tokenized */
	char tokens[TOKENS][TOKEN_SIZE];
	char exptoken[TOKEN_SIZE]; /* new token produced by expanding certain characters to other strings */
	char *token;
	int l;   /* keep track of line number */
	int t;   /* amount of tokens */

	printf(":: beginning configuration...\n\n");
	l = 1;
	while ((fgets(line_buffer, LINE_BUFFER_SIZE, config) != NULL)) {
		/* process line buffer before it is tokenized */
		switch (line_buffer[0]) {
			case '\n':
				continue;
			case '#':
				fgets(line_buffer, LINE_BUFFER_SIZE, config);
				continue;
		}
		stripnewline(line_buffer);


		/* begin tokenization of line buffer */
		t = 0;
		token = strtok(line_buffer, " ");
		while (token != NULL) {
			switch (token[0]) {
			case '~': /* expand tilde to $HOME */
				expandtilde(exptoken, token, TOKEN_SIZE);
				strncpy(tokens[t], exptoken, TOKEN_SIZE);
			case '#': /* skip lines that begin with # (comments) */
				break;
			default:
				strncpy(tokens[t], token, TOKEN_SIZE);
			}

			if (strstr(token, ":host") != NULL) {
				char hostname[HOSTNAME_SIZE];
				token[strcspn(token, ":")] = '\0';
				gethostname(hostname, HOSTNAME_SIZE);
				strncomb(token, TOKEN_SIZE - 1, ":", hostname, NULL);
				strncpy(tokens[t], token, TOKEN_SIZE);
			}


			token = strtok(NULL, " ");
			t++; /* new line (increment line) */
		}

		parseline(tokens, t);

		/* reset line buffer and increment line number */
		memset(tokens, 0, sizeof(tokens[0][0]) * TOKENS * TOKEN_SIZE);
		memset(line_buffer, 0, LINE_BUFFER_SIZE);
		l++;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	FILE *config;

	int option;
	while ((option = getopt(argc, argv, "fvhC:")) != -1) {
		switch (option) {
		case 'f':
			opts.force = 1;
			break;
		case 'v':
			version();
			exit(0);
			break;
		case 'h':
			help();
			exit(0);
			break;
		case 'C':
			chdir(optarg);
			break;
		default:
			break;
		}
	}

	if ((config = loadfile("econf")) == NULL) {
		return -1;
	}

	readconfig(config);

	printf("\n:: symlinked %i objs\n", sts.nsymlinks);
	printf(":: installed %i scripts\n", sts.ninstallscripts);
	printf(":: completed installation with %i failed symlinks\n", sts.nfailedsymlinks);

	fclose(config);
	return 0;
}
