extern "C" {
#include <fcntl.h>
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


static const char *newroot;
static const char *newwd;
static char *const *args;

static int childFunc(void *arg)
{
    wait_parent('I');

    dbg() << "newwd = " << newwd << endl;
    dbg() << "newroot = " << newroot << endl;
    for (char *const *p=args; *p; ++p) {
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
        perror("pipe/tochild");
    if (pipe(toparent) == -1)
        perror("pipe/toparent");
}

static pid_t child_pid = 0;
static int child_res;

pid_t setup_child(uid_t new_uid, gid_t new_gid,
                  const char *newRootDir,
                  const char *newWorkDir,
                  char *const *cmdArgs)
{
    int fd;

    newroot = newRootDir;
    newwd = newWorkDir;
    args = cmdArgs;

    initComm();

    child_res = 127;
    child_pid = fork();
    if (child_pid == -1)
        return -1;
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
