#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>

/* Global state */
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)
#define WHITEOUT_PREFIX ".wh."
#define MAX_PATH_LEN 4096

/* ======================== Helper Functions ======================== */

/**
 * Resolve the actual path and determine which layer the file is in.
 * Returns: 0 on success, -errno on error
 * layer: 1 = upper, 2 = lower, 0 = not found or whiteouted
 */
int resolve_path(const char *path, char *resolved_path, int *layer) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    struct stat st;
    char upper_path[MAX_PATH_LEN];
    char lower_path[MAX_PATH_LEN];
    char whiteout_path[MAX_PATH_LEN];

    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    /* Build paths */
    snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);
    snprintf(lower_path, MAX_PATH_LEN, "%s%s", data->lower_dir, path);

    /* Check for whiteout file in upper dir */
    const char *basename = strrchr(path, '/');
    if (basename == NULL) basename = path;
    else basename++;

    snprintf(whiteout_path, MAX_PATH_LEN, "%s/%s%s", 
             dirname(upper_path), WHITEOUT_PREFIX, basename);

    if (lstat(whiteout_path, &st) == 0) {
        /* Whiteout file exists - file is deleted */
        *layer = 0;
        return -ENOENT;
    }

    /* Check upper dir first (takes precedence) */
    if (lstat(upper_path, &st) == 0) {
        strcpy(resolved_path, upper_path);
        *layer = 1;
        return 0;
    }

    /* Check lower dir */
    if (lstat(lower_path, &st) == 0) {
        strcpy(resolved_path, lower_path);
        *layer = 2;
        return 0;
    }

    *layer = 0;
    return -ENOENT;
}

/**
 * Check if a file is whiteouted
 */
int is_whiteouted(const char *path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    struct stat st;
    char whiteout_path[MAX_PATH_LEN];
    
    const char *basename = strrchr(path, '/');
    if (basename == NULL) basename = path;
    else basename++;

    char parent_dir[MAX_PATH_LEN];
    strcpy(parent_dir, path);
    char *parent = dirname(parent_dir);

    snprintf(whiteout_path, MAX_PATH_LEN, "%s/%s%s",
             (parent[0] == '/' && parent[1] == '\0') ? 
             data->upper_dir : data->upper_dir,
             WHITEOUT_PREFIX, basename);

    return (lstat(whiteout_path, &st) == 0) ? 1 : 0;
}

/**
 * Ensure all parent directories exist in upper_dir
 */
int ensure_parent_dir(const char *path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char parent_upper[MAX_PATH_LEN];
    char *p;
    struct stat st;

    /* Extract parent directory from path */
    strcpy(parent_upper, data->upper_dir);
    strcat(parent_upper, path);
    
    p = strrchr(parent_upper, '/');
    if (p == NULL) return 0;
    *p = '\0';

    /* Check if parent exists */
    if (lstat(parent_upper, &st) == 0) {
        return 0;
    }

    /* Recursively create parent directories */
    int ret = mkdir(parent_upper, 0755);
    if (ret == -1 && errno != EEXIST) {
        return -errno;
    }

    return 0;
}

/**
 * Copy file from source to destination (for Copy-on-Write)
 */
int copy_file(const char *src, const char *dest) {
    int src_fd, dest_fd;
    char buf[4096];
    ssize_t bytes;
    struct stat st;

    /* Get source file stats */
    if (stat(src, &st) == -1) {
        return -errno;
    }

    /* Ensure parent directory exists */
    char *dest_copy = strdup(dest);
    char *dest_dir = dirname(dest_copy);
    struct stat dir_st;
    if (lstat(dest_dir, &dir_st) != 0) {
        mkdir(dest_dir, 0755);
    }
    free(dest_copy);

    /* Open source */
    src_fd = open(src, O_RDONLY);
    if (src_fd == -1) {
        return -errno;
    }

    /* Create destination */
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (dest_fd == -1) {
        close(src_fd);
        return -errno;
    }

    /* Copy data */
    while ((bytes = read(src_fd, buf, sizeof(buf))) > 0) {
        if (write(dest_fd, buf, bytes) != bytes) {
            close(src_fd);
            close(dest_fd);
            unlink(dest);
            return -EIO;
        }
    }

    close(src_fd);
    close(dest_fd);

    return (bytes == 0) ? 0 : -errno;
}

/* ======================== FUSE Operations ======================== */

static int unionfs_getattr(const char *path, struct stat *stbuf,
                           struct fuse_file_info *fi) {
    char resolved_path[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved_path, &layer);
    if (ret != 0) {
        return ret;
    }

    ret = lstat(resolved_path, stbuf);
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

static int unionfs_open(const char *path, struct fuse_file_info *fi) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[MAX_PATH_LEN];
    char upper_path[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved_path, &layer);
    if (ret != 0) {
        /* File doesn't exist - only allow write modes for creation */
        if (fi->flags & O_CREAT) {
            return 0;
        }
        return ret;
    }

    /* If opening for writing and file is in lower_dir, trigger CoW */
    if ((fi->flags & (O_WRONLY | O_RDWR | O_APPEND)) && layer == 2) {
        snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);
        
        /* Ensure parent directory exists */
        int parent_ret = ensure_parent_dir(path);
        if (parent_ret != 0) {
            return parent_ret;
        }

        /* Copy file from lower to upper */
        int copy_ret = copy_file(resolved_path, upper_path);
        if (copy_ret != 0) {
            return copy_ret;
        }

        /* Update resolved path for subsequent operations */
        strcpy(resolved_path, upper_path);
    }

    return 0;
}

