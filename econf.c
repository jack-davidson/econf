#include <stdio.h>
#include <string.h>
#include <wordexp.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

int link_dir(char *dest, char *src);
int link_files(char *dest, char *src);
int rm(char *path);

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

	/* rm(dest_dir); */
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
			snprintf(dest_filename, 1024, "%s/%s/%s", dest, ".", entry->d_name);

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

int main(int argc, char *argv[])
{
	char *home;

	if (argc > 2 && !strcmp(argv[1], "-C")) {
		chdir(argv[2]);
	}

	/* TODO: implement config file */
	/* TODO: call this on each field of config file */
	/*
	wordexp_t exp_result;
	wordexp("~/", &exp_result, 0);
	printf("%s\n", exp_result.we_wordv[0]);
	wordfree(&exp_result);
	*/

	link_dotfiles("/home/jd", "zsh");
	link_dir("/home/jd/.config", "dunst");
	return 0;
}
