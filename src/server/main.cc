#include "erlent/erlent.hh"

#include <istream>
#include <ostream>
#include <iostream>

extern "C" {
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
    pid_t pid = fork();
    if (pid == -1) {
        int err = errno;
        cerr << "Error in fork: " << strerror(err) << endl;
        exit(1);
    } else if (pid == 0) {
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

        execv(args[0], args);
        exit(127);
    }

    close(tochild[0]);
    close(toparent[1]);

    dup2(tochild[1], 1);
    dup2(toparent[0], 0);

    close(tochild[1]);
    close(toparent[0]);
}

int main(int argc, char *argv[])
{
    child_pid = 0;
    if (argc >= 2)
        startchild(argc-1, argv+1);

    cout << unitbuf;
    do {
    } while (processMessage());

    int res = 0;

    if (child_pid != 0) {
        int status;
        waitpid(child_pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            cerr << "Error executing " << argv[1]
                 << ": exit code " << WEXITSTATUS(status) << "."
                 << endl;
            res = 1;
        }
    }
    return res;
}
