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
#include <limits.h>

#define DOT_OR_DOTDOT(name) !(strncmp(name, "..", 2) \
			    & strncmp(name, ".", 1))

#define VERSION "1.0"

#define PATH_SIZE	 PATH_MAX + 1
#define LINE_BUFFER_SIZE 512
#define TOKEN_SIZE	 256
#define TOKENS		 128
#define	HOSTNAME_SIZE	 48
#define	COMMAND_SIZE	 512

typedef	char Tokens[TOKENS][TOKEN_SIZE];
typedef	char Path[PATH_SIZE];

struct	options;
struct	status;

struct	 options {
	unsigned int force:1;
	Path configpath;
};

struct 	status {
	unsigned int symlinkstart:1;
	unsigned int installstart:1;
	int nfailedsymlinks;
	int ninstallscripts;
	int nsymlinks;
};

int	confirm(char *confirm_message, char *item);
int	parseline(Tokens tokens, int ntokens, int l);
int	linkdotfiles(Path dest, Path src);
int	install(Tokens tokens, int t);
int	linkdir(Path dest, Path src);
int	parseerror(Tokens tokens, int ntokens, int l);
int	readconfig(FILE *config);
int	sh(Tokens tokens, int t);
int	rm(Path path);
char	*strtolower(char *s, char *str, size_t size);
char	*expandtilde(Path dest, Path src, int l);
char	*strncomb(char *s, size_t n, ...);
char	*stripnewline(char *s);
FILE	*loadconfig(Path file);
void	version();
void	usage();
void	help();

int	main(int argc, char **argv);

static	struct status sts;
static	struct options opts;

