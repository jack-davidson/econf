#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <ftw.h>

int link_dir(char *dest, char *src);
int link_files(char *dest, char *src);

int link_dir(char *dest, char *src)
{
	char *src_dir;
	char *dest_dir;

	char cwd[1024];
	getcwd(cwd, 1024);
	src_dir = malloc(1024);
	snprintf(src_dir, 1024, "%s/%s", cwd, src);

	dest_dir = malloc(1024);
	snprintf(dest_dir, 1024, "%s/%s", dest, src);

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
	struct dirent *files;

	src_dir = opendir(src);
	if (src_dir == NULL){
		fprintf(stderr, "(link_dotfiles(%s, %s)), cannot open %s\n", dest, src, src);
		return 1;
	}
	while ((files = readdir(src_dir)) != NULL) {
		/* Exclude '.' and '..' files from source directory */
		if (strcmp(files->d_name, "..") && strcmp(files->d_name, ".")) {
			char *dest_filename;
			char *src_filename;

			dest_filename = malloc(1024);
			snprintf(dest_filename, 1024, "%s%s%s", dest, ".", files->d_name);

			src_filename = malloc(1024);
			char cwd[1024];
			getcwd(cwd, 1024);
			snprintf(src_filename, 1024, "%s/%s/%s", cwd, src, files->d_name);

			remove(dest_filename);

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
	if (argc > 2 && !strcmp(argv[1], "-C")) {
		chdir(argv[2]);
	}

	link_dotfiles("/home/jd/", "zsh");
	link_dir("/home/jd/.config", "dunst");
	return 0;
}
