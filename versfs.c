/**
 * \file versfs.c
 * \date November 2020
 * \author Author of the underlying mirrorfs code (pass-through file system): Scott F. Kaplan <sfkaplan@amherst.edu>; modified into a versioning file system by Sergei Bondarenko
 * 
 * A user-level file system that maintains, within the storage directory, a
 * versioned history of each file in the mount point.
 *
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 * Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>
 *
 * This program can be distributed under the terms of the GNU GPL.
 */

#define FUSE_USE_VERSION 26

#include<stdlib.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>



#endif

static char* storage_dir = NULL;
static char  storage_path[256];

//static char* ver = malloc(3);




char* prepend_storage_dir (char* pre_path, const char* path) {
  strcpy(pre_path, storage_dir);
  strcat(pre_path, path);
  return pre_path;
}


// copy routine by user 'caf':
/*
https://stackoverflow.com/questions/2180079/how-can-i-copy-a-file-on-unix-using-c
*/
int cp(const char *to, const char *from)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

        /* Success! */
        return 0;
    }

  out_error:
    saved_errno = errno;

    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);

    errno = saved_errno;
    return -1;
}
// end of caf's copy routine



// My backup helper function
static int vers_backup(char *path) {

	path = prepend_storage_dir(storage_path, path);
	// Init start var to -999 for easy error triage if it doesn't get updated properly
	int start = -999;
	int res;
	
	// Open metadata
	char *filename_v = malloc(strlen(path) + 3);
			sprintf(filename_v, "%s,v", path);
			res = open(filename_v, O_RDWR, 0666);
			if (res >= 0){
			// Read the latest version
				read(res, &start, 4);
				// Increment it
				start += 1;
				// And save the incremented version number
				pwrite(res, &start, 4, 0); // using pwrite() at offset 0 because write()'s pointer has been moved to EOF by read(), and write() doesn't allow 0 offset/rewind
				res = close(res);
				}
	
	// Generate the backup file name/path
	char *filename_to = malloc(strlen(path) + 6);
	sprintf(filename_to, "%s,%d", path, start);
	

	// Copy current file to that path/name
	res = cp(filename_to, path);
	
	if (res >= 0){
		return res;
	}
	
	return -1;
	

}







static int vers_getattr(const char *path, struct stat *stbuf)
{
	int res;
	
	path = prepend_storage_dir(storage_path, path);
	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_access(const char *path, int mask)
{
	int res;

	path = prepend_storage_dir(storage_path, path);
	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_readlink(const char *path, char *buf, size_t size)
{
	int res;

	path = prepend_storage_dir(storage_path, path);
	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}



static int vers_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

	path = prepend_storage_dir(storage_path, path);
	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		// My bypass to hide metadata and backup files
		// Name extraction
		char search[strlen(de->d_name)];
		strcpy(search, de->d_name);
		// Pattern matching to filter service files
		if ( (search[strlen(search)-2] != ',') && (search[strlen(search)-3] != ',')){
			struct stat st;
			memset(&st, 0, sizeof(st));
			st.st_ino = de->d_ino;
			st.st_mode = de->d_type << 12;
			if (filler(buf, de->d_name, &st, 0))
				break;
			}
	}

	closedir(dp);
	return 0;
}


