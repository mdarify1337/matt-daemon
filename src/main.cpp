#include "MattDaemon.hpp"
#include <iostream>
#include <unistd.h>

int main()
{
    // if (getuid() != 0) {
    //     std::cerr << "Error: You must run this program as root." << std::endl;
    //     return 1;
    // }
    std::cout << "Running as root!" << std::endl;
    try
    {
        MattDaemon daemon;
        daemon.run();
        // std::cout << "==> dkhal hna " << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}