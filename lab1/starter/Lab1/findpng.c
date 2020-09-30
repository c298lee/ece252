#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lab_png.h"


void searchDir(char* path, int* haspng) {
	DIR *p_dir;
	struct dirent *p_dirent;
	char str[64];

	if ((p_dir = opendir(path)) == NULL) {
		sprintf(str, "opendir(%s)", path);
		perror(str);
		exit(2);	
	}

	while ((p_dirent = readdir(p_dir)) != NULL) {
		char *str_path = p_dirent->d_name;	
		if (str_path == NULL) {
			fprintf(stderr, "Null pointer found!");
			exit(3);
		}
		else {
			char *fullpath = (char*)malloc(1024*sizeof(char));
			memcpy(fullpath, path, strlen(path)+1);

			if (fullpath[strlen(fullpath)-1] != '/') {
				strcat(fullpath, "/");
			}
			strcat(fullpath, str_path);

			char *ptr = (char*)malloc(9*sizeof(char));
			struct stat buf;

			if (lstat(fullpath, &buf) < 0) {
					perror("lstat error");
					continue;
			}

			if (S_ISREG(buf.st_mode)) strcpy(ptr, "regular");
			else if (S_ISDIR(buf.st_mode)) strcpy(ptr, "directory");
			else strcpy(ptr, "unknown");

			if (!strcmp(ptr,"regular")) {
				FILE *fp = fopen(fullpath, "rb");
				if (fp != NULL) {
					U8 png;
					int count = 0;
					U8 *header = (U8*)malloc(4*sizeof(U8));
					for (int i = 0; i < 4; i++) {
						count = fread(&png, sizeof(U8), 1, fp);
						if (count > 0) {
							header[i] = png;
						}	
						else {
							header[i] = 0;
						}
					}

					if (count > 0 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) {
							printf("%s\n",fullpath);
							*haspng = 1;
					}
					free(header);
				}
				fclose(fp);
			}
			else if (!strcmp(ptr,"directory") && strcmp(str_path,".") && strcmp(str_path,"..")) {
				searchDir(fullpath,haspng);
			}
			free(ptr);
			free(fullpath);
		}
	}

	if (closedir(p_dir) != 0) {
		perror("closedir");
		exit(3);
	}	
}

int main(int argc, char *argv[]) {
	if (argc == 1) {
		fprintf(stderr, "Usage: %s <directory name> \n", argv[0]);
		exit(1);
	}
	int haspng = 0;
	searchDir(argv[1],&haspng);
	
	if (haspng == 0) {
		printf("findpng: No PNG file found\n");
	}

	return 0;
}
	
