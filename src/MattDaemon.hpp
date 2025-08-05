#pragma once

#include "TintinReporter.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>
#include <atomic>

class MattDaemon
{
public:
    MattDaemon();
    ~MattDaemon();
    MattDaemon(const MattDaemon &other) = delete;
    MattDaemon &operator=(const MattDaemon &other) = delete;
    void run();
    static void signalHandler(int signal);

private:
    static const int PORT = 4244;
    static const int MAX_CLIENTS = 3;
    static const std::string LOCK_FILE;
    static MattDaemon *instance;
    static std::atomic<bool> running;

    int serverSocket;
    int lockFileDescriptor;
    std::vector<int> clientSockets;
    TintinReporter &logger;

    bool checkRootPrivileges();
    bool createLockFile();
    void removeLockFile();
    void daemonize();
    bool createServer();
    void handleConnections();
    void acceptNewConnection(fd_set &readFds);
    void handleClientData(int clientSocket, fd_set &readFds);
    void closeAllConnections();
    void cleanup();
    void setupSignalHandlers();
};
