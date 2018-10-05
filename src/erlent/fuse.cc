#define FUSE_USE_VERSION 30
extern "C" {
#include <fuse.h>
#include <fuse/fuse_lowlevel.h>
#include <sys/types.h>
#include <sys/wait.h>
}

#include "erlent/child.hh"
#include "erlent/erlent.hh"
#include "erlent/fuse.hh"

using namespace erlent;

#include <cstdio>
#include <cstring>
#include <climits>
#include <errno.h>
#include <fcntl.h>
#include <csignal>

#include <limits>
#include <map>
#include <utility>
#include <vector>

using namespace std;

static RequestProcessor *reqproc = nullptr;

static int erlent_getattr(const char *path, struct stat *stbuf) {
    dbg() << "erlent_getattr on '" << path << "'" << endl;
    GetattrRequest req(path);
    req.getReply().init(stbuf);
    return reqproc->process(req);
}

static int erlent_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi)
{
    dbg() << "erlent_readdir '" << path << "'" << endl;
    (void) offset;
    (void) fi;

    ReaddirRequest req(path);
    int res = reqproc->process(req);
    ReaddirReply &rr = req.getReply();
    if (res == 0) {
        ReaddirReply::name_iterator end = rr.names_end(), it;
        for (it=rr.names_begin(); it != end; ++it) {
            filler(buf, (*it).c_str(), NULL, 0);
        }
    }

    return res;
}

static int erlent_readlink(const char *path, char *result, size_t size) {
    dbg() << "erlent_readlink for '" << path << "'." << endl;
    ReadlinkRequest req(path);
    int res = reqproc->process(req);
    if (res == 0) {
        strncpy(result, req.getReply().getTarget().c_str(), size);
    }
    return res;
}

static int erlent_open(const char *path, struct fuse_file_info *fi)
{
    dbg() << "erlent_open for '" << path << "' with flags=0" << fi->flags << "." << endl;
    OpenRequest req(path, fi->flags);
    req.setMode(0);
    return reqproc->process(req);
}

static int erlent_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi)
{
    dbg() << "erlent_read '" << path << "'." << endl;
    ReadRequest req(path, size, offset);
    req.getReply().init(buf, size);
    return reqproc->process(req);
}

static int erlent_write(const char *path, const char *data, size_t size, off_t offset,
                        struct fuse_file_info *fi)
{
    dbg() << "erlent_write '" << path << "'." << endl;
    WriteRequest req(path, data, size, offset);
    return reqproc->process(req);
}

static int erlent_access(const char *path, int perms) {
    dbg() << "erlent_access '" << path << "'." << endl;
    AccessRequest req(path, perms);
    return reqproc->process(req);
}

static int erlent_truncate(const char *path, off_t size) {
    dbg() << "erlent_truncate '" << path << "'." << endl;
    TruncateRequest req(path, size);
    return reqproc->process(req);
}

static int erlent_chmod(const char *path, mode_t mode) {
    dbg() << "erlent_chmod '" << path << "', mode " << hex << mode << endl;
    ChmodRequest req(path, mode);
    return reqproc->process(req);
}

static int erlent_chown(const char *path, uid_t uid, gid_t gid) {
    dbg() << "erlent_chown '" << path << "' " << dec << uid
          << ':' << dec << gid << endl;
    ChownRequest req(path);
    req.setUid(uid);
    req.setGid(gid);
    return reqproc->process(req);
}

static int erlent_mkdir(const char *path, mode_t mode) {
    dbg() << "erlent_mkdir '" << path << "'." << endl;
    MkdirRequest req(path, mode);
    struct fuse_context *ctx = fuse_get_context();
    req.setUid(ctx->uid);
    req.setGid(ctx->gid);
    return reqproc->process(req);
}

static int erlent_unlink(const char *path) {
    dbg() << "erlent_unlink '" << path << "'." << endl;
    UnlinkRequest req(path);
    return reqproc->process(req);
}

static int erlent_rename(const char *from, const char *to) {
    dbg() << "erlent_rename '" << from << "' -> '" << to << "'." << endl;
    RenameRequest req(from, to);
    return reqproc->process(req);
}

static int erlent_rmdir(const char *path) {
    dbg() << "erlent_rmdir '" << path << "'." << endl;
    RmdirRequest req(path);
    return reqproc->process(req);
}

