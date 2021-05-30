#include <stdio.h>
#include <string.h>
#include <wordexp.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#define VERSION "0.1"

#define LINE_BUFFER_SIZE 1024

int link_dir(char *dest, char *src);
int link_files(char *dest, char *src);
int rm(char *path);
int parse_args(int argc, char *argv[]);

void usage();
void version();

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

				new_path = malloc(1024);
				snprintf(new_path, 1024, "%s/%s", path, entry->d_name);
				rm(new_path);
				free(new_path);
			}
		}
		closedir(src_dir);
	}
	remove(path);
	return 0;
}

int link_dir(char *dest, char *src)
{
	char *src_dir;
	char *dest_dir;

	char cwd[1024];

	if(access(src, F_OK)) {
		fprintf(stderr, "cannot open directory: %s\n", src);
		return 1;
	}

	getcwd(cwd, 1024);
	src_dir = malloc(1024);
	snprintf(src_dir, 1024, "%s/%s", cwd, src);

	dest_dir = malloc(1024);
	snprintf(dest_dir, 1024, "%s/%s", dest, src);
	rm(dest_dir);

	if(!symlink(src_dir, dest_dir))
		fprintf(stdout, "%s -> %s\n", src_dir, dest_dir);
	else
		fprintf(stderr, "symlink failed: %s -> %s\n", src_dir, dest_dir);

	free(dest_dir);
	free(src_dir);
	return 0;
}

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

			char cwd[1024];

			dest_filename = malloc(1024);
			snprintf(dest_filename, 1024, "%s/%s%s", dest, ".", entry->d_name);

			src_filename = malloc(1024);
			getcwd(cwd, 1024);
			snprintf(src_filename, 1024, "%s/%s/%s", cwd, src, entry->d_name);

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

int parse_args(int argc, char *argv[]) {
	if (argc > 2 && !strcmp(argv[1], "-C"))
		chdir(argv[2]);
	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage();
	if (argc > 1 && !strcmp(argv[1], "-v"))
		version();
}

FILE *load_config(char *config_filename)
{
	FILE *config;

	if (!access(config_filename, F_OK)) {
		config = fopen(config_filename, "r");
		return config;
	} else {
		fprintf(stdout, "config file not found\n");
		usage();
		return NULL;
	}
}

int parse_config(FILE *config)
{
	char line_buffer[LINE_BUFFER_SIZE];

	while ((fgets(line_buffer, LINE_BUFFER_SIZE, config) != NULL)) {
		char line_tokens[64][86];
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
		line_buffer[strcspn(line_buffer, "\n")] = 0;

		token_index = 0;
		token = strtok(line_buffer, " ");
		while (token != NULL) {
			if (token[0] == '~') {
				char new_token[86];
				memmove(token, token+1, strlen(token));
				snprintf(new_token, 86, "%s%s", getenv("HOME"), token);
				strcpy(line_tokens[token_index], new_token);
			} else {
				strcpy(line_tokens[token_index], token);
			}
			token = strtok(NULL, " ");
			token_index++;
		}

		if (token_index > 3) {
			if (!strcmp(line_tokens[3], "sys")) {
				char hostname[64];
				gethostname(hostname, 64);
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
				char command[256] = {0};
				for (int i = 1; i < token_index; i++) {
					strcat(command, line_tokens[i]);
					strcat(command, " ");
				}
				system(command);
			}
		}

		memset(line_buffer, 0, LINE_BUFFER_SIZE);
	}
}

int main(int argc, char *argv[])
{
	FILE *config;
	int err;

	parse_args(argc, argv);

	if ((config = load_config("econf")) == NULL) {
		return 1;
	}

	if ((err = parse_config(config)) != 0) {
		printf("failed to parse config, err %i", err);
		return err;
	}

	fclose(config);
	return 0;
}