static int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
    char resolved_path[MAX_PATH_LEN];
    int layer;
    int fd;
    int ret;

    ret = resolve_path(path, resolved_path, &layer);
    if (ret != 0) {
        return ret;
    }

    fd = open(resolved_path, O_RDONLY);
    if (fd == -1) {
        return -errno;
    }

    ret = pread(fd, buf, size, offset);
    close(fd);

    return (ret == -1) ? -errno : ret;
}

static int unionfs_write(const char *path, const char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[MAX_PATH_LEN];
    char upper_path[MAX_PATH_LEN];
    int layer;
    int fd;
    int ret;

    /* Check current state */
    int resolve_ret = resolve_path(path, resolved_path, &layer);
    
    /* If file doesn't exist, we're creating it in upper */
    if (resolve_ret != 0) {
        snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);
        ensure_parent_dir(path);
        fd = open(upper_path, O_WRONLY | O_CREAT, 0644);
        if (fd == -1) {
            return -errno;
        }
    } 
    /* If file is in lower_dir, trigger CoW */
    else if (layer == 2) {
        snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);
        ensure_parent_dir(path);
        copy_file(resolved_path, upper_path);
        fd = open(upper_path, O_WRONLY);
        if (fd == -1) {
            return -errno;
        }
    } 
    /* File is in upper_dir */
    else {
        fd = open(resolved_path, O_WRONLY);
        if (fd == -1) {
            return -errno;
        }
    }

    ret = pwrite(fd, buf, size, offset);
    close(fd);

    return (ret == -1) ? -errno : ret;
}

static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char upper_path[MAX_PATH_LEN];
    char lower_path[MAX_PATH_LEN];
    DIR *upper_dir = NULL;
    DIR *lower_dir = NULL;
    struct dirent *entry;

    snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);
    snprintf(lower_path, MAX_PATH_LEN, "%s%s", data->lower_dir, path);

    /* List entries from both directories, avoiding duplicates */
    /* We need to merge the directories and handle whiteouts */
    
    if (strcmp(path, "/") == 0) {
        snprintf(upper_path, MAX_PATH_LEN, "%s", data->upper_dir);
        snprintf(lower_path, MAX_PATH_LEN, "%s", data->lower_dir);
    }

    upper_dir = opendir(upper_path);
    lower_dir = opendir(lower_path);

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    /* List from upper */
    if (upper_dir != NULL) {
        while ((entry = readdir(upper_dir)) != NULL) {
            if (entry->d_name[0] == '\0') continue;

            /* Skip whiteout files */
            if (strncmp(entry->d_name, WHITEOUT_PREFIX, strlen(WHITEOUT_PREFIX)) == 0) {
                continue;
            }

            filler(buf, entry->d_name, NULL, 0);
        }
        closedir(upper_dir);
    }

    /* List from lower, avoiding duplicates */
    if (lower_dir != NULL) {
        while ((entry = readdir(lower_dir)) != NULL) {
            if (entry->d_name[0] == '\0') continue;

            /* Check if entry exists in upper_dir or is whiteouted */
            char check_upper[MAX_PATH_LEN];
            struct stat st;
            
            snprintf(check_upper, MAX_PATH_LEN, "%s/%s", upper_path, entry->d_name);
            
            /* Skip if exists in upper */
            if (lstat(check_upper, &st) == 0) {
                continue;
            }

            /* Skip if whiteouted */
            char whiteout_check[MAX_PATH_LEN];
            snprintf(whiteout_check, MAX_PATH_LEN, "%s/%s%s", 
                     upper_path, WHITEOUT_PREFIX, entry->d_name);
            if (lstat(whiteout_check, &st) == 0) {
                continue;
            }

            filler(buf, entry->d_name, NULL, 0);
        }
        closedir(lower_dir);
    }

    return 0;
}

static int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char upper_path[MAX_PATH_LEN];
    int fd;

    snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);

    /* Ensure parent directory exists */
    int ret = ensure_parent_dir(path);
    if (ret != 0) {
        return ret;
    }

    fd = open(upper_path, O_WRONLY | O_CREAT | O_EXCL, mode);
    if (fd == -1) {
        return -errno;
    }

    close(fd);
    return 0;
}