static int erlent_utimens(const char *path, const struct timespec tv[2]) {
    dbg() << "erlent_utimens '" << path << "'." << endl;
    UtimensRequest req(path, tv);
    return reqproc->process(req);
}

static int erlent_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    dbg() << "erlent_create '" << path << "'." << endl;
    CreatRequest req(path);
    req.setMode(mode);
    struct fuse_context *ctx = fuse_get_context();
    req.setUid(ctx->uid);
    req.setGid(ctx->gid);
    return reqproc->process(req);
}

static int erlent_mknod(const char *path, mode_t mode, dev_t dev)
{
    dbg() << "erlent_mknod '" << path << "'." << endl;
    MknodRequest req(path, dev);
    req.setMode(mode);
    struct fuse_context *ctx = fuse_get_context();
    req.setUid(ctx->uid);
    req.setGid(ctx->gid);
    return reqproc->process(req);
}

static int erlent_symlink(const char *from, const char *to) {
    dbg() << "erlent_symlink '" << from << "' -> '" << to << "'." << endl;
    SymlinkRequest req(from, to);
    struct fuse_context *ctx = fuse_get_context();
    req.setUid(ctx->uid);
    req.setGid(ctx->gid);
    return reqproc->process(req);
}

static int erlent_link(const char *from, const char *to) {
    dbg() << "erlent_link '" << from << "' -> '" << to << "'." << endl;
    LinkRequest req(from, to);
    return reqproc->process(req);
}

static int erlent_statfs(const char *path, struct statvfs *buf) {
    dbg() << "erlent_statfs '" << path << "'." << endl;
    StatfsRequest req(path);
    req.getReply().init(buf);
    return reqproc->process(req);
}

static int erlent_flush(const char *path, struct fuse_file_info *fi)
{
    return 0;
}


static void cleanup_tempdir();

static char hostname[200];

static void errExit(const char *msg)
{
    cerr << hostname << ": " << msg << ": "
         << strerror(errno) << endl;
    cleanup_tempdir();
    exit(EXIT_FAILURE);
}

static void cleanup()
{
    // Waiting for child processes seems to be
    // superfluous and incorrect (hangs).
    // The file system is already unmounted
    // when cleanup() is called (fuse_teardown()
    // seems to ensure this).
#if 0
    // Wait for all remaining child processes (fuse may have
    // started "fusermount -u -z" or similar).
    pid_t pid;
    int status;

    cerr << "A" << endl;
    while ((pid = wait(&status)) != -1) {
    }
    cerr << "B" << endl;
    // When there are no more child processes, errno is set
    // to ECHILD.
    if (errno != ECHILD) {
        int err = errno;
        cerr << "Error in wait: " << strerror(err) << "endl";
    }
#endif
    cleanup_tempdir();
}

static string newroot;

static void cleanup_tempdir() {
    // Remove the temporary directory for the
    // new root. The directory should not be in use
    // (we have waited for child processes, e.g. fusermount,
    // for this reason) but, to be on the safe side,
    // check if EBUSY is returned and retry after a short
    // delay in this case.
    int res, err;
    int tries = 10;
    do {
        res = rmdir(newroot.c_str());
        if (res == -1) {
            err = errno;
            --tries;
            if (err == ENOENT) // parent_fuse_preclean() has removed the directory already
                break;
            else if (err == EBUSY && tries > 0) {
                usleep(10000);
            } else {
                cerr << "Could not remove '" << newroot << "': " << strerror(err) << "." << endl;
                tries = 0;
            }
        } else
            tries = 0;
    } while (tries > 0);
}


static void *erlent_init(struct fuse_conn_info *conn)
{
    run_child(newroot);
    // We cannot call wait_child_chroot() here, because
    // the mount operations before the chroot() call in
    // the child require filesystem operations
    // (on the file system we would block here).
    return 0;
}

static fuse_session *fuse_instance;
static pid_t child_pid;

// When the FUSE process receives a TERM, INT, HUP
// or QUIT signal, call fuse_exit() to terminate
// the file system.
static void endsig_hdl(int signum)
{
    FORK_DEBUG { cerr << "fuse received signal " << signum << ", forwarding to " << child_pid << endl; }
    kill(child_pid, signum);
    fuse_session_exit(fuse_instance);
}

