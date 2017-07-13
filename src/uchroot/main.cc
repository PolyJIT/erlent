extern "C" {
#include <pwd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/types.h>
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
#include "erlent/fuse.hh"
#include "erlent/local.hh"
#include "erlent/signalrelay.hh"

using namespace std;
using namespace erlent;

static pid_t child_pid = 0;

static void sigchld_action(int signum, siginfo_t *si, void *ctx)
{
    int status;

    // do not consume the exit status of the child process
    // because the main program needs it
    if (si->si_pid == child_pid)
        return;

    waitpid(si->si_pid, &status, 0);

    dbg() << "sigchld_action: child process exited" << endl;
}

static int autoMap(const char *file, vector<Mapping> &mappings) {
    const int nMappings = 65536;
    uid_t ruid = getuid();
    uid_t euid = geteuid();
    int res = -1;
    FILE *f = fopen(file, "r");
    if (f == NULL)
        return res;
    size_t n;
    char *str = NULL;
    while (getline(&str,&n,f) > 0){
        char *p = strchr(str, ':');
        if (p != NULL) {
            *p = '\0';
            struct passwd *pw = getpwnam(str);
            if (pw != NULL && (pw->pw_uid == ruid || pw->pw_uid == euid)) {
                unsigned long outer, count;
                if (sscanf(p+1, "%lu:%lu*[^\n]\n", &outer, &count) == 2) {
                    if (count >= nMappings) {
                        mappings.push_back(Mapping(0,outer,nMappings));
                        res = 0;
                        break;
                    }
                }
            }
        }
    }
    free(str);
    fclose(f);
    return res;
}
static void automaticMappings(ChildParams &params) {
    if (autoMap("/etc/subuid", params.uidMappings) == -1) {
        cerr << "Could not determine user mapping from /etc/subuid, exiting." << endl;
        exit(1);
    }
    if (autoMap("/etc/subgid", params.gidMappings) == -1) {
        cerr << "Could not determine user mapping from /etc/subgid, exiting." << endl;
        exit(1);
    }

}

bool parseBind(const string &str, string &first, string &second) {
    size_t pos = str.find(':');
    if (pos == 0 || pos == string::npos)
        return false;
    first  = str.substr(0, pos);
    second = str.substr(pos+1);
    // Paths must be absolute
    if (first.find('/') != 0 || second.find('/') != 0)
        return false;
    return true;
}

static void usage(const char *progname)
{
    cerr << "USAGE: " << progname << " <OPTIONS> [--] CMD ARGS..." << endl
         << endl
         << progname << " allows a user to perform a \"change root\" operation." << endl
         << "Build time stamp: " << __DATE__ << " " << __TIME__ << endl
         << endl
         << "   -r DIR        new root directory" << endl
         << "   -w DIR        change working directory to DIR after changing root" << endl
         << "   -C            set up /dev, /proc and /sys inside the new root" << endl
         << "   -M SRC:TGT    map SRC (from host) to TGT in new root with uid/gid mapping" << endl
         << "   -m SRC:MNTPT  bind mount SRC (from host) to MNTPT in new root" << endl
         << "   -n            unshare network namespace" << endl
         << "   -E            emulate file owner and access mode through FUSE" << endl
         << "   -u UID        run CMD with this real and effective user  id (default: 0)" << endl
         << "   -g GID        run CMD with this real and effective group id (default: 0)" << endl
         << "   -U I:O:C      map user  ids [I..I+C) to host users  [O..O+C)" << endl
         << "   -G I:O:C      map group ids [I..I+C) to host groups [O..O+C)" << endl
         << "   -A            automatic user/group ID mapping for 65536 users/groups" << endl
         << "   -d            print a few debug messages" << endl
         << "   -h            print this help" << endl
         << "   CMD ARGS...   command to execute and its arguments" << endl
         << endl
         << "   SRC, TGT and MNTPT must be absolute paths." << endl
         << endl;
}

int main(int argc, char *argv[])
{
    ChildParams params;
    string chrootDir("/");
    LocalRequestProcessor reqproc;
    int opt, usercmd;
    bool withfuse = false;

    cerr << unitbuf;
    cout << unitbuf;

    GlobalOptions::setDebug(false);

    char cwd[PATH_MAX];
    params.newWorkDir = getcwd(cwd, sizeof(cwd));
    params.initialUID = 0;
    params.initialGID = 0;

    while ((opt = getopt(argc, argv, "+r:w:CEM:m:nu:g:U:G:Adh")) != -1) {
        switch(opt) {
        case 'r': chrootDir = optarg; break;
        case 'w': params.newWorkDir = optarg; break;
        case 'C': params.devprocsys = true; break;
        case 'M': {
            string outside, inside;
            if (!parseBind(optarg, outside, inside)) {
                usage(argv[0]);
                return 1;
            }
            reqproc.addPathMapping(LocalRequestProcessor::AttrType::Mapped, inside, outside);
            break;
        }
        case 'm':
            if (!ChildParams::addBind(optarg, params.bindMounts)) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'n': params.unshareNet = true; break;
        case 'E': withfuse = true; break;
        case 'u': params.initialUID = atol(optarg); break;
        case 'g': params.initialGID = atol(optarg); break;
        case 'U':
            if (!ChildParams::addMapping(optarg, params.uidMappings)) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'G':
            if (!ChildParams::addMapping(optarg, params.gidMappings)) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'A': automaticMappings(params); break;
        case 'd': GlobalOptions::setDebug(true); break;
        case 'h': usage(argv[0]); return 0;
        default:
            usage(argv[0]); return 1;
        }
    }

    if (params.uidMappings.empty()) {
        uid_t euid = geteuid();
        params.uidMappings.push_back(Mapping(params.initialUID, euid, 1));
    }
    if (params.gidMappings.empty()) {
        gid_t egid = getegid();
        params.gidMappings.push_back(Mapping(params.initialGID, egid, 1));
    }

    usercmd = optind;
    if (usercmd >= argc) {
        // no command is given
        usage(argv[0]);
        return 1;
    }

    if (chrootDir.substr(0,1) != "/") {
        cerr << "New root directory must be an absolute path." << endl;
        return 1;
    }

    if (params.newWorkDir.substr(0,1) != "/") {
        cerr << "New working directory must be an absolute path." << endl;
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

    child_pid = setup_child(args, params);
    FORK_DEBUG {
        cerr << "parent is " << getpid() << endl
             << "child_pid is " << child_pid << endl;
    }

    pid_t fuse_pid = 0;
    if (withfuse) {
        reqproc.addPathMapping(LocalRequestProcessor::AttrType::Emulated, "/", chrootDir);
        reqproc.setParams(params);
        // run_child() is called by erlent_fuse()
        fuse_pid = erlent_fuse(child_pid, reqproc);
        FORK_DEBUG { cerr << "fuse pid is " << fuse_pid << endl; }
    } else {
        run_child(chrootDir);
    }
    wait_child_chroot();
    if (withfuse)
        parent_fuse_preclean();

    int exitcode = wait_for_pid(child_pid, {child_pid, fuse_pid});
    if (fuse_pid != 0) {
        FORK_DEBUG { cerr << "signaling fuse process " << fuse_pid << endl; }
        kill(fuse_pid, SIGTERM);
        wait_for_pid(fuse_pid, {});
    }

    return exitcode;
}

