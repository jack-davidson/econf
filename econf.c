#include <stdio.h>
#include <string.h>
#include <wordexp.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#define VERSION "0.3"

#define PATH_SIZE	 1024 /* maximum supported path size */

#define LINE_BUFFER_SIZE 1024 /* maximum length of line in config file */
#define TOKEN_SIZE	 256  /* maximum length of a config token */
#define TOKENS		 128  /* maximum allowed config tokens */

#define HOSTNAME_SIZE	 48   /* maximum size of hostname */
#define COMMAND_SIZE	 512  /* maximum size of a command */

int link_dir(char *dest, char *src);
int link_files(char *dest, char *src);

int rm(char *path);

int parse_args(int argc, char *argv[]);

int parse_config(char line_tokens[TOKENS][TOKEN_SIZE], int token_index);
int lex_config(FILE *config);

void usage();
void version();

/* recursively delete a directory or file */
int rm(char *path)
{
	struct stat path_stat;
	int is_dir;

	lstat(path, &path_stat);

	is_dir = S_ISDIR(path_stat.st_mode);

	if (is_dir) {
		DIR *src_dir;
		struct dirent *entry;

		src_dir = opendir(path);
		if (src_dir == NULL){
			fprintf(stderr, "cannot open %s\n", path);
			return 1;
		}
		while ((entry = readdir(src_dir)) != NULL) {
			if (strcmp(entry->d_name, "..") && strcmp(entry->d_name, ".")) {
				char *new_path;

				new_path = malloc(PATH_SIZE);
				snprintf(new_path, PATH_SIZE, "%s/%s", path, entry->d_name);
				rm(new_path);
				free(new_path);
			}
		}
		closedir(src_dir);
	}
	remove(path);
	return 0;
}

/* link a directory src to another directory dest */
int link_dir(char *dest, char *src)
{
	char *src_dir;
	char *dest_dir;
	char cwd[PATH_SIZE];

	if(access(src, F_OK)) {
		fprintf(stderr, "cannot open directory: %s\n", src);
		return 1;
	}

	src_dir = malloc(PATH_SIZE);
	dest_dir = malloc(PATH_SIZE);

	getcwd(cwd, PATH_SIZE);
	snprintf(src_dir, PATH_SIZE, "%s/%s", cwd, src);
	snprintf(dest_dir, PATH_SIZE, "%s/%s", dest, src);

	rm(dest_dir);

	if(!symlink(src_dir, dest_dir))
		fprintf(stdout, "%s -> %s\n", src_dir, dest_dir);
	else
		fprintf(stderr, "symlink failed: %s -> %s\n", src_dir, dest_dir);

	free(dest_dir);
	free(src_dir);
	return 0;
}

/* link all files within a directory src to another directory dest as hidden files */
int link_dotfiles(char *dest, char *src)
{
	DIR *src_dir;
	struct dirent *entry;

	src_dir = opendir(src);
	if (src_dir == NULL){
		fprintf(stderr, "cannot open directory: %s\n", src);
		return 1;
	}

	while ((entry = readdir(src_dir)) != NULL) {
		/* Exclude '.' and '..' files from source directory */
		if (strcmp(entry->d_name, "..") && strcmp(entry->d_name, ".")) {
			char *dest_filename;
			char *src_filename;
			char cwd[PATH_SIZE];

			dest_filename = malloc(PATH_SIZE);
			src_filename = malloc(PATH_SIZE);

			getcwd(cwd, PATH_SIZE);
			snprintf(dest_filename, PATH_SIZE, "%s/%s%s", dest, ".", entry->d_name);
			snprintf(src_filename, PATH_SIZE, "%s/%s/%s", cwd, src, entry->d_name);

			rm(dest_filename);

			if (!symlink(src_filename, dest_filename))
				fprintf(stdout, "%s -> %s\n", src_filename, dest_filename);
			else
				fprintf(stderr, "symlink failed: %s -> %s\n", src_filename, dest_filename);

			free(dest_filename);
			free(src_filename);
		}
	}

	closedir(src_dir);
	return 0;
}

