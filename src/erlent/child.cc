extern "C" {
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
}

#include "erlent/child.hh"
#include "erlent/erlent.hh"


using namespace std;
using namespace erlent;

int tochild[2], toparent[2];

static void signal_child(char c) {
    if (write(tochild[1], &c, 1) == -1)
        perror("signal_child");
}

static void signal_parent(char c) {
    if (write(toparent[1], &c, 1) == -1)
        perror("signal_parent");
}

static void wait_child(char expected) {
    char c = 0;
    if (read(toparent[0], &c, 1) == -1)
        perror("wait_child");
    if (c != expected) {
        cerr << "Communication error in wait_child: expected '" << expected
             << "', got '" << c << "'." << endl;
        exit(1);
    }
}

static void wait_parent(char expected) {
    char c = 0;
    if (read(tochild[0], &c, 1) == -1)
        perror("wait_parent");
    if (c != expected) {
        cerr << "Communication error in wait_parent: expected '" << expected
             << "', got '" << c << "'." << endl;
        exit(1);
    }
}


void mnt(const char *src, const char *dst, const char *fstype, int flags) {
    int res = mount(src, dst, fstype, flags, NULL);
    if (res == -1) {
        int err = errno;
        cerr << "Mount of '" << src << "' at '" << dst << "' (type " << fstype
             << ") failed: " << strerror(err) << endl;
        exit(1);
    }
}

static char *const *args;

static int childFunc(ChildParams params)
{
    wait_parent('I');

    dbg() << "newwd = " << params.newRoot << endl;
    dbg() << "newroot = " << params.newWorkDir << endl;
    for (char *const *p=args; *p; ++p) {
        dbg() << " " << *p << endl;
    }

    if (params.devprocsys) {
        const string &root = params.newRoot;
        mnt("proc", (root+"/proc").c_str(), "proc", MS_NODEV | MS_NOSUID | MS_NOEXEC);
        mnt("/dev", (root+"/dev").c_str(), NULL, MS_BIND | MS_REC);
        mnt("/sys", (root+"/sys").c_str(), NULL, MS_BIND | MS_REC);
    }
    for (const pair<string,string> &b : params.bindMounts) {
        mnt(b.first.c_str(), (params.newRoot+"/"+b.second).c_str(), NULL, MS_BIND | MS_REC);
    }

    if (chroot(params.newRoot.c_str()) == -1) {
        int err = errno;
        cerr << "chroot failed: " << strerror(err) << endl;
    }

    if (chdir(params.newWorkDir.c_str()) == -1) {
        int err = errno;
        cerr << "chdir failed: " << strerror(err) << endl;
    }

    /*
    if (dup2(2, 1) == -1) {
        int err = errno;
        cerr << "dup2 failed: " << strerror(err) << endl;
    }
    */

    execvp(args[0], args);
    int err = errno;
    cerr << "Could not execute '" << args[0] << "': " << strerror(err) << endl;
    return 127;
}

static void initComm() {
    if (pipe(tochild) == -1)
        perror("pipe/tochild");
    if (pipe(toparent) == -1)
        perror("pipe/toparent");
}

static pid_t child_pid = 0;
static int child_res;

pid_t setup_child(uid_t new_uid, gid_t new_gid,
                  char *const *cmdArgs,
                  const ChildParams &params)
{
    int fd;

    args = cmdArgs;

    initComm();

    child_res = 127;
    child_pid = fork();
    if (child_pid == -1)
        return -1;
    else if (child_pid == 0) {
        close(toparent[0]);
        close(tochild[1]);
        unshare(CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNS);

        signal_parent('U');
        pid_t p = fork();
        if (p == -1) {
            cerr << "fork (in child) failed." << endl;
            exit(127);
        } else if (p == 0)
            childFunc(params);
        else {
            exit(wait_for_pid(p));
        }
    }
    close(toparent[1]);
    close(tochild[0]);
    wait_child('U');

    char str[200];
    sprintf(str, "/proc/%d/uid_map", child_pid);
    fd = open(str, O_WRONLY);
    if (fd == -1) {
        perror("open uid_map");
        return -1;
    }

    long euid = geteuid();
    sprintf(str,"%ld %ld 1\n", (long)new_uid, euid);
    if (write(fd, str, strlen(str)) == -1) {
        perror("write uid");
        return -1;
    }
    close(fd);

    // gid_map can only be written when setgroups
    // is disabled on newer kernels (for security
    // reasons).
    sprintf(str, "/proc/%d/setgroups", child_pid);
    fd = open(str, O_WRONLY);
    if (fd == -1) {
        perror("open /proc/.../setgroups");
        return -1;
    }
    sprintf(str, "deny");
    if (write(fd, str, strlen(str)) == -1) {
        perror("write setgroups");
        return -1;
    }
    close(fd);

    sprintf(str, "/proc/%d/gid_map", child_pid);
    fd = open(str, O_WRONLY);
    if (fd == -1) {
        perror("open gid_map");
        return -1;
    }

    long egid = getegid();
    sprintf(str,"%ld %ld 1\n", (long)new_gid, (long)egid);
    if (write(fd, str, strlen(str)) == -1) {
        perror("write gid");
        return -1;
    }
    close(fd);

    return child_pid;
}

void run_child() {
    signal_child('I');
}

// return exit status of pid p
int wait_for_pid(pid_t p) {
    int status;
    while (waitpid(p, &status, 0) == -1) {
    }
    int ex;
    if (WIFEXITED(status))
        ex = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        ex = 128 + WTERMSIG(status);
    else
        ex = 255;
    return ex;
}
