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

static map<string,pair<uid_t,gid_t>> chownMap;

static int erlent_getattr(const char *path, struct stat *stbuf) {
    dbg() << "erlent_getattr on '" << path << "'" << endl;
    GetattrRequest req(path);
    req.getReply().init(stbuf);
    int res = reqproc->process(req);
    auto it = chownMap.find(path);
    if (res == 0 && it != chownMap.end()) {
        stbuf->st_uid = it->second.first;
        stbuf->st_gid = it->second.second;
    }

    return res;
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
    OpenRequest req(path, fi->flags, 0);
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
    cerr << "erlent_chown '" << path << "' " << dec << uid
          << ':' << dec << gid << endl;
#if 0
    ChownRequest req(path, uid, gid);
    return reqproc->process(req);
#endif
    chownMap[path] = make_pair(uid,gid);
    return 0;
}

static int erlent_mkdir(const char *path, mode_t mode) {
    dbg() << "erlent_mkdir '" << path << "'." << endl;
    MkdirRequest req(path, mode);
    return reqproc->process(req);
}

static int erlent_unlink(const char *path) {
    dbg() << "erlent_unlink '" << path << "'." << endl;
    UnlinkRequest req(path);
    return reqproc->process(req);
}

static int erlent_rmdir(const char *path) {
    dbg() << "erlent_rmdir '" << path << "'." << endl;
    RmdirRequest req(path);
    return reqproc->process(req);
}

static int erlent_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    dbg() << "erlent_create '" << path << "'." << endl;
    OpenRequest req(path, O_CREAT|O_WRONLY|O_TRUNC, mode);
    return reqproc->process(req);
}

static int erlent_mknod(const char *path, mode_t mode, dev_t dev)
{
    dbg() << "erlent_mknod '" << path << "'." << endl;
    return -ENOSYS;
}

static int erlent_symlink(const char *from, const char *to) {
    dbg() << "erlent_symlink '" << from << "' -> '" << to << "'." << endl;
    SymlinkRequest req(from, to);
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

const char *tempdir;
const char *newroot;

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
        res = rmdir(newroot);
        err = errno;
        if (res == -1) {
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
    rmdir(tempdir);
}


static pid_t child_pid = 0, fuse_pid = 0;
static int child_res = 0;
static ChildParams params;
static char *const *cmdArgs;

static void *erlent_init(struct fuse_conn_info *conn)
{
    child_pid = setup_child(cmdArgs, params);
    if (child_pid == -1)
        errExit("fork");
    run_child();
    return 0;
}

static void sigchld_action(int signum, siginfo_t *si, void *ctx)
{
    int status;
    dbg() << "sigchld_action" << endl;
    waitpid(si->si_pid, &status, 0);

    if (si->si_pid != child_pid)
        return;

    child_res = WIFEXITED(status) ? WEXITSTATUS(status) : 127;

    kill(fuse_pid, SIGTERM);
}

int erlent_fuse(RequestProcessor &rp, char *const *cmdArgs_, const ChildParams &params_)
{
    cmdArgs = cmdArgs_;
    params = params_;
    reqproc = &rp;

    char tempdirtempl[] = "/tmp/erlent.XXXXXX";

    gethostname(hostname, sizeof(hostname));

    tempdir = mkdtemp(tempdirtempl);
    if (tempdir == NULL)
        errExit("mkdtemp");
    string newrootStr = string(tempdir) + "/newroot";
    newroot = newrootStr.c_str();
    if (mkdir(newroot, S_IRWXU) == -1) {
        rmdir(tempdir);
        errExit("mkdir newroot");
    }
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
    erlent_oper.symlink = erlent_symlink;
    erlent_oper.access = erlent_access;
    erlent_oper.truncate = erlent_truncate;
    erlent_oper.chmod    = erlent_chmod;
    erlent_oper.chown    = erlent_chown;
    erlent_oper.mkdir    = erlent_mkdir;
    erlent_oper.unlink   = erlent_unlink;
    erlent_oper.rmdir    = erlent_rmdir;
    erlent_oper.flush    = erlent_flush;
    erlent_oper.init     = erlent_init;

    fuse_pid = fork();
    if (fuse_pid == -1)
        errExit("fork/fuse");
    else if (fuse_pid == 0) {
        char *fuse_args[] = {
            strdup("erlent-fuse"), strdup("-f"), strdup("-s"),
            strdup("-o"), strdup("auto_unmount"), strdup(newroot)
        };
        int fuse_argc = sizeof(fuse_args)/sizeof(*fuse_args);
        exit(fuse_main(fuse_argc, fuse_args, &erlent_oper, NULL));
    }

    int fuse_status;
    waitpid(fuse_pid, &fuse_status, 0);

    cleanup();

    return child_res;
}
