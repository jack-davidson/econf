/* See LICENSE file for copyright and license details.
 *
 * econf
 *
 * Automated deployment of configuration.
 *
 */

#define  _XOPEN_SOURCE 700
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <wordexp.h>
#include <stdarg.h>

#define DOT_OR_DOTDOT(name) strncmp(name, "..", 2) & strncmp(name, ".", 1)

#define VERSION "0.8"

#define PATH_SIZE	 512  /* maximum supported path size */
#define LINE_BUFFER_SIZE 512  /* maximum length of line in config file */
#define TOKEN_SIZE	 256  /* maximum length of a config token */
#define TOKENS		 128  /* maximum allowed config tokens */
#define HOSTNAME_SIZE	 48   /* maximum size of hostname */
#define COMMAND_SIZE	 512  /* maximum size of a command */

int parseline(char tokens[TOKENS][TOKEN_SIZE], int t);
char *stripnewline(char *s);
char *expandtilde(char *dest, char *src, int l);
int parseargs(int argc, char **argv);
int linkfiles(char *dest, char *src);
int linkdir(char *dest, char *src);
int readconfig(FILE *config);
int rm(char *path);
FILE *loadfile(char *file);
int install(char *script);
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

	if(access(src, F_OK)) {
		fprintf(stderr, "cannot open directory: %s\n", src);
		return -1;
	}

	getcwd(cwd, PATH_SIZE);

	strncomb(src_dir, PATH_SIZE - 1, cwd, "/", src, NULL);
	strncomb(dest_dir, PATH_SIZE - 1, dest, "/", src, NULL);

	rm(dest_dir);

	if(!symlink(src_dir, dest_dir))
		fprintf(stdout, "%s -> %s\n",
			src_dir, dest_dir);
	else
		fprintf(stderr, "symlink failed: %s -> %s\n",
			src_dir, dest_dir);

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
			if (!symlink(src_filename, dest_filename))
				fprintf(stdout, "%s -> %s\n",
					src_filename, dest_filename);
			else
				fprintf(stderr,
					"symlink failed: %s -> %s\n",
					src_filename, dest_filename);
		}
	}

	closedir(src_dir);
	return 0;
}

void usage()
{
	printf("usage: econf [-vh] [-C working_directory] \n");
}

void version()
{
	printf("econf-%s\n", VERSION);
}

int strtolower(char *s, char *str, size_t size)
{
	int i;
	for (i = 0; i < size; i++) {
		*(s + i) = tolower(*(str + i));
	}
	return -1;
}

int install(char *script)
{
	char script_path[PATH_SIZE];
	char cwd[PATH_SIZE];
	char confirm[10];

	memset(script_path, 0, sizeof(script_path));
	memset(confirm, 0, sizeof(confirm));

	getcwd(cwd, PATH_SIZE);
	strncomb(script_path, PATH_SIZE - 1, cwd, "/install/", script, NULL);

	printf("install %s? [Y/n] ", script);
	scanf("%c", confirm);
	strtolower(confirm, confirm, sizeof(confirm));

	if (strcmp(confirm, "n")) {
		printf("executing install script %s\n", script);
		if (execl(script_path, "", NULL) < 0) {
			printf("failed to execute install script %s\n", script);
		}
	}
	return 0;
}

/* parse main program arguments */
int parseargs(int argc, char **argv) {
	int option;

	while ((option = getopt(argc, argv, "vhC:")) != -1) {
		switch (option) {
		case 'v':
			version();
			exit(0);
			break;
		case 'h':
			usage();
			exit(0);
			break;
		case 'C':
			chdir(optarg);
			break;
		default:
			break;
		}
	}

	if (argc > optind && !strcmp(argv[optind], "install"))
		install(argv[optind + 1]);
	return 0;
}

/* load config file, handle errors and return file descriptor */
FILE * loadfile(char *file)
{
	if (!access(file, F_OK)) {
		FILE *fd;
		fd = fopen(file, "r");
		return fd;
	}
	return NULL;
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
	if (tokens[1][strlen(tokens[1]) - 1] == '@') {
		char hostname[HOSTNAME_SIZE];
		tokens[1][strlen(tokens[1]) - 1] = 0;
		gethostname(hostname, HOSTNAME_SIZE);
		strncomb(tokens[1], TOKEN_SIZE - 1, "-", hostname, NULL);
	}

	if (t >= 2) {
		if (!strcmp(tokens[0], "dir")) {
			linkdir(tokens[2], tokens[1]);
		}
		if (!strcmp(tokens[0], "files")) {
			link_dotfiles(tokens[2], tokens[1]);
		}
		if (!strcmp(tokens[0], "install")) {
			install(tokens[1]);
		}
		if (!strcmp(tokens[0], "run")) {
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
	int err;
	int t;   /* amount of tokens */

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
			default: /* otherwise copy the token to the token array */
				strncpy(tokens[t], token, TOKEN_SIZE);
			}
			token = strtok(NULL, " ");
			t++; /* new line (increment line) */
		}

		/* handle parser errors */
		if ((err = (parseline(tokens, t))) < 0) {
			printf("failed to parse line %i in config (error code %i)\n",
			       l, err);
			return err;
		}

		/* reset line buffer and increment line number */
		memset(line_buffer, 0, LINE_BUFFER_SIZE);
		l++;
	}
	return 0;
}

int main(int argc, char **argv)
{
	FILE *config;
	int err;

	/* handle errors: */

	if ((err = parseargs(argc, argv)) < 0) {
		printf("failed to parse arguments (error code %i)\n", err);
		usage();
	}

	if ((config = loadfile("econf")) == NULL) {
		printf("failed to load config file\n");
		usage();
	}

	if ((err = readconfig(config)) < 0) {
		printf("failed to lex config (error code %i)\n", err);
		return err;
	}

	fclose(config);
	return 0;
}
