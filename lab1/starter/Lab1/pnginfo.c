#include "lab_png.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include "crc.c"

int is_png(U8 *buf, size_t n){
	if (buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47)
	{
			return 0;
	}
	return -1;
}

int checkCRC(FILE* fp) {
	struct chunk *chunk_info = (struct chunk*)malloc(sizeof (struct chunk));
	fread(&chunk_info->length,sizeof(U32),1,fp);
	printf("%d\n",ntohl(chunk_info->length));
	U8 data;
	for (int i = 0; i < 4; i++) {
		fread(&data,sizeof(U8),1,fp);
		chunk_info->type[i] = data;
	}
	chunk_info->p_data = (U8 *)malloc(ntohl(chunk_info->length)*sizeof(U8));
	fread(chunk_info->p_data,1,ntohl(chunk_info->length),fp);
	fread(&chunk_info->crc,sizeof(U32),1,fp);
	U32 crc_info = crc(chunk_info->p_data,ntohl(chunk_info->length));
   	if (crc_info != ntohl(chunk_info->crc)) {
		printf("%c%c%c%c chunk CRC error: computed %x, expected %x \n",chunk_info->type[0],chunk_info->type[1],chunk_info->type[2],chunk_info->type[3],crc_info,ntohl(chunk_info->crc));
		return -1;
	}
	free(chunk_info);	
	return 0;
}

int main (int argc, char *argv[]) {
		FILE *fp;
		U8 png;
		U8 *buf = (U8*)malloc(sizeof(U8));
		fp=fopen(argv[1],"rb");
		if (fp == NULL) {
			printf("no file");
			return -1;
		}
		for (int i = 0; i < 8; i++) {
			fread(&png,sizeof(U8),1,fp);
			buf[i]=png;
		}

		if (is_png(buf,sizeof(buf)) == 0) {
			free(buf);
			struct data_IHDR *out = (struct data_IHDR*)malloc(sizeof(struct data_IHDR));
			U32 dimensions;
			U8 IHDRinfo;
			fseek(fp,8,SEEK_CUR);
			for (int i = 0; i < 7; i++) {
				if (i < 2) {
						fread(&dimensions, sizeof(U32),1,fp);
						if (i == 0) out->width = ntohl(dimensions);
						if (i == 1) out->height = ntohl(dimensions);
				}
				else {
					fread(&IHDRinfo, sizeof(U8),1,fp);
					if (i == 2) out->bit_depth = IHDRinfo;
					if (i == 3) out->color_type = IHDRinfo;
					if (i == 4) out->compression = IHDRinfo;
					if (i == 5) out->filter = IHDRinfo;
					if (i == 6) out->interlace = IHDRinfo;
				}
			}
			fseek(fp,8,SEEK_SET);
			if (checkCRC(fp) == 0) {
				if (checkCRC(fp) == 0) {
						if (checkCRC(fp) == 0) {
							printf("%s: %d x %d\n", argv[1], out->width, out->height);
						}
				}
			}
		}
		else {
			printf("%s: Not a PNG file\n",argv[1]);
		}
		fclose(fp);
		return 0;
}
		

