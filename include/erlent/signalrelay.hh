#ifndef _ERLENT_SIGNALRELAY_HH
#define _ERLENT_SIGNALRELAY_HH

#include <utility>
#include <sys/types.h>

namespace erlent {

void install_signal_relay(pid_t child, std::initializer_list<int> signals);

}

#endif // _ERLENT_SIGNALRELAY_HH
