extern "C" {
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
}

#include <iostream>
#include <utility>

#include <erlent/signalrelay.hh>


using namespace std;

static pid_t child_pid;

// Forward a signal to the child process
static void terminthup_hdl(int sig) {
    kill(child_pid, sig);
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

void erlent::install_signal_relay(pid_t child, initializer_list<int> signals) {
    child_pid = child;

    sigset_t sigset;
    sigemptyset(&sigset);

    for (int sig : signals) {
        set_signal_handler(sig);
        sigaddset(&sigset, sig);
    }

    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}
