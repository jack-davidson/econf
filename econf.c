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
typedef char Command[COMMANDBUFSIZE];

static char *expandhostname(char *s);
static int confirm(char *confirm_message, char *item);
static int parseline(Tokens tokens, int ntokens, int l);
static int parseerror(Tokens tokens, int ntokens, int l);
static int linkfiles(char dest[1024], char src[1024]);
static int install(Tokens tokens, int t);
static int linkdir(char dest[1024], char src[1024]);
static int rm(char path[1024]);
static void usage();
static void version();
static char *strtolower(char *s, char *str, size_t size);
static void help();
static FILE *loadconfig(char path[1024]);
static int sh(Tokens tokens, int t);
static char *expandtilde(char dest[1024], char src[1024], int size);
static char *stripnewline(char *s);
static int readconfig(FILE *config);
int main(int argc, char *argv[]);

char configpath[1024];
int isinstall, isforce;
int symlinkstarted, installstarted;
int nfailedsymlinks, ninstallscripts, nsymlinks;

/* expand .*:host to .*:$(hostname), if present, return pointer
 * to position of .*:host. in s */
static char *
expandhostname(char *s)
{
	char *p, *e;

	if ((p = strstr(s, ":host")) != NULL) {
		char hostname[HOSTNAMEBUFSIZE];

		e = s;
		while (*e != ':') {
			++e;
		}
		*(++e) = '\0';

		gethostname(hostname, HOSTNAMEBUFSIZE);
		strncat(s, hostname, 64);
		return p;
	}
	return NULL;
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
	int i, f;

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
linkfiles(char dest[1024], char src[1024])
{
	char dest_filename[4096], src_filename[4096], cwd[1024];

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
			getcwd(cwd, sizeof(cwd));

			*dest_filename = *src_filename = '\0';

			snprintf(dest_filename, sizeof(dest_filename), "%s%s", dest, entry->d_name);
			snprintf(src_filename, sizeof(src_filename), "%s/%s/%s", cwd, src, entry->d_name);

			rm(dest_filename);

			if (!symlink(src_filename, dest_filename)) {
				fprintf(stdout, "\tsymlinking %s -> %s\n",
					src_filename, dest_filename);
				nsymlinks++;
			} else {
				fprintf(stderr,
					"\tsymlink failed: %s -> %s\n",
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
	char cmd[4096];
	char cwd[1024];
	int i;

	printf("\n");

	if (!isinstall)
		return 0;

	if (!installstarted) {
		installstarted = 1;
		printf(":: beginning execution of installation scripts\n\n");
	}

	getcwd(cwd, sizeof(cwd));
	snprintf(cmd, sizeof(cmd), "%s/install/", cwd);

	for (i = 1; i < t; i++) {
		snprintf(cmd, sizeof(cmd), tokens[i], " ", NULL);
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
linkdir(char dest[1024], char src[1024])
{
	char dest_dir[4096], src_dir[4096], cwd[1024];
	char *s;

	s = expandhostname(src);

	*dest_dir = *src_dir = '\0';

	if (!symlinkstarted) {
		symlinkstarted = 1;
		printf(":: starting config symlinking\n\n");
	}

	if(access(src, F_OK)) {
		fprintf(stderr, "cannot open directory: %s\n", src);
		return -1;
	}

	getcwd(cwd, sizeof(cwd));

	snprintf(src_dir, sizeof(src_dir), "%s/%s", cwd, src);

	if (s != NULL)
		*s = '\0';

	snprintf(dest_dir, sizeof(dest_dir), "%s/%s", dest, src);

	rm(dest_dir);

	if(!symlink(src_dir, dest_dir)) {
		fprintf(stdout, "\tsymlinking %s -> %s\n", src_dir, dest_dir);
		nsymlinks++;
	} else {
		fprintf(stderr, "\tsymlink failed: %s -> %s\n", src_dir, dest_dir);
		nfailedsymlinks++;
	}

	return 0;
}

static int
rm(char path[1024])
{
	struct stat path_stat;
	char new_path[1024];
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
				*new_path = '\0';
				snprintf(new_path, sizeof(new_path), "%s/%s", path, entry->d_name);
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
loadconfig(char path[1024])
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
	char cmd[4096];
	int i;

	*cmd = '\0';
	for (i = 1; i < t; i++) {
		snprintf(cmd, sizeof(cmd), "%s ", tokens[i]);
	}

	system(cmd);
	return 0;
}

static char *
expandtilde(char dest[1024], char src[1024], int size)
{
	snprintf(dest, size, "%s%s", getenv("HOME"), ++src);
	return dest;
}

static char *
stripnewline(char *s)
{
	char *p;

	p = s;
	while (*p != '\n') {
		++p;
	}
	*p = '\0';
	return s;
}

static int
readconfig(FILE *config)
{
	Tokens tokens;
	char line_buffer[LINEBUFSIZE];
	char exppath[1024];
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
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	FILE *config;
	int option;

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
