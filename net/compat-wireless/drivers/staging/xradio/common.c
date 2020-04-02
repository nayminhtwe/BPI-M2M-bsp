/*
 * Common interfaces for XRadio drivers
 *
 * Copyright (c) 2013, XRadio
 * Author: XRadio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef XRADIO_COMMON_C
#define XRADIO_COMMON_C
#include <linux/init.h>
#include <linux/vmalloc.h>

#include "xradio.h"

struct xr_file *xr_fileopen(const char *path, int open_mode, umode_t mode)
{
	struct xr_file *xr_fp = NULL;
	if (!path) return NULL;

	xr_fp = kmalloc(sizeof(struct xr_file), GFP_ATOMIC);
	if (!xr_fp) {
		xradio_dbg(XRADIO_DBG_ERROR, "%s:kmalloc failed,size=%d!\n", 
		           __func__, sizeof(struct xr_file));
		return NULL;
	}
	memset(xr_fp, 0, sizeof(struct xr_file));

	//open file.
	xr_fp->fp = filp_open(path, open_mode, mode);
	if (IS_ERR(xr_fp->fp)) {
		xradio_dbg(XRADIO_DBG_ERROR, "filp_open failed(%d)\n", (int)xr_fp->fp);
		kfree(xr_fp);
		return NULL;
	}

	//get file size.
	if(xr_fp->fp->f_op->llseek != NULL) {
		mm_segment_t old_fs = get_fs();
		set_fs(KERNEL_DS);
		xr_fp->size = vfs_llseek(xr_fp->fp, 0, SEEK_END);
		vfs_llseek(xr_fp->fp, 0, SEEK_SET);
		set_fs(old_fs);
	} else {
		xr_fp->size = 0;
	}
	return xr_fp;
}

int xr_fileclose(const struct xr_file *xr_fp)
{
	if (xr_fp) {
		if (!IS_ERR(xr_fp->fp))
			filp_close(xr_fp->fp, NULL);
		if (xr_fp->data)
			kfree(xr_fp->data);
		kfree(xr_fp);
		return 0;
	}
	return -1;
}

int xr_fileread(const struct xr_file *xr_fp, char *buffer, int size)
{
	int ret = -1;
	if(xr_fp && buffer && xr_fp->fp->f_op->read != NULL) {
		mm_segment_t old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = vfs_read(xr_fp->fp, buffer, size, &xr_fp->fp->f_pos);
		set_fs(old_fs);
	}
	return ret;
}

int xr_filewrite(const struct xr_file *xr_fp, char *buffer, int size)
{
	int ret = -1;
	if(xr_fp && buffer && xr_fp->fp->f_op->write != NULL) {
		mm_segment_t old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = vfs_write(xr_fp->fp, buffer, size, &xr_fp->fp->f_pos);
		set_fs(old_fs);
	}
	return ret;
}

struct xr_file *xr_request_file(const char *path)
{
	int ret = -1;
	struct xr_file *xr_fp = xr_fileopen(path, O_RDONLY, 0);
	if (!xr_fp) {
		xradio_dbg(XRADIO_DBG_ERROR, "%s:xr_fileopen failed!\n", __func__);
		return NULL;
	}

	//get file data.
	if (xr_fp->size) {
		xr_fp->data = kmalloc(xr_fp->size, GFP_ATOMIC);
		if(xr_fp->data && xr_fp->fp->f_op->read != NULL) {
			mm_segment_t old_fs = get_fs();
			set_fs(KERNEL_DS);
			ret = vfs_read(xr_fp->fp, xr_fp->data, xr_fp->size, &xr_fp->fp->f_pos);
			set_fs(old_fs);
		}
	}
	if (ret < 0) {
		xr_fileclose(xr_fp);
		xr_fp = NULL;
	}
	return xr_fp;
}

/* To use macaddr and ps mode of customers */
int access_file(char *path, char *buffer, int size, int isRead)
{
	int ret=0;
	struct file *fp;
	mm_segment_t old_fs = get_fs();

	if(isRead)
		fp = filp_open(path, O_RDONLY, S_IRUSR);
	else
		fp = filp_open(path, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);

	if (IS_ERR(fp)) {
		if ((int)fp != -ENOENT)
			xradio_dbg(XRADIO_DBG_ERROR, "can't open %s, failed(%d)\n", path, (int)fp);
		return -1;
	}

	if(isRead)
	{
		if(fp->f_op->read == NULL) {
			xradio_dbg(XRADIO_DBG_ERROR, "read %s Not allow.\n", path);
			return -1;
		}
		else {
			fp->f_pos = 0;
			set_fs(KERNEL_DS);
			ret = vfs_read(fp, buffer, size, &fp->f_pos);
			set_fs(old_fs);
		}
	}
	else
	{
		if(fp->f_op->write == NULL) {
			xradio_dbg(XRADIO_DBG_ERROR, "write %s Not allow.\n", path);
			return -1;
		}
		else {
			fp->f_pos = 0;
			set_fs(KERNEL_DS);
			ret = vfs_write(fp, buffer, size, &fp->f_pos);
			set_fs(old_fs);
		}
	}
	filp_close(fp,NULL);

	xradio_dbg(XRADIO_DBG_NIY, "Access_file %s(%d)\n", path, ret);
	return ret;
}

#endif //XRADIO_COMMON_C