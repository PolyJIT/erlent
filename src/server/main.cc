#include "erlent/erlent.hh"

#include <istream>
#include <ostream>
#include <iostream>

using namespace std;
using namespace erlent;

bool processMessage() {
    istream &is = cin;
    ostream &os = cout;

    Request *req = Request::receive(is);
    req->perform(os);
    return true;
}

int main(int argc, char *argv[])
{
    do {
    } while (processMessage());
    return 0;
}