static int unionfs_unlink(const char *path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[MAX_PATH_LEN];
    char upper_path[MAX_PATH_LEN];
    char whiteout_path[MAX_PATH_LEN];
    int layer;
    int ret;

    ret = resolve_path(path, resolved_path, &layer);
    if (ret != 0) {
        return ret;
    }

    /* File is in upper_dir - just unlink it */
    if (layer == 1) {
        snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);
        return (unlink(upper_path) == -1) ? -errno : 0;
    }

    /* File is in lower_dir - create whiteout file */
    if (layer == 2) {
        snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);
        
        /* Ensure parent directory exists */
        int parent_ret = ensure_parent_dir(path);
        if (parent_ret != 0) {
            return parent_ret;
        }

        /* Create whiteout file */
        const char *basename = strrchr(path, '/');
        if (basename == NULL) basename = path;
        else basename++;

        char parent_upper[MAX_PATH_LEN];
        strcpy(parent_upper, upper_path);
        char *parent_ptr = dirname(parent_upper);
        
        snprintf(whiteout_path, MAX_PATH_LEN, "%s/%s%s",
                 parent_ptr, WHITEOUT_PREFIX, basename);

        int fd = open(whiteout_path, O_WRONLY | O_CREAT, 0644);
        if (fd == -1) {
            return -errno;
        }
        close(fd);
        return 0;
    }

    return -ENOENT;
}

static int unionfs_mkdir(const char *path, mode_t mode) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char upper_path[MAX_PATH_LEN];
    char resolved_path[MAX_PATH_LEN];
    int layer;

    snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);

    /* Ensure parent directory exists */
    int ret = ensure_parent_dir(path);
    if (ret != 0) {
        return ret;
    }

    /* Check if directory already exists */
    if (resolve_path(path, resolved_path, &layer) == 0) {
        struct stat st;
        if (lstat(resolved_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return -EEXIST;
        }
    }

    return (mkdir(upper_path, mode) == -1) ? -errno : 0;
}

static int unionfs_rmdir(const char *path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[MAX_PATH_LEN];
    char upper_path[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved_path, &layer);
    if (ret != 0) {
        return ret;
    }

    snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);

    /* Directory must be in upper to be removed (or create whiteout) */
    if (layer == 1) {
        return (rmdir(upper_path) == -1) ? -errno : 0;
    }

    /* If in lower, a whiteout is complex for directories - allow removal of empty upper */
    if (layer == 2) {
        /* For now, treat like unlink - create a whiteout marker */
        return unionfs_unlink(path);
    }

    return -ENOENT;
}

static int unionfs_rename(const char *from, const char *to) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char from_resolved[MAX_PATH_LEN];
    char to_upper[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(from, from_resolved, &layer);
    if (ret != 0) {
        return ret;
    }

    snprintf(to_upper, MAX_PATH_LEN, "%s%s", data->upper_dir, to);

    /* If from is in lower, copy to upper first (CoW) */
    if (layer == 2) {
        ensure_parent_dir(to);
        copy_file(from_resolved, to_upper);
    } else if (layer == 1) {
        /* Rename within upper */
        char from_upper[MAX_PATH_LEN];
        snprintf(from_upper, MAX_PATH_LEN, "%s%s", data->upper_dir, from);
        return (rename(from_upper, to_upper) == -1) ? -errno : 0;
    }

    return 0;
}

static int unionfs_truncate(const char *path, off_t size) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[MAX_PATH_LEN];
    char upper_path[MAX_PATH_LEN];
    int layer;

    int ret = resolve_path(path, resolved_path, &layer);
    if (ret != 0) {
        return ret;
    }

    snprintf(upper_path, MAX_PATH_LEN, "%s%s", data->upper_dir, path);

    /* If in lower, trigger CoW */
    if (layer == 2) {
        ensure_parent_dir(path);
        copy_file(resolved_path, upper_path);
    }

    return (truncate(upper_path, size) == -1) ? -errno : 0;
}

static int unionfs_release(const char *path, struct fuse_file_info *fi) {
    (void) path;
    (void) fi;
    return 0;
}

/* ======================== FUSE Operations Table ======================== */

static const struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .open = unionfs_open,
    .read = unionfs_read,
    .write = unionfs_write,
    .readdir = unionfs_readdir,
    .create = unionfs_create,
    .unlink = unionfs_unlink,
    .mkdir = unionfs_mkdir,
    .rmdir = unionfs_rmdir,
    .rename = unionfs_rename,
    .truncate = unionfs_truncate,
    .release = unionfs_release,
};

/* ======================== Main ======================== */

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mount_point> [fuse_options]\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));
    if (!state) {
        perror("malloc");
        return 1;
    }

    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    if (!state->lower_dir || !state->upper_dir) {
        perror("realpath");
        return 1;
    }

    printf("Mini-UnionFS mounted:\n");
    printf("  Lower (read-only): %s\n", state->lower_dir);
    printf("  Upper (read-write): %s\n", state->upper_dir);
    printf("  Mount point: %s\n", argv[3]);

    /* Prepare FUSE arguments */
    int new_argc = argc - 2;
    char **new_argv = malloc((new_argc + 1) * sizeof(char *));
    new_argv[0] = argv[0];
    new_argv[1] = argv[3];
    for (int i = 2; i < argc - 1; i++) {
        new_argv[i] = argv[i + 2];
    }
    new_argv[new_argc] = NULL;

    return fuse_main(new_argc, new_argv, &unionfs_oper, state);
}