int
confirm(char *confirm_message, char *item) {
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

int
parseline(Tokens tokens, int ntokens, int l)
{
	if (ntokens == 1) {
		if (!(strcmp(tokens[0], "noconfirm"))) {
			opts.force = 1;
			return 0;
		}
		if (!(strcmp(tokens[0], "confirm"))) {
			opts.force = 0;
			return 0;
		}
	} if (ntokens == 3) {
		if (!strcmp(tokens[0], "dir"))
			return linkdir(tokens[2], tokens[1]);
		else if (!strcmp(tokens[0], "files"))
			return linkdotfiles(tokens[2], tokens[1]);
	} if (!strcmp(tokens[0], "sh"))
		return sh(tokens, ntokens);
	if (!strcmp(tokens[0], "install"))
		return install(tokens, ntokens);
	return -1;
}

int
parseerror(Tokens tokens, int ntokens, int l) {
	int f;
	int i;

	f = opts.force;
	opts.force = 0;

	printf("failed to parse line %i:\n\tunrecognized tokens: \"", l);
	for (i = 0; i < ntokens; i++) {
		printf("%s ", tokens[i]);
	}
	printf("\"\n");
	confirm("\ncontinue with error", NULL);
	printf("\n");

	opts.force = f;
	return 0;
}

/* link all files within a directory src to another directory dest as hidden files */
int
linkdotfiles(Path dest, Path src)
{
	Path dest_filename;
	Path src_filename;
	Path cwd;

	struct dirent *entry;
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
		if (!DOT_OR_DOTDOT(entry->d_name)) {
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

int
install(Tokens tokens, int t)
{
	char cmd[COMMAND_SIZE] = {0};
	Path cwd;
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

int
linkdir(Path dest, Path src)
{
	Path dest_dir = {0};
	Path src_dir = {0};
	Path cwd = {0};

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

char *
strncomb(char *s, size_t n, ...)
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

int
rm(Path path)
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
			if (!DOT_OR_DOTDOT(entry->d_name)) {
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

void
usage()
{
	printf("usage: econf [-fhv] [-c path] [-C directory]\n");
}

void
version()
{
	printf("econf-%s\n", VERSION);
}

char *
strtolower(char *s, char *str, size_t size)
{
	int i;
	for (i = 0; i < size; i++) {
		s[i] = tolower(s[i]);
	}
	return s;
}

void
help()
{
	usage();
	printf("\nOPTIONS:\n-v: version\n-h: help\n-f: force\n-C <directory>: "
	    "set working directory\n-c <file>: use file for config\n\n");
}

FILE *
loadconfig(Path path)
{
	if (!access(path, F_OK)) {
		FILE *fd;
		fd = fopen(path, "r");
		return fd;
	} else {
		printf("failed to load config file\n");
		usage();
		return NULL;
	}
}

int
sh(Tokens tokens, int t)
{
	char cmd[COMMAND_SIZE] = {0};
	int i;

	for (i = 1; i < t; i++) {
		strncomb(cmd, COMMAND_SIZE - 1, tokens[i], " ", NULL);
	}

	system(cmd);
	return 0;
}

char *
expandtilde(Path dest, Path src, int size)
{
	int len;
	char *tmp;

	len = strlen(src);
	tmp = calloc(len + 1, 1);

	memset(dest, 0, size);
	strncpy(tmp, src, len);
	memmove(tmp, tmp + 1, len); /* copy all but first char of tmp */
	strncomb(dest, size - 1, getenv("HOME"), tmp, NULL);

	free(tmp);
	return dest;
}

char *
stripnewline(char *s)
{
	s[strcspn(s, "\n")] = 0;
	return s;
}

int
readconfig(FILE *config)
{
	Tokens tokens;
	char line_buffer[LINE_BUFFER_SIZE];
	Path exppath;
	int ntokens;
	char *token;
	int l;

	l = 1;
	printf(":: beginning configuration...\n\n");
	while ((fgets(line_buffer, LINE_BUFFER_SIZE, config) != NULL)) {
		switch (line_buffer[0]) {
			case '\n':
				/* FALLTHROUGH */
			case ' ':
				/* FALLTHROUGH */
			case '#':
				continue;
		}
		stripnewline(line_buffer);


		ntokens = 0;
		token = strtok(line_buffer, " ");
		while (token != NULL) {
			switch (token[0]) {
			case '~':
				expandtilde(exppath, token, TOKEN_SIZE);
				strncpy(tokens[ntokens], exppath, TOKEN_SIZE);
				/* FALLTHROUGH */
			case '#':
				break;
			default:
				strncpy(tokens[ntokens], token, TOKEN_SIZE);
			}

			/* expand :host to : + hostname */
			if (strstr(token, ":host") != NULL) {
				char hostname[HOSTNAME_SIZE];
				token[strcspn(token, ":")] = '\0';
				gethostname(hostname, HOSTNAME_SIZE);
				strncomb(token, TOKEN_SIZE - 1, ":", hostname, NULL);
				strncpy(tokens[ntokens], token, TOKEN_SIZE);
			}


			token = strtok(NULL, " ");
			ntokens++;
		}

		if (parseline(tokens, ntokens, l) < 0) {
			parseerror(tokens, ntokens, l);
		}
		l++;

		memset(tokens, 0, sizeof(tokens[0][0]) * TOKENS * TOKEN_SIZE);
		memset(line_buffer, 0, LINE_BUFFER_SIZE);
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	FILE *config;
	int option;

	memset(opts.configpath, 0, PATH_SIZE);
	strcpy(opts.configpath, "econf");

	while ((option = getopt(argc, argv, "fvhC:c:")) != -1) {
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
		case 'c':
			memset(opts.configpath, 0, PATH_SIZE);
			strcpy(opts.configpath, optarg);
			break;
		case 'C':
			chdir(optarg);
			break;
		default:
			break;
		}
	}

	if ((config = loadconfig(opts.configpath)) == NULL)
		return -1;

	readconfig(config);

	printf("\n:: symlinked %i objs\n", sts.nsymlinks);
	printf(":: installed %i scripts\n", sts.ninstallscripts);
	printf(":: completed installation with %i failed symlinks\n", sts.nfailedsymlinks);

	fclose(config);
	return 0;
}
