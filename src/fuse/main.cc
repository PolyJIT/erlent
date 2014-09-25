#define FUSE_USE_VERSION 30
extern "C" {
#include <fuse.h>
}

#include "erlent/erlent.hh"

using namespace erlent;

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <vector>

using namespace std;

static int erlent_getattr(const char *path, struct stat *stbuf) {
    dbg() << "erlent_getattr" << endl;
    /*
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(path, erlent_path) == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(erlent_str);
    } else
        res = -ENOENT;
        */
    GetattrReply repl(stbuf);
    return GetattrRequest(path).process(repl);
}
    
static int erlent_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi)
{
    dbg() << "erlent_readdir" << endl;
    ReaddirReply rr;
    (void) offset;
    (void) fi;

    int res = ReaddirRequest(path).process(rr);
    if (res == 0) {
        ReaddirReply::name_iterator end = rr.names_end(), it;
        for (it=rr.names_begin(); it != end; ++it) {
            filler(buf, (*it).c_str(), NULL, 0);
        }
    }

    return res;
}

static int erlent_open(const char *path, struct fuse_file_info *fi)
{
    dbg() << "erlent_open for '" << path << "'." << endl;
    OpenReply repl;
    return OpenRequest(path, fi->flags, 0).process(repl);
}

static int erlent_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi)
{
    dbg() << "erlent_read" << endl;
    ReadReply rr(buf, size);
    return ReadRequest(path, size, offset).process(rr);
}

static int erlent_write(const char *path, const char *data, size_t size, off_t offset,
                        struct fuse_file_info *fi)
{
    dbg() << "erlent_write" << endl;
    WriteReply repl;
    return WriteRequest(path, data, size, offset).process(repl);
}

static int erlent_unlink(const char *path) {
    dbg() << "erlent_unlink" << endl;
    UnlinkReply repl;
    return UnlinkRequest(path).process(repl);
}

static int erlent_access(const char *path, int perms) {
    dbg() << "erlent_access" << endl;
    return -ENOSYS;
}

static int erlent_truncate(const char *path, off_t size) {
    dbg() << "erlent_truncate" << endl;
    TruncateReply repl;
    return TruncateRequest(path, size).process(repl);
}


static int erlent_create(const char *path , mode_t mode, struct fuse_file_info *fi)
{
    dbg() << "erlent_create" << endl;
    OpenReply repl;
    return OpenRequest(path, O_CREAT|O_WRONLY|O_TRUNC, mode).process(repl);
}

static int erlent_flush(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

int main(int argc, char *argv[])
{
    dbg() << unitbuf;
    cout << unitbuf;

    struct fuse_operations erlent_oper;
    memset(&erlent_oper, 0, sizeof(erlent_oper));
    erlent_oper.getattr = erlent_getattr;
    erlent_oper.readdir = erlent_readdir;
    erlent_oper.open = erlent_open;
    erlent_oper.read = erlent_read;
    erlent_oper.write = erlent_write;
    erlent_oper.create = erlent_create;
    erlent_oper.unlink = erlent_unlink;
    erlent_oper.access = erlent_access;
    erlent_oper.truncate = erlent_truncate;
    erlent_oper.flush    = erlent_flush;

    return fuse_main(argc, argv, &erlent_oper, NULL);
}