void usage()
{
	printf("usage: econf [-c config_file] [-C working_directory] [-vh]\n");
}

void version()
{
	printf("econf-%s\n", VERSION);
}

/* parse main program arguments */
int parse_args(int argc, char *argv[]) {
	if (argc > 2 && !strcmp(argv[1], "-C"))
		chdir(argv[2]);
	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage();
	if (argc > 1 && !strcmp(argv[1], "-v"))
		version();
}

/* load config file, handle errors and return file descriptor */
FILE *load_config(char *config_filename)
{
	FILE *config;

	if (!access(config_filename, F_OK)) {
		config = fopen(config_filename, "r");
		return config;
	} else {
		return NULL;
	}
}

/* parse tokens of config line, called by lex_config for each line of config file */
int parse_config(char line_tokens[TOKENS][TOKEN_SIZE], int token_index)
{
	if (token_index > 3) {
		if (!strcmp(line_tokens[3], "sys")) {
			char hostname[HOSTNAME_SIZE];

			gethostname(hostname, HOSTNAME_SIZE);
			strcat(line_tokens[1], "-");
			strcat(line_tokens[1], hostname);
		}
	}

	if (token_index >= 2) {
		if (!strcmp(line_tokens[0], "dir")) {
			link_dir(line_tokens[2], line_tokens[1]);
		}
		if (!strcmp(line_tokens[0], "files")) {
			link_dotfiles(line_tokens[2], line_tokens[1]);
		}
		if (!strcmp(line_tokens[0], "hook")) {
			char command[COMMAND_SIZE] = {0};

			for (int i = 1; i < token_index; i++) {
				strcat(command, line_tokens[i]);
				strcat(command, " ");
			}
			system(command);
		}
	}
	return 0;
}

/* tokenize lines of config file and call parse_config() on each line */
int lex_config(FILE *config)
{
	char line_buffer[LINE_BUFFER_SIZE];
	int line;
	int err;

	line = 1;

	while ((fgets(line_buffer, LINE_BUFFER_SIZE, config) != NULL)) {
		char line_tokens[TOKENS][TOKEN_SIZE];
		char *token;
		int token_index;

		/* ignore lines starting with newline or comment symbol */
		switch (line_buffer[0]) {
			case '\n':
				continue;
			case '#':
				fgets(line_buffer, LINE_BUFFER_SIZE, config);
				continue;
		}

		/* remove trailing newline */
		line_buffer[strcspn(line_buffer, "\n")] = 0;

		/* tokenize config, filter input */
		token_index = 0;
		token = strtok(line_buffer, " ");
		while (token != NULL) {
			/* expand ~ to $HOME */
			if (token[0] == '~') {
				char new_token[TOKEN_SIZE];

				memmove(token, token+1, strlen(token)); /* remove first char in token */
				snprintf(new_token, TOKEN_SIZE, "%s%s", getenv("HOME"), token);
				strcpy(line_tokens[token_index], new_token);
			/* handle comments */
			} else if (token[0] == '#') {
				break;
			/* otherwise copy the token to line_tokens */
			} else {
				strcpy(line_tokens[token_index], token);
			}
			token = strtok(NULL, " ");
			token_index++;
		}

		if (err = (parse_config(line_tokens, token_index)) != 0) {
			printf("failed to execute line %i in config (error code %i)\n", line, err);
			return err;
		}

		memset(line_buffer, 0, LINE_BUFFER_SIZE);
		line++;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	FILE *config;
	int err;

	if ((err = parse_args(argc, argv)) != 0) {
		printf("failed to parse arguments (error code %i)\n", err);
		usage();
	}

	if ((config = load_config("econf")) == NULL) {
		printf("failed to load config file");
		usage();
	}

	if ((err = lex_config(config)) != 0) {
		printf("failed to lex config (error code %i)\n", err);
		return err;
	}

	fclose(config);
	return 0;
}
