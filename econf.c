/* See LICENSE file for copyright and license details.
 *
 * econf
 *
 * Automated deployment of configuration.
 *
 */

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

/* return true if name is . or .. */
#define DODD(name) (!(strncmp(name, "..", 2) \
		   & strncmp(name, ".", 1)))

#define PATHSIZE	PATH_MAX + 1
#define LINEBUFSIZE	512
#define TOKENBUFSIZE	256
#define TOKENLEN	128
#define	HOSTNAMEBUFSIZE	48
#define	COMMANDBUFSIZE	512

typedef	char Tokens[TOKENLEN][TOKENBUFSIZE];
typedef char Token[TOKENBUFSIZE];
typedef	char Path[PATHSIZE];
typedef char Command[COMMANDBUFSIZE];

static int confirm(char *confirm_message, char *item);
static int parseline(Tokens tokens, int ntokens, int l);
static int linkfiles(Path dest, Path src);
static int install(Tokens tokens, int t);
static int linkdir(Path dest, Path src);
static int parseerror(Tokens tokens, int ntokens, int l);
static int expandhostname(char *s);
static int readconfig(FILE *config);
static int sh(Tokens tokens, int t);
static int rm(Path path);
static char *strtolower(char *s, char *str, size_t size);
static char *expandtilde(Path dest, Path src, int l);
static char *strncomb(char *s, size_t n, ...);
static char *stripnewline(char *s);
static FILE *loadconfig(Path file);
static void version();
static void usage();
static void help();

int main(int argc, char **argv);

Path configpath;
int isinstall, isforce;
int symlinkstarted, installstarted;
int nfailedsymlinks, ninstallscripts, nsymlinks;

static int
expandhostname(char *s)
{
	if (strstr(s, ":host") != NULL) {
		char hostname[HOSTNAMEBUFSIZE];
		s[strcspn(s, ":")] = '\0';
		gethostname(hostname, HOSTNAMEBUFSIZE);
		strncomb(s, sizeof(Token), ":", hostname, NULL);
	}
	return 0;
}

static int
confirm(char *confirm_message, char *item)
{
	char confirm[10];
	char *message_format;

	if (isforce)
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

static int
parseline(Tokens tokens, int ntokens, int l)
{
	if (ntokens == 1) {
		if (!(strcmp(tokens[0], "noconfirm"))) {
			isforce = 1;
			return 0;
		}
		if (!(strcmp(tokens[0], "confirm"))) {
			isforce = 0;
			return 0;
		}
	} if (ntokens == 3) {
		if (!strcmp(tokens[0], "files"))
			return linkfiles(tokens[2], tokens[1]);
		else if (!strcmp(tokens[0], "dir")) {
			return linkdir(tokens[2], tokens[1]);
		}
	} if (!strcmp(tokens[0], "sh"))
		return sh(tokens, ntokens);
	if (!strcmp(tokens[0], "install"))
		return install(tokens, ntokens);
	return -1;
}

static int
parseerror(Tokens tokens, int ntokens, int l)
{
	int f;
	int i;

	f = isforce;
	isforce = 0;

	printf("failed to parse line %i:\n\tunrecognized tokens: \"", l);
	for (i = 0; i < ntokens; i++) {
		printf("%s ", tokens[i]);
	}
	printf("\"\n");
	if (!confirm("\ncontinue with error", NULL))
		exit(1);
	printf("\n");

	isforce = f;
	return 0;
}

/* link all files within a directory src to another directory dest as hidden files */
static int
linkfiles(Path dest, Path src)
{
	Path dest_filename, src_filename, cwd;

	struct dirent *entry;
	DIR *src_dir;

	expandhostname(src);

	if (!symlinkstarted) {
		symlinkstarted = 1;
		printf(":: starting config symlinking\n\n");
	}

	src_dir = opendir(src);
	if (src_dir == NULL){
		fprintf(stderr, "cannot open directory: %s\n", src);
		return -1;
	}

	while ((entry = readdir(src_dir)) != NULL) {
		if (!DODD(entry->d_name)) {
			getcwd(cwd, sizeof(Path));

			memset(dest_filename, 0, sizeof(Path));
			memset(src_filename, 0, sizeof(Path));

			strncomb(dest_filename, sizeof(Path),
				dest, "", entry->d_name, NULL);
			strncomb(src_filename, sizeof(Path),
				cwd, "/", src, "/", entry->d_name, NULL);

			rm(dest_filename);
			if (!symlink(src_filename, dest_filename)) {
				fprintf(stdout, "    symlinking %s -> %s\n",
					src_filename, dest_filename);
				nsymlinks++;
			} else {
				fprintf(stderr,
					"    symlink failed: %s -> %s\n",
					src_filename, dest_filename);
				nfailedsymlinks++;
			}

		}
	}

	closedir(src_dir);
	return 0;
}

static int
install(Tokens tokens, int t)
{
	Command cmd;
	Path cwd;
	int i;

	printf("\n");

	if (!isinstall)
		return 0;

	if (!installstarted) {
		installstarted = 1;
		printf(":: beginning execution of installation scripts\n\n");
	}

	getcwd(cwd, sizeof(Path));
	strncomb(cmd, sizeof(Path), cwd, "/install/", NULL);

	for (i = 1; i < t; i++) {
		strncomb(cmd, sizeof(Command), tokens[i], " ", NULL);
	}
	printf("    installing %s %s\n", tokens[1], *(tokens + 2));
	if (confirm("\n:: continue with installation", NULL)) {
		system(cmd);
		ninstallscripts++;
	} else {
		printf("installation aborted\n");
	}
	return 0;
}

static int
linkdir(Path dest, Path src)
{
	Path dest_dir;
	Path src_dir;
	Path cwd;
	char *s;

	expandhostname(src);

	memset(dest_dir, 0, sizeof(Path));
	memset(src_dir, 0, sizeof(Path));

	if (!symlinkstarted) {
		symlinkstarted = 1;
		printf(":: starting config symlinking\n\n");
	}

	if(access(src, F_OK)) {
		fprintf(stderr, "cannot open directory: %s\n", src);
		return -1;
	}

	getcwd(cwd, sizeof(Path));

	strncomb(src_dir, sizeof(Path), cwd, "/", src, NULL);

	if ((s = strstr(src, ":")) != NULL) {
		*s = '\0';
	}

	strncomb(dest_dir, sizeof(Path), dest, "/", src, NULL);

	rm(dest_dir);

	if(!symlink(src_dir, dest_dir)) {
		fprintf(stdout, "    symlinking %s -> %s\n",
			src_dir, dest_dir);
		nsymlinks++;
	} else {
		fprintf(stderr, "    symlink failed: %s -> %s\n",
			src_dir, dest_dir);
		nfailedsymlinks++;
	}

	return 0;
}

static char *
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

static int
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
			if (!DODD(entry->d_name)) {
				Path new_path;
				strncomb(new_path, sizeof(Path) - 1,
					path, "/", entry->d_name, NULL);
				rm(new_path);
			}
		}
		closedir(src_dir);
	}
	remove(path);
	return 0;
}

