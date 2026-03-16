/*
 * get_quota.c - Show ZFS user quota and usage for dataset or path.
 *
 * Usage: ./get_quota <dataset|path> <username>
 * Examples:
 *   ./get_quota rpool/ROOT/debian1 foouser
 *   ./get_quota ./mnt/zfs foouser
 *
 * Build: gcc -o get_quota get_quota.c -I/usr/include/libzfs -I/usr/include/libspl -lzfs -lnvpair -luutil
 */

// enables legacy c datatypes which are used in zfs/solaris code
#define _GNU_SOURCE

#define ZFS_SUPER_MAGIC 0x2fc12fc1  /* ZFS Magic on Linux - identifies a zfs partition*/

// some solaris datatypes - needs to be defined before includes are done!
typedef unsigned int   uint_t;
typedef unsigned char  uchar_t;
typedef long long      hrtime_t;

// includes
#include <sys/types.h>
#include <sys/statfs.h>   // needed to be able to detect zfs (not mandatory, but used in the demo code)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libzfs/libzfs.h>  // libzfs - we use this native zfs lib to work with the filesystem


// some helper functions


/*
 * bytes_to_human() - Format bytes as human-readable string (static buffer).
 */
static const char *bytes_to_human(uint64_t bytes)
{
    static char buf[64];
    if (bytes >= 1024ULL*1024*1024*1024) snprintf(buf, sizeof(buf), "%.2f TB", bytes/(1024.0*1024*1024*1024));
    else if (bytes >= 1024ULL*1024*1024) snprintf(buf, sizeof(buf), "%.2f GB", bytes/(1024.0*1024*1024));
    else if (bytes >= 1024ULL*1024) snprintf(buf, sizeof(buf), "%.2f MB", bytes/(1024.0*1024));
    else if (bytes >= 1024) snprintf(buf, sizeof(buf), "%.2f kB", bytes/1024.0);
    else snprintf(buf, sizeof(buf), "%" PRIu64 " B", bytes);
    return buf;
}

/*
 * is_zfs_filesystem() - Check if path is on ZFS via statfs().
 * Returns: 1=ZFS, 0=not ZFS, -1=error
 */
static int is_zfs_filesystem(const char *path, unsigned long *type)
{
    struct statfs st;
    if (statfs(path, &st) != 0) {
        perror("statfs");
        return -1;
    }
    if (type) *type = st.f_type;
    return (st.f_type == ZFS_SUPER_MAGIC);
}

/*
 * is_likely_path() - Detect paths (abs/rel) vs dataset names.
 * Treats ./, ../, /... as paths; others as datasets.
 */
static int is_likely_path(const char *arg)
{
    if (arg[0] == '/') return 1;                    // /abs/path
    if (strncmp(arg, "./", 2) == 0) return 1;      // ./rel/path
    if (strncmp(arg, "../", 3) == 0) return 1;     // ../rel/path
    return 0;  // rpool/dataset → Dataset
}


// main implementation


/*
 * takes 2 arguments: 
 *   path or dataset 
 *   username for which the quota information sould be queried
 */
