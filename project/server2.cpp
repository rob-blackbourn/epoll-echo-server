#include "io/event_loop.hpp"

using namespace jetblack::io;

int main()
{
    EventLoop event_loop;

    event_loop.start(5000);

    return 0;
}