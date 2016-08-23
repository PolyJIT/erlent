#ifndef _ERLENT_SIGNALRELAY_HH
#define _ERLENT_SIGNALRELAY_HH

#include <utility>
#include <sys/types.h>

namespace erlent {

void install_signal_relay(std::initializer_list<pid_t> children, std::initializer_list<int> signals);

}

#endif // _ERLENT_SIGNALRELAY_HH
