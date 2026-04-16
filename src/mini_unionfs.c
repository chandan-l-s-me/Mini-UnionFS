#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#define MAX_PATH_LEN 4096
#define WHITEOUT_PREFIX ".wh."

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* ================= HELPERS ================= */

int resolve_path(const char *path, char *resolved, int *layer) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    struct stat st;

    char upper[MAX_PATH_LEN];
    char lower[MAX_PATH_LEN];

    snprintf(upper, MAX_PATH_LEN, "%s%s", data->upper_dir, path);
    snprintf(lower, MAX_PATH_LEN, "%s%s", data->lower_dir, path);

    /* check whiteout */
    char *base = strrchr(path, '/');
    base = base ? base + 1 : (char *)path;

    char whiteout[MAX_PATH_LEN];
    snprintf(whiteout, MAX_PATH_LEN, "%s/.wh.%s", data->upper_dir, base);

    if (lstat(whiteout, &st) == 0) return -ENOENT;

    if (lstat(upper, &st) == 0) {
        strcpy(resolved, upper);
        *layer = 1;
        return 0;
    }

    if (lstat(lower, &st) == 0) {
        strcpy(resolved, lower);
        *layer = 2;
        return 0;
    }

    return -ENOENT;
}

int ensure_parent(const char *path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char full[MAX_PATH_LEN];

    snprintf(full, MAX_PATH_LEN, "%s%s", data->upper_dir, path);

    char *p = strrchr(full, '/');
    if (!p) return 0;
    *p = '\0';

    mkdir(full, 0755);
    return 0;
}

int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -errno;

    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) return -errno;

    char buf[4096];
    int n;

    while ((n = read(in, buf, sizeof(buf))) > 0)
        write(out, buf, n);

    close(in);
    close(out);
    return 0;
}

/* ================= FUSE OPS ================= */

int unionfs_getattr(const char *path, struct stat *stbuf) {
    char resolved[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved, &layer);
    if (ret != 0) return ret;

    if (lstat(resolved, stbuf) == -1)
        return -errno;

    return 0;
}

int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved[MAX_PATH_LEN];
    char upper[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved, &layer);

    if (ret != 0 && !(fi->flags & O_CREAT))
        return ret;

    if ((fi->flags & (O_WRONLY | O_RDWR)) && layer == 2) {
        snprintf(upper, MAX_PATH_LEN, "%s%s", UNIONFS_DATA->upper_dir, path);
        ensure_parent(path);
        copy_file(resolved, upper);
    }

    return 0;
}

int unionfs_read(const char *path, char *buf, size_t size,
                 off_t offset, struct fuse_file_info *fi) {
    char resolved[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved, &layer);
    if (ret != 0) return ret;

    int fd = open(resolved, O_RDONLY);
    if (fd < 0) return -errno;

    ret = pread(fd, buf, size, offset);
    close(fd);

    return ret;
}

int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi) {
    char resolved[MAX_PATH_LEN];
    char upper[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved, &layer);

    snprintf(upper, MAX_PATH_LEN, "%s%s", UNIONFS_DATA->upper_dir, path);
    ensure_parent(path);

    if (ret != 0 || layer == 2) {
        if (ret == 0)
            copy_file(resolved, upper);

        int fd = open(upper, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) return -errno;

        ret = pwrite(fd, buf, size, offset);
        close(fd);
        return ret;
    }

    int fd = open(resolved, O_WRONLY);
    if (fd < 0) return -errno;

    ret = pwrite(fd, buf, size, offset);
    close(fd);

    return ret;
}

int unionfs_readdir(const char *path, void *buf,
                    fuse_fill_dir_t filler, off_t offset,
                    struct fuse_file_info *fi) {

    char upper[MAX_PATH_LEN];
    char lower[MAX_PATH_LEN];
    DIR *dir;
    struct dirent *entry;
    struct stat st;

    (void) offset;
    (void) fi;

    snprintf(upper, MAX_PATH_LEN, "%s%s", UNIONFS_DATA->upper_dir, path);
    snprintf(lower, MAX_PATH_LEN, "%s%s", UNIONFS_DATA->lower_dir, path);

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    /* upper */
    dir = opendir(upper);
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, WHITEOUT_PREFIX, 4) == 0)
                continue;
            filler(buf, entry->d_name, NULL, 0);
        }
        closedir(dir);
    }

    /* lower */
    dir = opendir(lower);
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {

            char check[MAX_PATH_LEN];
            snprintf(check, MAX_PATH_LEN, "%s/%s", upper, entry->d_name);

            if (lstat(check, &st) == 0) continue;

            char wh[MAX_PATH_LEN];
            snprintf(wh, MAX_PATH_LEN, "%s/.wh.%s", upper, entry->d_name);

            if (lstat(wh, &st) == 0) continue;

            filler(buf, entry->d_name, NULL, 0);
        }
        closedir(dir);
    }

    return 0;
}

int unionfs_unlink(const char *path) {
    char resolved[MAX_PATH_LEN];
    char upper[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved, &layer);
    if (ret != 0) return ret;

    snprintf(upper, MAX_PATH_LEN, "%s%s", UNIONFS_DATA->upper_dir, path);

    if (layer == 1)
        return unlink(upper);

    /* create whiteout */
    char *base = strrchr(path, '/');
    base = base ? base + 1 : (char *)path;

    char wh[MAX_PATH_LEN];
    snprintf(wh, MAX_PATH_LEN, "%s/.wh.%s", UNIONFS_DATA->upper_dir, base);

    int fd = open(wh, O_CREAT, 0644);
    close(fd);

    return 0;
}

int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi) {
    char upper[MAX_PATH_LEN];

    snprintf(upper, MAX_PATH_LEN, "%s%s", UNIONFS_DATA->upper_dir, path);
    ensure_parent(path);

    int fd = open(upper, O_CREAT | O_WRONLY, mode);
    if (fd < 0) return -errno;

    close(fd);
    return 0;
}

/* ================= OPS ================= */

static struct fuse_operations ops = {
    .getattr = unionfs_getattr,
    .open = unionfs_open,
    .read = unionfs_read,
    .write = unionfs_write,
    .readdir = unionfs_readdir,
    .unlink = unionfs_unlink,
    .create = unionfs_create,
};

/* ================= MAIN ================= */

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s lower upper mount\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(*state));

    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    return fuse_main(argc - 2, argv + 2, &ops, state);
}