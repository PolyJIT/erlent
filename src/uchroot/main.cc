extern "C" {
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <errno.h>
#include <fcntl.h>
#include <csignal>

#include <iostream>
#include <sstream>
#include <vector>

#include "erlent/child.hh"
#include "erlent/erlent.hh"

using namespace std;
using namespace erlent;

static pid_t child_pid = 0;
static int child_res = 0;

extern "C"
void sigchld_action(int signum, siginfo_t *si, void *ctx)
{
    int status;
    waitpid(si->si_pid, &status, 0);

    if (si->si_pid != child_pid)
        return;

    dbg() << "sigchld_action: child process exited" << endl;
    child_res = WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

static void cleanup()
{
    pid_t pid;
    int status;

    while ((pid = wait(&status)) != -1) {
    }

    if (errno != ECHILD) {
        int err = errno;
        cerr << "Error in wait: " << strerror(err) << "endl";
    }
}

static void usage(const char *progname)
{
    cerr << "USAGE: " << progname << " <OPTIONS> [--] CMD ARGS..." << endl
         << endl
         << progname << " allows a user to perform a \"change root\" operation" << endl
         << endl
         << "   -r DIR       new root directory" << endl
         << "   -w DIR       change working directory to DIR after changing root" << endl
         << "   -u UID       change user ID to UID" << endl
         << "   -g GID       change group ID to GID" << endl
         << "   -d           print a few debug messages" << endl
         << "   -h           print this help" << endl
         << "   CMD ARGS...  command to execute and its arguments" << endl;
}

int main(int argc, char *argv[])
{
    int opt, usercmd;
    uid_t euid = geteuid();
    gid_t egid = getegid();
    uid_t inner_uid = euid;
    gid_t inner_gid = egid;

    cerr << unitbuf;
    cout << unitbuf;

    char cwd[PATH_MAX];
    const char *newwd = getcwd(cwd, sizeof(cwd));
    const char *newroot = "/";
    GlobalOptions::setDebug(false);

    while ((opt = getopt(argc, argv, "+r:w:u:g:dh")) != -1) {
        switch(opt) {
        case 'r': newroot = optarg; break;
        case 'w': newwd = optarg; break;
        case 'd': GlobalOptions::setDebug(true); break;
        case 'u': inner_uid = atol(optarg); break;
        case 'g': inner_gid = atol(optarg); break;
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
    char **args = new char* [n_args+1];
    for (int i=0; i<n_args; ++i)
        args[i] = argv[i+usercmd];
    args[n_args] = 0;

    struct sigaction sact;
    memset(&sact, 0, sizeof(sact));
    sact.sa_sigaction = sigchld_action;
    sact.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
    if (sigaction(SIGCLD, &sact, 0) == -1) {
        perror("sigaction");
        exit(1);
    }

    child_pid = setup_child(inner_uid, inner_gid, newroot, newwd, args);
    run_child();

    cleanup();

    return child_res;
}

