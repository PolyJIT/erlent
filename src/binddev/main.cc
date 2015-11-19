#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <unistd.h>
}

using namespace std;

static void usage(const char *progname)
{
    cerr << "USAGE: " << progname << " <OPTIONS> [--] CMD ARGS..." << endl
         << endl
         << progname << " bind mounts /dev, /dev/pts, /proc and /sys to a given directory" << endl
         << "and executes CMD (as the calling user, without privileges)." << endl
         << endl
         << "   -r DIR       target directory (containing dev, etc.)," << endl
         << "                defaults to \".\"" << endl
         << "   -h           print this help" << endl
         << "   CMD ARGS...  command to execute and its arguments;" << endl;
}

static vector<string> paths = { "/dev", "/dev/pts", "/proc", "/sys" };

int main(int argc, char *argv[]) {
    int opt, usercmd;
    uid_t uid = getuid();
    gid_t gid = getgid();

    if (geteuid() != 0) {
        cerr << "Need to be root (uid 0) to run this program." << endl;
        exit(1);
    }

    std::string targetdir = ".";

    while ((opt = getopt(argc, argv, "r:h")) != -1) {
        switch(opt) {
        case 'r': targetdir = optarg; break;
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

    if (unshare(CLONE_NEWNS) == -1) {
        int err = errno;
        cerr << "Could not unshare mount namespace: "
             << strerror(err) << endl;
        exit(1);
    }
    for (const string &path : paths) {
        const char *src = path.c_str();
        string destStr = targetdir + path;
        const char *dest = destStr.c_str();
        if (mount(src, dest, "", MS_BIND, "") == -1) {
            int err = errno;
            cerr << "failed to bind '" << src << "'' to '" << dest << "': "
                 << strerror(err) << endl;
            exit(1);
        }
    }
    if (seteuid(uid) == -1) {
        perror("seteuid");
        exit(1);
    }
    if (setegid(gid) == -1) {
        perror("setegid");
        exit(1);
    }
    if (execvp(args[0], args) == -1) {
        int err = errno;
        cerr << "Could not execute '" << args[0] << "': "
             << strerror(err) << endl;
        exit(1);
    }
    return 0;
}