pid_t erlent_fuse(pid_t child_pid, RequestProcessor &rp)
{
    ::child_pid = child_pid;
    reqproc = &rp;

    char tempdirtempl[] = "/tmp/erlent.root.XXXXXX";

    gethostname(hostname, sizeof(hostname));

    const char *tempdir = mkdtemp(tempdirtempl);
    if (tempdir == NULL)
        errExit("mkdtemp");
    newroot = string(tempdir);
    /*
    string newrootStr = string(tempdir) + "/newroot";
    newroot = newrootStr.c_str();
    if (mkdir(newroot, S_IRWXU) == -1) {
        rmdir(tempdir);
        errExit("mkdir newroot");
    }
    */
    dbg() << "Running on " << hostname << ", new root: " << newroot << endl;

    struct fuse_operations erlent_oper;
    memset(&erlent_oper, 0, sizeof(erlent_oper));
    erlent_oper.getattr = erlent_getattr;
    erlent_oper.readlink = erlent_readlink;
    erlent_oper.readdir = erlent_readdir;
    erlent_oper.open = erlent_open;
    erlent_oper.read = erlent_read;
    erlent_oper.write = erlent_write;
    erlent_oper.create = erlent_create;
    erlent_oper.mknod  = erlent_mknod;
    erlent_oper.symlink  = erlent_symlink;
    erlent_oper.link     = erlent_link;
    erlent_oper.access   = erlent_access;
    erlent_oper.truncate = erlent_truncate;
    erlent_oper.chmod    = erlent_chmod;
    erlent_oper.chown    = erlent_chown;
    erlent_oper.mkdir    = erlent_mkdir;
    erlent_oper.unlink   = erlent_unlink;
    erlent_oper.rename   = erlent_rename;
    erlent_oper.rmdir    = erlent_rmdir;
    erlent_oper.utimens  = erlent_utimens;
    erlent_oper.flush    = erlent_flush;
    erlent_oper.statfs   = erlent_statfs;
    erlent_oper.init     = erlent_init;

    // By default, FUSE does not pass calls to utimensat() with UTIME_NOW or UTIME_OMIT
    // to the filesystem implementation. We set the following flag to get calls
    // with these special values also (otherwise, time stamps set by "tar", for example,
    // do not work as "tar" uses UTIME_OMIT).
    erlent_oper.flag_utime_omit_ok = 1;

    pid_t fuse_pid = fork();
    if (fuse_pid == -1)
        errExit("fork/fuse");
    else if (fuse_pid == 0) {
        signal(SIGCHLD, SIG_DFL);

        initializer_list<int> signals = { SIGTERM, SIGINT, SIGHUP, SIGQUIT };
        sigset_t sigset;
        sigemptyset(&sigset);
        for (int sig : signals)
            sigaddset(&sigset, sig);
        sigprocmask(SIG_BLOCK, &sigset, NULL);

        char *fuse_args[] = {
            strdup("erlent-fuse"), strdup("-f"), strdup("-s"),
            strdup("-o"), strdup("auto_unmount"),
            strdup("-o"), strdup("allow_other"),
            strdup("-o"), strdup("default_permissions"),
            strdup(newroot.c_str())
        };
        int fuse_argc = sizeof(fuse_args)/sizeof(*fuse_args);

        int fuse_err;

        int multi;
        char *mountpt;
        struct fuse *fuse;
        fuse = fuse_setup(fuse_argc, fuse_args, &erlent_oper, sizeof(erlent_oper),
                          &mountpt, &multi, NULL);
        if (!fuse) {
            cerr << "Could not set up FUSE filesystem" << endl;
            exit(127);
        }
        fuse_instance = fuse_get_session(fuse);

        struct sigaction sact;
        memset(&sact, 0, sizeof(sact));
        sact.sa_handler = endsig_hdl;
        for (int sig : signals) {
            if (sigaction(sig, &sact, 0) == -1)
                errExit("sigaction");
        }
        sigprocmask(SIG_UNBLOCK, &sigset, NULL);

        if (multi)
            fuse_err = fuse_loop_mt(fuse);
        else
            fuse_err = fuse_loop(fuse);

        fuse_teardown(fuse, mountpt);

        cleanup();
        exit(fuse_err);
    }

    return fuse_pid;
}
