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

extern "C"
void sigchld_action(int signum, siginfo_t *si, void *ctx)
{
    int status;
    waitpid(si->si_pid, &status, 0);

    if (si->si_pid != child_pid)
        return;

    dbg() << "sigchld_action: child process exited" << endl;
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
         << "   -m DIR:MNTPT  map DIR (from host) to MNTPT in new root" << endl
         << "   -u UID        map UID to effective user  id" << endl
         << "   -g GID        map GID ot effective group id" << endl
         << "   -U I:O:C      map user  ids [I..I+C) to host users  [O..O+C)" << endl
         << "   -G I:O:C      map group ids [I..I+C) to host groups [O..O+C)" << endl
         << "   -d            print a few debug messages" << endl
         << "   -h            print this help" << endl
         << "   CMD ARGS...   command to execute and its arguments" << endl
         << endl;
}

static bool addBind(const string &arg, vector<pair<string,string>> &binds) {
    size_t pos = arg.find(':');
    if (pos == 0 || pos == string::npos)
        return false;
    binds.push_back(make_pair(arg.substr(0, pos), arg.substr(pos+1)));
    return true;
}

static bool addMapping(const string &arg, vector<Mapping> &mappings) {
    long innerID, outerID, count;
    char ch;
    istringstream iss(arg);

    iss >> innerID;
    iss >> ch;
    if (ch != ':')
        return false;
    iss >> outerID;
    iss >> ch;
    if (ch != ':')
        return false;
    iss >> count;
    if (!iss.eof() || iss.fail())
        return false;

    mappings.push_back(Mapping(innerID, outerID, count));
    return true;
}

int main(int argc, char *argv[])
{
    int opt, usercmd;
    uid_t euid = geteuid();
    gid_t egid = getegid();

    ChildParams params;

    cerr << unitbuf;
    cout << unitbuf;

    GlobalOptions::setDebug(false);

    while ((opt = getopt(argc, argv, "+r:w:Cm:U:G:u:g:dh")) != -1) {
        switch(opt) {
        case 'r': params.newRoot = optarg; break;
        case 'w': params.newWorkDir = optarg; break;
        case 'C': params.devprocsys = true; break;
        case 'm':
            if (!addBind(optarg, params.bindMounts)) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'u': params.uidMappings.push_back(Mapping(atol(optarg), euid, 1)); break;
        case 'g': params.gidMappings.push_back(Mapping(atol(optarg), egid, 1)); break;
        case 'U':
            if (!addMapping(optarg, params.uidMappings)) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'G':
            if (!addMapping(optarg, params.gidMappings)) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'd': GlobalOptions::setDebug(true); break;
        case 'h': usage(argv[0]); return 0;
        default:
            usage(argv[0]); return 1;
        }
    }

    if (params.uidMappings.empty())
        params.uidMappings.push_back(Mapping(euid, euid, 1));
    if (params.gidMappings.empty())
        params.gidMappings.push_back(Mapping(egid, egid, 1));

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

    child_pid = setup_child(args, params);
    run_child();

    return wait_for_pid(child_pid);
}

