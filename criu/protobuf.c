#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <ctype.h>

#include <google/protobuf-c/protobuf-c.h>

#include "image.h"
#include "servicefd.h"
#include "common/compiler.h"
#include "log.h"
#include "rst-malloc.h"
#include "string.h"
#include "sockets.h"
#include "cr_options.h"
#include "bfd.h"
#include "protobuf.h"
#include "util.h"

#include "./include/controller.h"

extern struct cr_zns file_to_zone_map;

extern unsigned int meta_zone;
extern unsigned int page_zone;
extern unsigned int next_zone;
char test_data[_192KB];

void zone_write(char *file_name, void* buf, unsigned int size ,int flag) {
	//void* test_data = malloc(_192KB);
	unsigned int zone_num = 0; // zone number to write data
	unsigned int from = 0; // write point in the zone
	unsigned int write_cnt_192 = 0;
	unsigned int written_cnt_192 = 0;
	unsigned int write_remain = 0;
	unsigned int zone_remain_cnt = 0;
	struct _cr_zns* file = NULL;

	if(flag == 0){//0 == meta file
		zone_num = meta_zone;
		//printf("A %u\n",zone_num);
	}
	else{//1 == page file
		zone_num = page_zone;
		//printf("B %u\n",zone_num);
	}
	
	from = zns_get_wp(zone_num); //where to write from wp

	zone_remain_cnt = (((zns_info -> zonef.zsze)-(from%(zns_info -> zonef.zsze))) * 512)/_192KB;
	write_cnt_192 = size/_192KB; // write data size / 192KB
	write_remain = size%_192KB;  // write data size % 192KB
	
	//printf("C %u\n",zone_remain_cnt);

	while((write_cnt_192 > 0) || (write_remain > 0)){
		//-------------------------------------------------------------------
		if(zone_remain_cnt < 1){
				zns_set_zone(zone_num, MAN_CLOSE);
				zone_num = next_zone;
				if(flag == 1)
					page_zone = zone_num;
				else
					meta_zone = zone_num;
				
				if((((next_zone/32)+1)*32) - next_zone> 1)
					next_zone += 1;
				else
					next_zone += 31;

				from = zns_get_wp(zone_num);
				zone_remain_cnt = (((zns_info -> zonef.zsze)-(from%(zns_info -> zonef.zsze))) * 512)/_192KB;
		}
		//-------------------------------------------------------------------
		memset(test_data, '0', _192KB);
		file = cr_zns_search_file(&file_to_zone_map, file_name); //search file
		if(write_cnt_192 > 0){
			//printf("HOOON 0 zone_num : %d, wp : %d, remain : %d, size : %d \n", zone_num, (from*512)/_192KB, zone_remain_cnt, 1);
			memcpy(test_data, buf+written_cnt_192, _192KB);
			write_cnt_192--;
			written_cnt_192++;
			cr_zns_file_write_zone(file, zone_num, from, _192KB);
		}
		else{
			//printf("HOOON 1 zone_num : %d, wp : %d, remain : %d, size : %d \n", zone_num, (from*512)/_192KB, zone_remain_cnt, 1);
			memcpy(test_data, buf+written_cnt_192, write_remain);
			write_remain = 0;
			cr_zns_file_write_zone(file, zone_num, from, write_remain);
		}
		//-------------------------------------------------------------------
		zns_set_zone(zone_num, MAN_OPEN);
		zns_write(test_data, _192KB, zone_num);
		zone_remain_cnt--;
		from = zns_get_wp(zone_num);
		if(zone_remain_cnt < 1){
				zns_set_zone(zone_num, MAN_CLOSE);
				zone_num = next_zone;

				if(flag == 1)
					page_zone = zone_num;
				else
					meta_zone = zone_num;

				if((((next_zone/32)+1)*32) - next_zone > 1)
					next_zone += 1;
				else
					next_zone += 31;

				from = zns_get_wp(zone_num);
				zone_remain_cnt = (((zns_info -> zonef.zsze)-(from%(zns_info -> zonef.zsze))) * 512)/_192KB;
		}
		//-------------------------------------------------------------------
	}
}

#define image_name(img, buf) __image_name(img, buf, sizeof(buf))
static char *__image_name(struct cr_img *img, char *image_path, size_t image_path_size)
{
	int fd = img->_x.fd;

	if (lazy_image(img))
		return img->path;
	else if (empty_image(img))
		return "(empty-image)";
	else if (fd >= 0 && read_fd_link(fd, image_path, image_path_size) > 0)
		return image_path;

	return NULL;
}

/*
 * Reads PB record (header + packed object) from file @fd and unpack
 * it with @unpack procedure to the pointer @pobj
 *
 *  1 on success
 * -1 on error (or EOF met and @eof set to false)
 *  0 on EOF and @eof set to true
 *
 * Don't forget to free memory granted to unpacked object in calling code if needed
 */

