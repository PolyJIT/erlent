#define FUSE_USE_VERSION 30
extern "C" {
#include <fuse.h>
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
    dbg() << "erlent_open for '" << path << "'." << endl;
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
    // Wait for all remaining child processes (fuse may have
    // started "fusermount -u -z" or similar).
    pid_t pid;
    int status;

    dbg() << "A" << endl;
    while ((pid = wait(&status)) != -1) {
    }
    dbg() << "B" << endl;
    // When there are no more child processes, errno is set
    // to ECHILD.
    if (errno != ECHILD) {
        int err = errno;
        cerr << "Error in wait: " << strerror(err) << "endl";
    }
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
            if (err == EBUSY && tries > 0) {
                usleep(10000);
            } else {
                cerr << "Could not remove '" << newroot << "': " << strerror(err) << "." << endl;
                tries = 0;
            }
        } else
            tries = 0;
    } while (tries > 0);
    // rmdir(tempdir);
}


static pid_t child_pid = 0, fuse_pid = 0;
static int child_res = 0;
static ChildParams params;
static char *const *cmdArgs;

static void *erlent_init(struct fuse_conn_info *conn)
{
    child_pid = setup_child(cmdArgs, params);
    if (child_pid != -1)
        run_child();
    return 0;
}

// The user command sends SIGCHLD when it exits.
// The receiver is the FUSE process (it has started
// the user command as its child).
// Call fuse_exit() to terminate the file system.
static void sigchld_action(int signum, siginfo_t *si, void *ctx)
{
    int status;
    dbg() << "sigchld_action" << endl;
    waitpid(si->si_pid, &status, 0);

    if (si->si_pid != child_pid)
        return;

    child_res = WIFEXITED(status) ? WEXITSTATUS(status) : 127;

    fuse_exit(fuse_get_context()->fuse);
}

int erlent_fuse(RequestProcessor &rp, char *const *cmdArgs_, const ChildParams &params_)
{
    cmdArgs = cmdArgs_;
    params = params_;
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

    struct sigaction sact;
    memset(&sact, 0, sizeof(sact));
    sact.sa_sigaction = sigchld_action;
    sact.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
    if (sigaction(SIGCLD, &sact, 0) == -1)
        errExit("sigaction");

    params.newRoot = newroot;

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

    fuse_pid = fork();
    if (fuse_pid == -1)
        errExit("fork/fuse");
    else if (fuse_pid == 0) {
        // the SIGCHLD signal handler needs to know the PID
        fuse_pid = getpid();
        char *fuse_args[] = {
            strdup("erlent-fuse"), strdup("-f"), //strdup("-s"),
            strdup("-o"), strdup("auto_unmount"),
            strdup("-o"), strdup("allow_other"),
            strdup("-o"), strdup("default_permissions"),
            //strdup("-d"),
            strdup(newroot.c_str())
        };
        int fuse_argc = sizeof(fuse_args)/sizeof(*fuse_args);

        // The child signals its exit code to this process,
        // so return child_res in case FUSE terminates correctly.
        int fuse_err = fuse_main(fuse_argc, fuse_args, &erlent_oper, NULL);
        exit(fuse_err != 0 ? fuse_err : child_res);
    }

    // wait for the FUSE process to end
    // "child_res" is not meaningful in this process because
    // the process "fuse_pid" has the child process as its child
    // (and gets its exit code).
    int res = wait_for_pid(fuse_pid);

    cleanup();

    return res;
}
