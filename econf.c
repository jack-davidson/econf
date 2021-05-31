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

#define DOT_OR_DOTDOT(name) strncmp(name, "..", 2) & strncmp(name, ".", 1)

#define VERSION "0.3"

#define PATH_SIZE	 512  /* maximum supported path size */
#define LINE_BUFFER_SIZE 512  /* maximum length of line in config file */
#define TOKEN_SIZE	 256  /* maximum length of a config token */
#define TOKENS		 128  /* maximum allowed config tokens */
#define HOSTNAME_SIZE	 48   /* maximum size of hostname */
#define COMMAND_SIZE	 512  /* maximum size of a command */

int parseconfig(char line_tokens[TOKENS][TOKEN_SIZE], int token_index);
int parseargs(int argc, char *argv[]);
int linkfiles(char *dest, char *src);
int linkdir(char *dest, char *src);
int lexconfig(FILE *config);
int rm(char *path);
int install(char *script);
int strtolower(char *s, char *str, size_t size);
void version();
void usage();

/* recursively delete a directory or file */
int
rm(char *path)
{
	char new_path[PATH_SIZE];
	struct stat path_stat;
	struct dirent *entry;
	DIR *src_dir;
	int is_dir;

	lstat(path, &path_stat);
	is_dir = S_ISDIR(path_stat.st_mode);

	if (is_dir) {
		src_dir = opendir(path);
		if (src_dir == NULL){
			fprintf(stderr, "cannot open %s\n", path);
			return 1;
		}
		while ((entry = readdir(src_dir)) != NULL) {
			if (DOT_OR_DOTDOT(entry->d_name)) {
				strncat(new_path, path, PATH_SIZE - 1);
				strncat(new_path, "/", PATH_SIZE - 1);
				strncat(new_path, entry->d_name, PATH_SIZE - 1);
				rm(new_path);
			}
		}
		closedir(src_dir);
	}
	remove(path);
	return 0;
}

