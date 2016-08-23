extern "C" {
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
}

#include <iostream>
#include <set>
#include <utility>

#include <erlent/child.hh>
#include <erlent/signalrelay.hh>


using namespace std;

static set<pid_t> child_pids;

// Forward a signal to the child process
static void terminthup_hdl(int sig) {
    FORK_DEBUG {
        cerr << "received signal " << sig << " in process " << getpid()
             << ", forwarding to ";
    }
    bool first = true;
    for (pid_t child_pid : child_pids) {
        FORK_DEBUG { cerr << (first ? "" : ", ") << child_pid; }
        kill(child_pid, sig);
        first = false;
    }
    FORK_DEBUG { cerr << endl; }
}

static void set_signal_handler(int sig) {
    struct sigaction sact;
    memset(&sact, 0, sizeof(sact));
    sact.sa_handler = terminthup_hdl;
    sact.sa_flags = 0;
    if (sigaction(sig, &sact, 0) == -1) {
        perror("sigaction");
        exit(1);
    }
}

void erlent::install_signal_relay(initializer_list<pid_t> children, initializer_list<int> signals) {
    child_pids = children;

    sigset_t sigset;
    sigemptyset(&sigset);

    for (int sig : signals) {
        set_signal_handler(sig);
        sigaddset(&sigset, sig);
    }

    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}
