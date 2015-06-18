#include "erlent/erlent.hh"

#include <istream>
#include <ostream>
#include <iostream>

extern "C" {
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
}

using namespace std;
using namespace erlent;

bool processMessage()
{
    istream &is = cin;
    ostream &os = cout;

    Request *req = Request::receive(is);
    req->perform(os);
    os.flush();
    return true;
}

static pid_t child_pid;

static void startchild(int argc, char *argv[])
{
    int tochild[2], toparent[2];
    if (pipe(tochild) == -1 || pipe(toparent)) {
        int err = errno;
        cerr << "Error creating pipes: " << strerror(err) << endl;
        exit(1);
    }
    child_pid = fork();
    if (child_pid == -1) {
        int err = errno;
        cerr << "Error in fork: " << strerror(err) << endl;
        exit(1);
    } else if (child_pid == 0) {
        close(tochild[1]);
        close(toparent[0]);
        dup2(tochild[0], 0);
        dup2(toparent[1], 1);
        close(tochild[0]);
        close(toparent[1]);

        char **args = new char* [argc+1];
        for (int i=0; i<argc; ++i)
            args[i] = argv[i];
        args[argc] = 0;

        execvp(args[0], args);
        int err = errno;
        cerr << "Could not execute '" << argv[0] << "': " << strerror(err) << endl;
        exit(127);
    }

    close(tochild[0]);
    close(toparent[1]);

    dup2(tochild[1], 1);
    dup2(toparent[0], 0);

    close(tochild[1]);
    close(toparent[0]);
}

void usage(const char *progname) {
    cerr << "USAGE: " << progname << "[-d] [-h] [--] CMD ARGS..." << endl
         << "   -h           show this help" << endl
         << "   -d           show debug messages" << endl
         << "   CMD ARGS...  command to execute and its arguments" << endl;
}

int main(int argc, char *argv[])
{
    int opt, usercmd;

    child_pid = 0;

    while ((opt = getopt(argc, argv, "+dh")) != -1) {
        switch(opt) {
        case 'd': GlobalOptions::setDebug(true); break;
        case 'h': usage(argv[0]); return 0;
        default:
            usage(argv[0]); return 1;
        }
    }
    usercmd = optind;

    if (usercmd >= argc) {
        usage(argv[0]);
        return 1;
    }

    startchild(argc-usercmd, &argv[usercmd]);

    try {
        cout << unitbuf;
        do {
        } while (processMessage());
    } catch (EofException &e) {
    }

    int res = 0;

    if (child_pid != 0) {
        int status;
        waitpid(child_pid, &status, 0);
        res = WIFEXITED(status) ? WEXITSTATUS(status) : 127;
        if (!WIFEXITED(status)) {
            cerr << "Error executing " << argv[1] << "." << endl;
        }
    }
    return res;
}