int do_pb_read_one(struct cr_img *img, void **pobj, int type, bool eof)
{
	char img_name_buf[PATH_MAX];
	u8 local[PB_PKOBJ_LOCAL_SIZE];
	void *buf = (void *)&local;
	u32 size;
	int ret;

	if (!cr_pb_descs[type].pb_desc) {
		pr_err("Wrong object requested %d on %s\n", type, image_name(img, img_name_buf));
		return -1;
	}

	*pobj = NULL;

	if (unlikely(empty_image(img)))
		ret = 0;
	else
		ret = bread(&img->_x, &size, sizeof(size));
	if (ret == 0) {
		if (eof) {
			return 0;
		} else {
			pr_err("Unexpected EOF on %s\n", image_name(img, img_name_buf));
			return -1;
		}
	} else if (ret < sizeof(size)) {
		pr_perror("Read %d bytes while %d expected on %s", ret, (int)sizeof(size),
			  image_name(img, img_name_buf));
		return -1;
	}

	if (size > sizeof(local)) {
		ret = -1;
		buf = xmalloc(size);
		if (!buf)
			goto err;
	}

	ret = bread(&img->_x, buf, size);
	if (ret < 0) {
		pr_perror("Can't read %d bytes from file %s", size, image_name(img, img_name_buf));
		goto err;
	} else if (ret != size) {
		pr_perror("Read %d bytes while %d expected from %s", ret, size, image_name(img, img_name_buf));
		ret = -1;
		goto err;
	}

	*pobj = cr_pb_descs[type].unpack(NULL, size, buf);
	if (!*pobj) {
		ret = -1;
		pr_err("Failed unpacking object %p from %s\n", pobj, image_name(img, img_name_buf));
		goto err;
	}

	ret = 1;
err:
	if (buf != (void *)&local)
		xfree(buf);

	return ret;
}

/*
 * Writes PB record (header + packed object pointed by @obj)
 * to file @fd, using @getpksize to get packed size and @pack
 * to implement packing
 *
 *  0 on success
 * -1 on error
 */
int pb_write_one(struct cr_img *img, void *obj, int type)
{
	//int nlength = strlen(img->path);
	//char *name = malloc(nlength);
	//strcpy(name, img->path);
	//printf("pb_write_one file_name : %s\n", img->name);
	
	u8 local[PB_PKOBJ_LOCAL_SIZE];
	void *buf = (void *)&local;
	u32 size, packed;
	int ret = -1;
	struct iovec iov[2];
	if (!cr_pb_descs[type].pb_desc) {
		pr_err("Wrong object requested %d\n", type);
		return -1;
	}

	if (lazy_image(img) && open_image_lazy(img))
		return -1;

	size = cr_pb_descs[type].getpksize(obj);
	if (size > (u32)sizeof(local)) {
		buf = xmalloc(size);
		if (!buf)
			goto err;
	}

	packed = cr_pb_descs[type].pack(obj, buf);
	if (packed != size) {
		pr_err("Failed packing PB object %p\n", obj);
		goto err;
	}

	//ZNS WRITE (yejin)
	void *zns_buf = (void*)malloc(size + sizeof(size));
	memcpy(zns_buf, buf, size);
	memcpy(zns_buf+size, &size, sizeof(size));
	
	//zns_format();

	zone_write(img->name, zns_buf, size + sizeof(size),0);
	free(zns_buf);
	
	/*
	iov[0].iov_base = &size;
	iov[0].iov_len = sizeof(size);
	iov[1].iov_base = buf;
	iov[1].iov_len = size;
	//printf("D\n");
	ret = bwritev(&img->_x, iov, 2);
	//printf("E\n");
	if (ret != size + sizeof(size)) {
		pr_perror("Can't write %d bytes", (int)(size + sizeof(size)));
		goto err;
	}
	*/
	//print_zone_desc(31);
	ret = 0;
err:
	if (buf != (void *)&local)
		xfree(buf);
	return ret;
}

int collect_entry(ProtobufCMessage *msg, struct collect_image_info *cinfo)
{
	void *obj;
	void *(*o_alloc)(size_t size) = malloc;
	void (*o_free)(void *ptr) = free;

	if (cinfo->flags & COLLECT_SHARED) {
		o_alloc = shmalloc;
		o_free = shfree_last;
	}

	if (cinfo->priv_size) {
		obj = o_alloc(cinfo->priv_size);
		if (!obj)
			return -1;
	} else
		obj = NULL;

	cinfo->flags |= COLLECT_HAPPENED;
	if (cinfo->collect(obj, msg, NULL) < 0) {
		o_free(obj);
		cr_pb_descs[cinfo->pb_type].free(msg, NULL);
		return -1;
	}

	if (!cinfo->priv_size && !(cinfo->flags & COLLECT_NOFREE))
		cr_pb_descs[cinfo->pb_type].free(msg, NULL);

	return 0;
}

int collect_image(struct collect_image_info *cinfo)
{
	int ret;
	struct cr_img *img;
	void *(*o_alloc)(size_t size) = malloc;
	void (*o_free)(void *ptr) = free;

	pr_info("Collecting %d/%d (flags %x)\n", cinfo->fd_type, cinfo->pb_type, cinfo->flags);

	img = open_image(cinfo->fd_type, O_RSTR);
	if (!img)
		return -1;

	if (cinfo->flags & COLLECT_SHARED) {
		o_alloc = shmalloc;
		o_free = shfree_last;
	}

	while (1) {
		void *obj;
		ProtobufCMessage *msg;

		if (cinfo->priv_size) {
			ret = -1;
			obj = o_alloc(cinfo->priv_size);
			if (!obj)
				break;
		} else
			obj = NULL;

		ret = pb_read_one_eof(img, &msg, cinfo->pb_type);
		if (ret <= 0) {
			o_free(obj);
			break;
		}

		cinfo->flags |= COLLECT_HAPPENED;
		ret = cinfo->collect(obj, msg, img);
		if (ret < 0) {
			o_free(obj);
			cr_pb_descs[cinfo->pb_type].free(msg, NULL);
			break;
		}

		if (!cinfo->priv_size && !(cinfo->flags & COLLECT_NOFREE))
			cr_pb_descs[cinfo->pb_type].free(msg, NULL);
	}

	close_image(img);
	pr_debug(" `- ... done\n");
	return ret;
}