/* link a directory src to another directory dest */
int
linkdir(char *dest, char *src)
{
	char dest_dir[PATH_SIZE] = {0};
	char src_dir[PATH_SIZE] = {0};
	char cwd[PATH_SIZE];

	if(access(src, F_OK)) {
		fprintf(stderr, "cannot open directory: %s\n", src);
		return -1;
	}

	getcwd(cwd, PATH_SIZE);

	strncat(src_dir, cwd, PATH_SIZE - 1);
	strncat(src_dir, "/", PATH_SIZE - 1);
	strncat(src_dir, src, PATH_SIZE - 1);

	strncat(dest_dir, dest, PATH_SIZE - 1);
	strncat(dest_dir, "/", PATH_SIZE - 1);
	strncat(dest_dir, src, PATH_SIZE - 1);

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
int
link_dotfiles(char *dest, char *src)
{
	char dest_filename[PATH_SIZE];
	char src_filename[PATH_SIZE];
	struct dirent *entry;
	char cwd[PATH_SIZE];
	DIR *src_dir;

	src_dir = opendir(src);
	if (src_dir == NULL){
		fprintf(stderr, "cannot open directory: %s\n", src);
		return 1;
	}

	while ((entry = readdir(src_dir)) != NULL) {
		/* Exclude '.' and '..' files from source directory */
		if (DOT_OR_DOTDOT(entry->d_name)) {
			getcwd(cwd, PATH_SIZE);

			memset(dest_filename, 0, PATH_SIZE);
			memset(src_filename, 0, PATH_SIZE);

			strncat(dest_filename, dest, PATH_SIZE - 1);
			strncat(dest_filename, "/.", PATH_SIZE - 1);
			strncat(dest_filename, entry->d_name, PATH_SIZE - 1);

			strncat(src_filename, cwd, PATH_SIZE - 1);
			strncat(src_filename, "/", PATH_SIZE - 1);
			strncat(src_filename, src, PATH_SIZE - 1);
			strncat(src_filename, "/", PATH_SIZE - 1);
			strncat(src_filename, entry->d_name, PATH_SIZE - 1);

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

void
usage()
{
	printf("usage: econf [-c config_file] [-C working_directory] [-vh]\n");
}

void
version()
{
	printf("econf-%s\n", VERSION);
}

int strtolower(char *s, char *str, size_t size)
{
	int i;
	for (i = 0; i < size; i++) {
		*(s + i) = tolower(*(str + i));
	}
	return 1;
}

int install(char *script)
{
	char cwd[PATH_SIZE];
	char script_path[PATH_SIZE];
	char confirm[10];

	getcwd(cwd, PATH_SIZE);
	strncat(script_path, cwd, PATH_SIZE - 1);
	strncat(script_path, "/install/", PATH_SIZE - 1);
	strncat(script_path, script, PATH_SIZE - 1);

	printf("execute install script %s? (Potentially Dangerous) [y/N] ", script);
	scanf("%c", confirm);
	strtolower(confirm, confirm, sizeof(confirm));
	if (!strcmp(confirm, "y")) {
		printf("executing install script %s\n", script);
		if (execl(script_path, "", NULL) < 0) {
			printf("failed to execute install script %s\n", script);
		}
	}
	return 0;
}

/* parse main program arguments */
int
parseargs(int argc, char *argv[]) {
	if (argc > 2 && !strcmp(argv[1], "-C"))
		chdir(argv[2]);
	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage();
	if (argc > 1 && !strcmp(argv[1], "-v"))
		version();
	if (argc > 2 && !strcmp(argv[1], "install"))
		install(argv[2]);
	return 0;
}

/* load config file, handle errors and return file descriptor */
FILE *
loadconfig(char *config_filename)
{
	FILE *config;

	if (!access(config_filename, F_OK)) {
		config = fopen(config_filename, "r");
		return config;
	} else {
		return NULL;
	}
}

/* parse tokens of config line, called by lexconfig for each line of config file */
int
parseconfig(char line_tokens[TOKENS][TOKEN_SIZE], int token_index)
{
	char hostname[HOSTNAME_SIZE];
	char command[COMMAND_SIZE];
	int i;

	if (token_index > 3) {
		if (!strcmp(line_tokens[3], "sys")) {
			gethostname(hostname, HOSTNAME_SIZE);
			strncat(line_tokens[1], "-", TOKEN_SIZE - 1);
			strncat(line_tokens[1], hostname, TOKEN_SIZE - 1);
		}
	}

	if (token_index >= 2) {
		if (!strcmp(line_tokens[0], "dir")) {
			linkdir(line_tokens[2], line_tokens[1]);
		}
		if (!strcmp(line_tokens[0], "files")) {
			link_dotfiles(line_tokens[2], line_tokens[1]);
		}
		if (!strcmp(line_tokens[0], "run")) {
			memset(command, 0, COMMAND_SIZE);
			for (i = 1; i < token_index; i++) {
				strncat(command, line_tokens[i], COMMAND_SIZE - 1);
				strncat(command, " ", COMMAND_SIZE - 1);
			}
			system(command);
		}
	}
	return 0;
}

/* tokenize lines of config file and call parseconfig() on each line */
int
lexconfig(FILE *config)
{
	char line_tokens[TOKENS][TOKEN_SIZE];
	char line_buffer[LINE_BUFFER_SIZE];
	char expanded_token[TOKEN_SIZE];
	int token_index;
	char *token;
	int line;
	int err;

	line = 1;
	while ((fgets(line_buffer, LINE_BUFFER_SIZE, config) != NULL)) {
		switch (line_buffer[0]) {
			case '\n':
				continue;
			case '#':
				fgets(line_buffer, LINE_BUFFER_SIZE, config);
				continue;
		}
		line_buffer[strcspn(line_buffer, "\n")] = 0;

		token_index = 0;
		token = strtok(line_buffer, " ");
		while (token != NULL) {
			/* expand ~ to $HOME */
			if (token[0] == '~') {
				memset(expanded_token, 0, TOKEN_SIZE);
				memmove(token, token+1, strlen(token));
				strncat(expanded_token, getenv("HOME"), TOKEN_SIZE - 1);
				strncat(expanded_token, token, TOKEN_SIZE - 1);
				strncpy(line_tokens[token_index], expanded_token, TOKEN_SIZE);
			} else if (token[0] == '#') {
				break;
			} else {
				strncpy(line_tokens[token_index], token, TOKEN_SIZE);
			}
			token = strtok(NULL, " ");
			token_index++;
		}

		if ((err = (parseconfig(line_tokens, token_index))) < 0) {
			printf("failed to execute line %i in config (error code %i)\n",
			       line, err);
			return err;
		}

		memset(line_buffer, 0, LINE_BUFFER_SIZE);
		line++;
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	FILE *config;
	int err;

	if ((err = parseargs(argc, argv)) != 0) {
		printf("failed to parse arguments (error code %i)\n", err);
		usage();
	}

	if ((config = loadconfig("econf")) == NULL) {
		printf("failed to load config file");
		usage();
	}

	if ((err = lexconfig(config)) != 0) {
		printf("failed to lex config (error code %i)\n", err);
		return err;
	}

	fclose(config);
	return 0;
}
