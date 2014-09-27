#define FUSE_USE_VERSION 30
extern "C" {
#include <fuse.h>
#include <sys/types.h>
#include <sys/wait.h>
}

#include "erlent/erlent.hh"

using namespace erlent;

#include <cstdio>
#include <cstring>
#include <climits>
#include <errno.h>
#include <fcntl.h>
#include <csignal>

#include <vector>

using namespace std;

static int erlent_getattr(const char *path, struct stat *stbuf) {
    dbg() << "erlent_getattr" << endl;
    GetattrRequest req(path);
    req.getReply().init(stbuf);
    return req.process();
}
    
static int erlent_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi)
{
    dbg() << "erlent_readdir" << endl;
    (void) offset;
    (void) fi;

    ReaddirRequest req(path);
    int res = req.process();
    ReaddirReply &rr = req.getReply();
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
    return OpenRequest(path, fi->flags, 0).process();
}

static int erlent_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi)
{
    dbg() << "erlent_read" << endl;
    ReadRequest req(path, size, offset);
    req.getReply().init(buf, size);
    return req.process();
}

static int erlent_write(const char *path, const char *data, size_t size, off_t offset,
                        struct fuse_file_info *fi)
{
    dbg() << "erlent_write" << endl;
    return WriteRequest(path, data, size, offset).process();
}

static int erlent_access(const char *path, int perms) {
    dbg() << "erlent_access" << endl;
    return -ENOSYS;
}

static int erlent_truncate(const char *path, off_t size) {
    dbg() << "erlent_truncate" << endl;
    return TruncateRequest(path, size).process();
}

static int erlent_chmod(const char *path, mode_t mode) {
    dbg() << "erlent_chmod" << endl;
    return ChmodRequest(path, mode).process();
}

static int erlent_mkdir(const char *path, mode_t mode) {
    dbg() << "erlent_mkdir" << endl;
    return MkdirRequest(path, mode).process();
}

static int erlent_unlink(const char *path) {
    dbg() << "erlent_unlink" << endl;
    return UnlinkRequest(path).process();
}

static int erlent_rmdir(const char *path) {
    dbg() << "erlent_rmdir" << endl;
    return RmdirRequest(path).process();
}


static int erlent_create(const char *path , mode_t mode, struct fuse_file_info *fi)
{
    dbg() << "erlent_create" << endl;
    return OpenRequest(path, O_CREAT|O_WRONLY|O_TRUNC, mode).process();
}

static int erlent_flush(const char *path, struct fuse_file_info *fi)
{
    return 0;
}


static char hostname[200];

static void errExit(const char *msg)
{
    cerr << hostname << ": " << strerror(errno) << endl;
    exit(EXIT_FAILURE);
}


int tochild[2], toparent[2];

static void signal_child(char c) {
    if (write(tochild[1], &c, 1) == -1)
        errExit("signal_child");
}

static void signal_parent(char c) {
    if (write(toparent[1], &c, 1) == -1)
        errExit("signal_parent");
}

static void wait_child(char expected) {
    char c = 0;
    if (read(toparent[0], &c, 1) == -1)
        errExit("wait_child");
    if (c != expected) {
        cerr << "Communication error in wait_child: expected '" << expected
             << "', got '" << c << "'." << endl;
        exit(1);
    }
}

static void wait_parent(char expected) {
    char c = 0;
    if (read(tochild[0], &c, 1) == -1)
        errExit("wait_parent");
    if (c != expected) {
        cerr << "Communication error in wait_parent: expected '" << expected
             << "', got '" << c << "'." << endl;
        exit(1);
    }
}

static void *erlent_init(struct fuse_conn_info *conn)
{
    signal_child('I');
    return 0;
}

char *newroot;
char *newwd;
char **args;

static int childFunc(void *arg)
{
    wait_parent('I');

    dbg() << "newwd = " << newwd << endl;
    dbg() << "newroot = " << newroot << endl;
    for (char **p=args; *p; ++p) {
        dbg() << " " << *p << endl;
    }

    if (chroot(newroot) == -1) {
        int err = errno;
        cerr << "chroot failed: " << strerror(err) << endl;
    }

    if (chdir(newwd) == -1) {
        int err = errno;
        cerr << "chdir failed: " << strerror(err) << endl;
    }

    if (dup2(2, 1) == -1) {
        int err = errno;
        cerr << "dup2 failed: " << strerror(err) << endl;
    }

    execvp(args[0], args);
    int err = errno;
    cerr << "Could not execute '" << args[0] << "': " << strerror(err) << endl;
    return 127;
}

static void initComm() {
    if (pipe(tochild) == -1)
        errExit("pipe/tochild");
    if (pipe(toparent) == -1)
        errExit("pipe/toparent");
}


