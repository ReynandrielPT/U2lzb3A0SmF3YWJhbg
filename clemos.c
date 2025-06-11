#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <stdlib.h>

static char docroot[PATH_MAX];
static char logpath[PATH_MAX];

static void reverse_str(char *s, size_t len) {
    for (size_t i = 0; i < len/2; i++) {
        char t = s[i]; s[i] = s[len-1-i]; s[len-1-i] = t;
    }
}

static void log_event(const char *action, const char *path) {
    FILE *f = fopen(logpath, "a");
    if (!f) return;
    time_t now = time(NULL);
    char *ts = ctime(&now);
    if (ts) {
        ts[strcspn(ts, "\n")] = '\0';
        fprintf(f, "%s %s: %s\n", ts, action, path);
    }
    fclose(f);
}

static void map_path(const char *path, char real[PATH_MAX]) {
    char tmp[PATH_MAX]; strncpy(tmp, path, PATH_MAX);
    char *saveptr;
    int depth=0, in_clem=0;
    real[0]='\0';
    strncat(real, docroot, PATH_MAX-strlen(real)-1);
    for (char *token=strtok_r(tmp+1,"/",&saveptr);
         token;
         token=strtok_r(NULL,"/",&saveptr), depth++) {
        char comp[PATH_MAX];
        if (depth==0 && strncmp(token,"Clem_",5)==0) { in_clem=1; strncpy(comp,token,PATH_MAX); }
        else if (in_clem) {
            char name[PATH_MAX]; strncpy(name,token,PATH_MAX);
            char *dot=strrchr(name,'.');
            if(dot){ size_t bl=dot-name; char base[PATH_MAX],ext[PATH_MAX]; strncpy(base,name,bl); base[bl]='\0'; strncpy(ext,dot+1,PATH_MAX); reverse_str(base,bl); snprintf(comp,PATH_MAX,"%s.%s",base,ext); }
            else{ strcpy(comp,name); reverse_str(comp,strlen(comp)); }
        } else strncpy(comp,token,PATH_MAX);
        strncat(real,"/",PATH_MAX-strlen(real)-1);
        strncat(real,comp,PATH_MAX-strlen(real)-1);
    }
}

static int fs_getattr(const char *path, struct stat *stbuf) {
    char p[PATH_MAX]; map_path(path,p);
    return lstat(p, stbuf)==-1? -errno:0;
}

static int fs_mkdir(const char *path, mode_t mode) {
    char p[PATH_MAX]; map_path(path,p);
    int res = mkdir(p, mode);
    if (res==-1) return -errno;
    if(strncmp(path+1,"Downloads",9)==0) log_event("MKDIR", path);
    return 0;
}

static int fs_rmdir(const char *path) {
    char p[PATH_MAX]; map_path(path,p);
    int res = rmdir(p);
    return res==-1? -errno:0;
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
    char p[PATH_MAX]; map_path(path,p);
    int fd = open(p, fi->flags);
    if (fd==-1) return -errno;
    fi->fh = fd;
    return 0;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    int res = pread(fi->fh, buf, size, offset);
    return res<0? -errno: res;
}

static int fs_release(const char *path, struct fuse_file_info *fi) {
    close(fi->fh);
    return 0;
}

static int fs_opendir(const char *path, struct fuse_file_info *fi) {
    char p[PATH_MAX]; map_path(path,p);
    DIR *dp = opendir(p);
    if (!dp) return -errno;
    fi->fh = (intptr_t)dp;
    if(strncmp(path+1,"Downloads",9)==0) log_event("ACCESS", path);
    return 0;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
    DIR *dp = (DIR*)(uintptr_t)fi->fh;
    struct dirent *de;
    int in_clem = strncmp(path+1,"Clem_",5)==0;
    filler(buf,".",NULL,0);
    filler(buf,"..",NULL,0);
    while((de=readdir(dp))){
        if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
        char name[PATH_MAX]; strncpy(name,de->d_name,PATH_MAX);
        if(in_clem){ char *dot=strrchr(name,'.'); if(dot){ size_t bl=dot-name; char base[PATH_MAX]; strncpy(base,name,bl); base[bl]='\0'; reverse_str(base,bl); snprintf(name,PATH_MAX,"%s%s",base,dot);} else reverse_str(name,strlen(name)); }
        struct stat st={0}; st.st_ino=de->d_ino; st.st_mode=de->d_type<<12;
        if(filler(buf,name,&st,0)) break;
    }
    return 0;
}

static int fs_releasedir(const char *path, struct fuse_file_info *fi) {
    closedir((DIR*)(uintptr_t)fi->fh);
    return 0;
}

static struct fuse_operations ops = {
    .getattr = fs_getattr,
    .mkdir   = fs_mkdir,
    .rmdir   = fs_rmdir,
    .open    = fs_open,
    .read    = fs_read,
    .release = fs_release,
    .opendir = fs_opendir,
    .readdir = fs_readdir,
    .releasedir = fs_releasedir,
};

int main(int argc, char *argv[]) {
    if(argc<2){ fprintf(stderr,"Usage: %s <mountpoint>\n",argv[0]); return 1; }
    const char *homedir = getenv("HOME"); if(!homedir) homedir="/home/rey";
    snprintf(docroot,PATH_MAX,"%s/Documents",homedir);
    snprintf(logpath,PATH_MAX,"%s/Documents/Downloads/log.log",homedir);
    umask(0);
    return fuse_main(argc,argv,&ops,NULL);
}