static int vers_mknod(const char *path, mode_t mode, dev_t rdev)
{

	
	
	
	int res;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	path = prepend_storage_dir(storage_path, path);
	
	// If we are creating a file node (as opposed to directory)
	if (S_ISREG(mode)) {
	
		
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
			
			
		// My code:
		
		// Create the "foo.ext,v" metadata file for the new "foo.ext" file
		char *filename = malloc(strlen(path) + 3);
		sprintf(filename, "%s,v", path);
		res = open(filename, O_CREAT | O_WRONLY, 0666);
		if (res >= 0){
			// Init file version tracker to -1
			int start = -1;
			write(res, &start, 4);
	
			res = close(res);
			}
		
		// end of my vers_mknod() code
		
			
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_mkdir(const char *path, mode_t mode)
{
	int res;

	path = prepend_storage_dir(storage_path, path);
	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_unlink(const char *path)
{
	int res;
	int start = 0;
	
	// Prof's code to delete the current file
	path = prepend_storage_dir(storage_path, path);
	
	res = unlink(path);
	if (res == -1)
		return -errno;
		
	// My unlink code:
	
	// Prepare to delete the metadata file
	char *filename = malloc(strlen(path) + 3);
	sprintf(filename, "%s,v", path);
	res = open(filename, O_RDWR, 0666);
		if (res >= 0){
			// Read the latest backup version before unlinking the metadata file
			read(res, &start, 4);
			res = close(res);
			}
	// Delete it
	res = unlink(filename);
	if (res == -1)
		return -errno;
	
	// Iterate from latest backup version to 0 and delete every backup file
	for(int i = start; i > -1; i--){
		
		char *filename_to = malloc(strlen(path) + 6);
		sprintf(filename_to, "%s,%d", path, i);
		res = unlink(filename_to);
		if (res == -1)
			return -errno;
			// End of my unlink code
		
	}
		
	
		
	

	return 0;
}

static int vers_rmdir(const char *path)
{
	int res;

	path = prepend_storage_dir(storage_path, path);
	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_symlink(const char *from, const char *to)
{
	int res;
	char storage_from[256];
	char storage_to[256];

	prepend_storage_dir(storage_from, from);
	prepend_storage_dir(storage_to,   to  );
	res = symlink(storage_from, storage_to);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_rename(const char *from, const char *to)
{
	int res;
	char storage_from[256];
	char storage_to[256];
	int start;
	// Prof's code to rename the current file itself
	prepend_storage_dir(storage_from, from);
	prepend_storage_dir(storage_to,   to  );
	
	res = rename(storage_from, storage_to);
	if (res == -1)
		return -errno;

	// My rename code:	
		
	// rename the *.v metadata file
	char *filename_from = malloc(strlen(storage_from) + 3);
	char *filename_to = malloc(strlen(storage_to) + 3);
	sprintf(filename_from, "%s,v", storage_from);
	sprintf(filename_to, "%s,v", storage_to);
	res = rename(filename_from, filename_to);
	if (res == -1)
		return -errno;
	
	// Read the latest version value from the metadata file
	res = open(filename_to, O_RDWR, 0666);
		if (res >= 0){
			read(res, &start, 4);
			res = close(res);
			}
			
	// Iterate from latest version to 0 and rename every *,n backup file accordingly		
	for(int i = start; i > -1; i--){
		
		char *filename_from_n = malloc(strlen(storage_from) + 6);
		sprintf(filename_from_n, "%s,%d", storage_from, i);
		
		char *filename_to_n = malloc(strlen(storage_to) + 6);
		sprintf(filename_to_n, "%s,%d", storage_to, i);
		
		
		res = rename(filename_from_n, filename_to_n);
		if (res == -1)
			return -errno;
			// End of my rename code
		
	}
	
	

	return 0;
}

static int vers_link(const char *from, const char *to)
{
	int res;
	char storage_from[256];
	char storage_to[256];

	prepend_storage_dir(storage_from, from);
	prepend_storage_dir(storage_to,   to  );
	res = link(storage_from, storage_to);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_chmod(const char *path, mode_t mode)
{
	int res;

	path = prepend_storage_dir(storage_path, path);
	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	path = prepend_storage_dir(storage_path, path);
	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_truncate(const char *path, off_t size)
{
	int res;
	
	char *path_copy = malloc(strlen(path));
	strcpy(path_copy, path);
	
	// backup the file before truncating
	vers_backup(path_copy);

	path = prepend_storage_dir(storage_path, path);
	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int vers_utimens(const char *path, const struct timespec ts[2])
{
	int res;

	/* don't use utime/utimes since they follow symlinks */
	path = prepend_storage_dir(storage_path, path);
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int vers_open(const char *path, struct fuse_file_info *fi)
{
	int res;

	path = prepend_storage_dir(storage_path, path);
	res = open(path, fi->flags);
	if (res == -1)
		return -errno;

	close(res);

	return 0;
}

static int vers_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;
	int i;
	char temp_buf[size];

	(void) fi;
	path = prepend_storage_dir(storage_path, path);
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -errno;

	res = pread(fd, temp_buf, size, offset);
	if (res == -1)
		res = -errno;

	// Move data from temporary buffer into provided one.
	for (i = 0; i < size; i += 1) {
	  buf[i] = temp_buf[i];
	}

	close(fd);
	
	
	
	
	return res;
}

static int vers_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;
	int i;
	char temp_buf[size];

	(void) fi;
	
	char *path_copy = malloc(strlen(path));
	strcpy(path_copy, path);
	
	// backup curent contents before (over)writing it
	vers_backup(path_copy);
	
	path = prepend_storage_dir(storage_path, path);
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	for (i = 0; i < size; i += 1) {
	  temp_buf[i] = buf[i];
	}

	res = pwrite(fd, temp_buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
}

static int vers_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	path = prepend_storage_dir(storage_path, path);
	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int vers_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) fi;
	return 0;
}

static int vers_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int vers_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;

	path = prepend_storage_dir(storage_path, path);
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int vers_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	path = prepend_storage_dir(storage_path, path);
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int vers_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	path = prepend_storage_dir(storage_path, path);
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int vers_listxattr(const char *path, char *list, size_t size)
{
	path = prepend_storage_dir(storage_path, path);
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int vers_removexattr(const char *path, const char *name)
{
	path = prepend_storage_dir(storage_path, path);
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations vers_oper = {
	.getattr	= vers_getattr,
	.access		= vers_access,
	.readlink	= vers_readlink,
	.readdir	= vers_readdir,
	.mknod		= vers_mknod,
	.mkdir		= vers_mkdir,
	.symlink	= vers_symlink,
	.unlink		= vers_unlink,
	.rmdir		= vers_rmdir,
	.rename		= vers_rename,
	.link		= vers_link,
	.chmod		= vers_chmod,
	.chown		= vers_chown,
	.truncate	= vers_truncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= vers_utimens,
#endif
	.open		= vers_open,
	.read		= vers_read,
	.write		= vers_write,
	.statfs		= vers_statfs,
	.release	= vers_release,
	.fsync		= vers_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= vers_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= vers_setxattr,
	.getxattr	= vers_getxattr,
	.listxattr	= vers_listxattr,
	.removexattr	= vers_removexattr,
#endif
};

int main(int argc, char *argv[])
{
	umask(0);
	if (argc < 3) {
	  fprintf(stderr, "USAGE: %s <storage directory> <mount point> [ -d | -f | -s ]\n", argv[0]);
	  return 1;
	}
	storage_dir = argv[1];
	char* mount_dir = argv[2];
	if (storage_dir[0] != '/' || mount_dir[0] != '/') {
	  fprintf(stderr, "ERROR: Directories must be absolute paths\n");
	  return 1;
	}
	fprintf(stderr, "DEBUG: Mounting %s at %s\n", storage_dir, argv[2]);
	int short_argc = argc - 1;
	char* short_argv[short_argc];
	short_argv[0] = argv[0];
	for (int i = 2; i < argc; i += 1) {
	  short_argv[i - 1] = argv[i];
	}
	return fuse_main(short_argc, short_argv, &vers_oper, NULL);
}