int main(int argc, char *argv[])
{
    // some local declarations
    libzfs_handle_t *hdl = NULL;
    zfs_handle_t *zhp = NULL;
    char *prop_userused = NULL, *prop_userquota = NULL;
    uint64_t value = 0;
    const char *tmpl_used = "userused@", *tmpl_quota = "userquota@";
    char buf[ZFS_MAXPROPLEN];
    unsigned long fstype;

    // cmd line parsing 
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <dataset|path> <username>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // 2 options: path or dataset
    //     if path, then we need to check if the path is living on a zfs fs

    /* Determine input type and validate. */
    if (is_likely_path(argv[1])) {
        printf("Path-Modus: %s\n", argv[1]);
        switch (is_zfs_filesystem(argv[1], &fstype)) {
        case 1: printf("✓ ZFS filesystem (f_type=0x%lx)\n", fstype); break;
        case 0: fprintf(stderr, "✗ Not ZFS (f_type=0x%lx)\n", fstype); return EXIT_FAILURE;
        case -1: fprintf(stderr, "✗ statfs failed\n"); return EXIT_FAILURE;
        }
    } else {
        printf("Dataset-Modus: %s\n", argv[1]);  // no path (trailing slash) detected - we assume a dataset
    }

    // a prop is a command-string for the zfs library - the same command-string format is used when the zfs command is used 
    //   the following block builds these command-strings from the arguments received from comandline
    //     userused is the command-string to query the storage consumed by a user
    //     userquota is the command-string to query the quoata of the user

    /* Build props: userused@user, userquota@user */
    if (asprintf(&prop_userused, "%s%s", tmpl_used, argv[2]) < 0 ||
        asprintf(&prop_userquota, "%s%s", tmpl_quota, argv[2]) < 0) {
        perror("asprintf");
        goto out; // on error we jump here to cleanup before exiting the program
    }

    // init the libzfs
    hdl = libzfs_init();
    if (!hdl) { fprintf(stderr, "libzfs_init failed\n"); goto out; }

    // get the zhp handle - zhp is the handle which corresponds to the dataset/path. zhp is used for the operations and connects them to the dataset/path.

    /* Open: path first, fallback to dataset name. */
    zhp = zfs_path_to_zhandle(hdl, argv[1], ZFS_TYPE_FILESYSTEM); // if we are confident that a path lives on a zfs, we can use this function to get the corresponding dataset
    if (!zhp) {
        printf("Path failed, trying dataset name...\n");
        zhp = zfs_open(hdl, argv[1], ZFS_TYPE_FILESYSTEM);  // if we know the dataset, we use this function
    }
    if (!zhp) {
        fprintf(stderr, "Cannot open '%s': %s\n", argv[1], libzfs_error_description(hdl));
        goto out; // if no path and no dataset work, we give up
    }

    printf("\nDataset: %s\nUser: %s\n\n", zfs_get_name(zhp), argv[2]);

    // next 2 methods are shown: first is returning the raw bytes, the second a string
    //     - we use the helper functions outlined above to do conversions
    //     - zhp is used - operations will be done on the corresponding datasets

    /* Method 1: Numeric (raw bytes, for scripts or integration to C programs). */
    printf("=== Numeric quota/usage ===\n");
    if (zfs_prop_get_userquota_int(zhp, prop_userquota, &value) == 0)
        printf("Quota: %" PRIu64 " (%s)\n", value, bytes_to_human(value));
    else
        fprintf(stderr, "userquota@%s: %s\n", argv[2], libzfs_error_description(hdl));

    if (zfs_prop_get_userquota_int(zhp, prop_userused, &value) == 0)
        printf("Used:  %" PRIu64 " (%s)\n", value, bytes_to_human(value));
    else
        fprintf(stderr, "userused@%s: %s\n", argv[2], libzfs_error_description(hdl));

    printf("\n");


    /* Method 2: String (human-readable). */
    printf("=== String quota/usage ===\n");
    if (zfs_prop_get_userquota(zhp, prop_userquota, buf, sizeof(buf), B_FALSE) == 0)
        printf("Quota: %s\n", buf);
    else if (errno == ENOENT)
        printf("Quota: no quota set\n");
    else
        fprintf(stderr, "userquota@%s failed\n", argv[2]);

    if (zfs_prop_get_userquota(zhp, prop_userused, buf, sizeof(buf), B_FALSE) == 0)
        printf("Used:  %s\n", buf);
    else if (errno == ENOENT)
        printf("Used:  no quota set\n");
    else
        fprintf(stderr, "userused@%s failed\n", argv[2]);


out:
    if (zhp) zfs_close(zhp);     // close dataset handle
    if (hdl) libzfs_fini(hdl);   // close libzfs handle
    free(prop_userused);         // free memory we have used for the command-string
    free(prop_userquota);        
    return 0;                   // no error
}  // - end