static void cleanup()
{
    // Wait for all remaining child processes (fuse may have
    // started "fusermount -u -z" or similar).
    pid_t pid;
    int status;
    while ((pid = wait(&status)) != -1) {
    }
    // When there are no more child processes, errno is set
    // to ECHILD.
    if (errno != ECHILD) {
        int err = errno;
        cerr << "Error in wait: " << strerror(err) << "endl";
    }

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
}

static pid_t child_pid, fuse_pid;
static int child_res;

extern "C"
void sigchld_action(int signum, siginfo_t *si, void *ctx)
{
    int status;
    dbg() << "sigchld_action" << endl;
    waitpid(si->si_pid, &status, 0);

    if (si->si_pid != child_pid)
        return;

    child_res = WIFEXITED(status) ? WEXITSTATUS(status) : 127;

    kill(fuse_pid, SIGTERM);
}

static void usage(const char *progname)
{
    cerr << "USAGE: " << progname << " [-w DIR] [-h] CMD ARGS..." << endl
         << "   -w DIR       change working directory to DIR" << endl
         << "   -h           print this help" << endl
         << "   CMD ARGS...  command to execute and its arguments" << endl;
}

int main(int argc, char *argv[])
{
    int opt, usercmd;

    dbg() << unitbuf;
    cout << unitbuf;

    char cwd[PATH_MAX];
    newwd = getcwd(cwd, sizeof(cwd));

    while ((opt = getopt(argc, argv, "+w:h")) != -1) {
        switch(opt) {
        case 'w': newwd = optarg; break;
        case 'h': usage(argv[0]); return 0;
        default:
            usage(argv[0]); return 1;
        }
    }
    usercmd = optind;
    if (usercmd >= argc) {
        // no command is given
        usage(argv[0]);
        return 1;
    }

    int n_args = argc - usercmd;
    args = new char* [n_args+1];
    for (int i=0; i<n_args; ++i)
        args[i] = argv[i+usercmd];
    args[n_args] = 0;

    char newroottempl[] = "/tmp/newroot.XXXXXX";
    int fd;

    gethostname(hostname, sizeof(hostname));

    newroot = mkdtemp(newroottempl);
    if (newroot == NULL)
        errExit("mkdtemp");
    dbg() << "Running on " << hostname << ", temp dir: " << newroot << endl;

    struct sigaction sact;
    memset(&sact, 0, sizeof(sact));
    sact.sa_sigaction = sigchld_action;
    sact.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
    if (sigaction(SIGCLD, &sact, 0) == -1)
        errExit("sigaction");

    initComm();

    child_res = 127;
    child_pid = fork();
    if (child_pid == -1)
        errExit("fork");
    else if (child_pid == 0) {
        close(toparent[0]);
        close(tochild[1]);
        unshare(CLONE_NEWUSER);
        signal_parent('U');
        childFunc(0);
    }
    close(toparent[1]);
    close(tochild[0]);
    wait_child('U');

    char str[200];
    sprintf(str, "/proc/%d/uid_map", child_pid);
    fd = open(str, O_WRONLY);
    if (fd == -1)
        errExit("open");
    sprintf(str,"0 %ld 1\n", (long)geteuid());
    if (write(fd, str, strlen(str)) == -1)
        errExit("write");
    close(fd);

    sprintf(str, "/proc/%d/gid_map", child_pid);
    fd = open(str, O_WRONLY);
    if (fd == -1)
        errExit("open");
    sprintf(str,"0 %ld 1\n", (long)getegid());
    if (write(fd, str, strlen(str)) == -1)
        errExit("write");
    close(fd);

    struct fuse_operations erlent_oper;
    memset(&erlent_oper, 0, sizeof(erlent_oper));
    erlent_oper.getattr = erlent_getattr;
    erlent_oper.readdir = erlent_readdir;
    erlent_oper.open = erlent_open;
    erlent_oper.read = erlent_read;
    erlent_oper.write = erlent_write;
    erlent_oper.create = erlent_create;
    erlent_oper.access = erlent_access;
    erlent_oper.truncate = erlent_truncate;
    erlent_oper.chmod    = erlent_chmod;
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
            argv[0], strdup("-f"), strdup("-s"),
            strdup("-o"), strdup("auto_unmount"), newroot
        };
        int fuse_argc = sizeof(fuse_args)/sizeof(*fuse_args);
        exit(fuse_main(fuse_argc, fuse_args, &erlent_oper, NULL));
    }

    int fuse_status;
    waitpid(fuse_pid, &fuse_status, 0);

    cleanup();

    return child_res;
}
