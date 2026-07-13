#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "core/persistence.h"
#include "core/persistence_format.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void set_error(char *error, size_t size, const char *format, ...) {
    if (error == NULL || size == 0U) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static bool fsync_retry(int descriptor) {
    while (fsync(descriptor) != 0) {
        if (errno != EINTR) return false;
    }
    return true;
}

static bool sync_temporary_file(int descriptor) {
#ifdef __APPLE__
    while (fcntl(descriptor, F_FULLFSYNC) != 0) {
        if (errno == EINVAL || errno == ENOTSUP) return fsync_retry(descriptor);
        if (errno != EINTR) return false;
    }
    return true;
#else
    return fsync_retry(descriptor);
#endif
}

bool persistence_load(const char *path, TaskList *list, char *error, size_t error_size) {
    if (error != NULL && error_size > 0U) {
        error[0] = '\0';
    }
    if (path == NULL || list == NULL) {
        set_error(error, error_size, "invalid load arguments");
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            return true;
        }
        set_error(error, error_size, "cannot open state: %s", strerror(errno));
        return false;
    }
    TaskList loaded;
    task_list_init(&loaded);
    bool ok = persistence_format_load(file, &loaded, error, error_size);
    if (fclose(file) != 0 && ok) {
        set_error(error, error_size, "cannot close state: %s", strerror(errno));
        ok = false;
    }
    if (!ok) {
        task_list_free(&loaded);
        return false;
    }
    task_list_free(list);
    *list = loaded;
    return true;
}

static bool ensure_parent(const char *path, char *error, size_t error_size) {
    char copy[PATH_MAX];
    const size_t length = strlen(path);
    if (length == 0U || length >= sizeof(copy)) {
        set_error(error, error_size, "state path is too long");
        return false;
    }
    memcpy(copy, path, length + 1U);
    char *slash = strrchr(copy, '/');
    if (slash == NULL) {
        return true;
    }
    *slash = '\0';
    for (char *cursor = copy + (copy[0] == '/' ? 1 : 0); ; ++cursor) {
        if (*cursor != '/' && *cursor != '\0') {
            continue;
        }
        const char saved = *cursor;
        *cursor = '\0';
        if (copy[0] != '\0' && mkdir(copy, 0700) != 0 && errno != EEXIST) {
            set_error(error, error_size, "cannot create state directory: %s", strerror(errno));
            return false;
        }
        *cursor = saved;
        if (saved == '\0') {
            break;
        }
    }
    return true;
}

bool persistence_lock(const char *path, int *lock_fd, char *error, size_t error_size) {
    if (lock_fd != NULL) *lock_fd = -1;
    if (path == NULL || lock_fd == NULL || !ensure_parent(path, error, error_size)) return false;
    char lock_path[PATH_MAX];
    const int written = snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
    if (written < 0 || (size_t)written >= sizeof(lock_path)) {
        set_error(error, error_size, "state lock path is too long");
        return false;
    }
    /* Refuse symlink and non-regular lock targets so advisory locking stays on our own file. */
    const int descriptor = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) {
        set_error(error, error_size, "cannot open state lock: %s", strerror(errno));
        return false;
    }
    (void)fchmod(descriptor, 0600);
    struct stat status;
    bool locked = false;
    if (fstat(descriptor, &status) != 0) {
        locked = false;
    } else if (!S_ISREG(status.st_mode)) {
        errno = EINVAL;
    } else {
        locked = flock(descriptor, LOCK_EX | LOCK_NB) == 0;
    }
    if (!locked) {
        const int saved_errno = errno;
        (void)close(descriptor);
        set_error(error, error_size, "state is already open or cannot be locked: %s",
                  strerror(saved_errno));
        return false;
    }
    *lock_fd = descriptor;
    return true;
}

void persistence_unlock(int lock_fd) {
    if (lock_fd < 0) return;
    (void)flock(lock_fd, LOCK_UN);
    (void)close(lock_fd);
}

static bool sync_parent_directory(const char *path) {
    const int entry_errno = errno;
    char parent[PATH_MAX];
    const size_t length = strlen(path);
    if (length == 0U || length >= sizeof(parent)) return false;
    memcpy(parent, path, length + 1U);
    char *slash = strrchr(parent, '/');
    if (slash == NULL) {
        (void)snprintf(parent, sizeof(parent), ".");
    } else if (slash == parent) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    const int descriptor = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) return false;
    const bool ok = fsync_retry(descriptor);
    const int saved_errno = errno;
    (void)close(descriptor);
#ifdef __APPLE__
    /* macOS can reject directory fsync with EINVAL; that path cannot establish
       directory-entry durability after the rename. */
    if (!ok && saved_errno == EINVAL) {
        errno = entry_errno;
        return true;
    }
#endif
    if (ok) {
        errno = entry_errno;
        return true;
    }
    errno = saved_errno;
    return false;
}

bool persistence_save(const char *path, const TaskList *list, char *error, size_t error_size) {
    if (error != NULL && error_size > 0U) {
        error[0] = '\0';
    }
    if (path == NULL || list == NULL) {
        set_error(error, error_size, "invalid save arguments");
        return false;
    }
    if (!persistence_format_state_is_valid(list)) {
        set_error(error, error_size, "invalid task list");
        return false;
    }
    if (!ensure_parent(path, error, error_size)) {
        return false;
    }
    char temporary[PATH_MAX];
    const int length = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        set_error(error, error_size, "state path is too long");
        return false;
    }
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        set_error(error, error_size, "cannot create temporary state: %s", strerror(errno));
        return false;
    }
    (void)fchmod(descriptor, 0600);
    FILE *file = fdopen(descriptor, "wb");
    bool ok = file != NULL;
    if (ok) {
        ok = persistence_format_write(file, list) && fflush(file) == 0 && sync_temporary_file(descriptor);
        if (fclose(file) != 0) {
            ok = false;
        }
    } else {
        (void)close(descriptor);
    }
    if (ok) {
        ok = rename(temporary, path) == 0 && sync_parent_directory(path);
    }
    if (!ok) {
        const int saved_errno = errno;
        (void)unlink(temporary);
        set_error(error, error_size, "cannot save state: %s", strerror(saved_errno));
    }
    return ok;
}

bool persistence_default_path(char *output, size_t output_size) {
    if (output == NULL || output_size == 0U) {
        return false;
    }
    const char *base = getenv("XDG_DATA_HOME");
    const char *suffix = "/lowtask/tasks.db";
    char fallback[PATH_MAX];
    if (base == NULL || base[0] != '/') {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] != '/') {
            return false;
        }
        const int fallback_length = snprintf(fallback, sizeof(fallback), "%s/.local/share", home);
        if (fallback_length < 0 || (size_t)fallback_length >= sizeof(fallback)) {
            return false;
        }
        base = fallback;
    }
    const int written = snprintf(output, output_size, "%s%s", base, suffix);
    return written >= 0 && (size_t)written < output_size;
}
