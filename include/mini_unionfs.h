#ifndef MINI_UNIONFS_H
#define MINI_UNIONFS_H

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

/* Global state passed to FUSE */
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* Path resolution constants */
#define WHITEOUT_PREFIX ".wh."
#define MAX_PATH_LEN 4096

/* Helper function prototypes */
int resolve_path(const char *path, char *resolved_path, int *layer);
int is_whiteouted(const char *path);
int copy_file(const char *src, const char *dest);
int ensure_parent_dir(const char *path);
int get_real_path(const char *path, char *real_path);

/* FUSE operation prototypes */
static int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
static int unionfs_open(const char *path, struct fuse_file_info *fi);
static int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int unionfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
static int unionfs_unlink(const char *path);
static int unionfs_mkdir(const char *path, mode_t mode);
static int unionfs_rmdir(const char *path);
static int unionfs_rename(const char *from, const char *to);
static int unionfs_truncate(const char *path, off_t size);
static int unionfs_release(const char *path, struct fuse_file_info *fi);

#endif /* MINI_UNIONFS_H */