static void
usage()
{
	fprintf(stdout, "usage: econf [-fihv] [-c path] [-C directory]\n");
}

static void
version()
{
	fprintf(stdout, "econf-"VERSION"\n");
}

static char *
strtolower(char *s, char *str, size_t size)
{
	int i;
	for (i = 0; i < size; i++) {
		s[i] = tolower(s[i]);
	}
	return s;
}

static void
help()
{
	usage();
	fprintf(stdout, "\nOPTIONS:\n"
	       "-v: version\n"
	       "-h: help\n"
	       "-f: isforce\n"
	       "-i: run installation scripts\n"
	       "-C <directory>: set working directory\n"
	       "-c <file>: use file for config\n\n"
	);
}

static FILE *
loadconfig(Path path)
{
	if (!access(path, F_OK)) {
		FILE *fd;
		fd = fopen(path, "r");
		return fd;
	} else {
		fprintf(stdout, "failed to load config file\n");
		usage();
		return NULL;
	}
}

static int
sh(Tokens tokens, int t)
{
	Command cmd;
	int i;

	memset(cmd, 0, sizeof(Command));
	for (i = 1; i < t; i++) {
		strncomb(cmd, sizeof(Command), tokens[i], " ", NULL);
	}

	system(cmd);
	return 0;
}

static char *
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

static char *
stripnewline(char *s)
{
	s[strcspn(s, "\n")] = 0;
	return s;
}

static int
readconfig(FILE *config)
{
	Tokens tokens;
	char line_buffer[LINEBUFSIZE];
	Path exppath;
	int ntokens;
	char *token;
	int l;

	l = 1;
	fprintf(stdout, ":: beginning configuration...\n\n");
	while ((fgets(line_buffer, LINEBUFSIZE, config) != NULL)) {
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
				expandtilde(exppath, token, sizeof(Token));
				strncpy(tokens[ntokens], exppath, sizeof(Token));
				/* FALLTHROUGH */
			case '#':
				break;
			default:
				strncpy(tokens[ntokens], token, sizeof(Token));
			}


			token = strtok(NULL, " ");
			ntokens++;
		}

		if (parseline(tokens, ntokens, l) < 0) {
			parseerror(tokens, ntokens, l);
		}
		l++;

		memset(tokens, 0, sizeof(Tokens));
		memset(line_buffer, 0, LINEBUFSIZE);
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	FILE *config;
	int option;

	memset(configpath, 0, sizeof(Path));
	strcpy(configpath, "econf");

	while ((option = getopt(argc, argv, "fvhiC:c:")) != -1) {
		switch (option) {
		case 'f':
			isforce = 1;
			break;
		case 'v':
			version();
			exit(0);
			break;
		case 'h':
			help();
			exit(0);
			break;
		case 'i':
			isinstall = 1;
			break;
		case 'c':
			memset(configpath, 0, sizeof(Path));
			strcpy(configpath, optarg);
			break;
		case 'C':
			chdir(optarg);
			break;
		default:
			break;
		}
	}

	if ((config = loadconfig(configpath)) == NULL)
		return -1;

	readconfig(config);

	printf("\n:: symlinked %i objs\n", nsymlinks);
	printf(":: installed %i scripts\n", ninstallscripts);
	printf(":: completed installation with %i failed symlinks\n", nfailedsymlinks);

	fclose(config);
	return 0;
}
