#include "MattDaemon.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <cstring>
#include <algorithm>
#include <errno.h>
#include <cstdlib>

// Use local paths instead of system paths
const std::string MattDaemon::LOCK_FILE = "/var/lock/matt_daemon.lock";
MattDaemon *MattDaemon::instance = nullptr;
std::atomic<bool> MattDaemon::running{true};

MattDaemon::MattDaemon() : serverSocket(-1), lockFileDescriptor(-1), logger(TintinReporter::getInstance())
{
    instance = this;
    clientSockets.reserve(MAX_CLIENTS);
}

MattDaemon::~MattDaemon()
{
    cleanup();
}

bool MattDaemon::checkRootPrivileges()
{
    if (getuid() != 0)
    {
        std::cerr << "Matt_daemon must be run as root" << std::endl;
        return false;
    }
    return true;
}

bool MattDaemon::createLockFile()
{
    lockFileDescriptor = open(LOCK_FILE.c_str(), O_CREAT | O_RDWR, 0644);
    if (lockFileDescriptor == -1)
    {
        std::cerr << "Can't open lock file " << LOCK_FILE << ": " << strerror(errno) << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: Error opening lock file.");
        return false;
    }

    if (flock(lockFileDescriptor, LOCK_EX | LOCK_NB) == -1)
    {
        std::cerr << "Can't lock file " << LOCK_FILE << ": " << strerror(errno) << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: Error - daemon already running or file locked.");
        close(lockFileDescriptor);
        lockFileDescriptor = -1;
        return false;
    }

    std::string pidStr = std::to_string(getpid()) + "\n";
    if (write(lockFileDescriptor, pidStr.c_str(), pidStr.length()) == -1)
    {
        std::cerr << "Failed to write PID to lock file: " << strerror(errno) << std::endl;
    }

    std::cout << "Lock file created successfully: " << LOCK_FILE << std::endl;
    return true;
}

void MattDaemon::removeLockFile()
{
    if (lockFileDescriptor != -1)
    {
        flock(lockFileDescriptor, LOCK_UN);
        close(lockFileDescriptor);
        unlink(LOCK_FILE.c_str());
        lockFileDescriptor = -1;
        std::cout << "Lock file removed" << std::endl;
    }
}

void MattDaemon::daemonize()
{
    logger.log(TintinReporter::INFO, "Matt_daemon: Entering Daemon mode.");
    
    // First fork
    pid_t pid = fork();
    if (pid < 0)
    {
        logger.log(TintinReporter::ERROR, "Matt_daemon: First fork failed.");
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS); // Parent exits
    }

    
    if (setsid() < 0)
    {
        logger.log(TintinReporter::ERROR, "Matt_daemon: setsid failed.");
        exit(EXIT_FAILURE);
    }

    // Ignore SIGHUP
    signal(SIGHUP, SIG_IGN);

    // Second fork
    pid = fork();
    if (pid < 0)
    {
        logger.log(TintinReporter::ERROR, "Matt_daemon: Second fork failed.");
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS); // Parent exits
    }

    // Change working directory to home instead of root
    const char *home = getenv("HOME");
    if (home && chdir(home) < 0)
    {
        logger.log(TintinReporter::ERROR, "Matt_daemon: chdir to home failed.");
        if (chdir("/tmp") < 0)
        {
            logger.log(TintinReporter::ERROR, "Matt_daemon: chdir to /tmp failed.");
            exit(EXIT_FAILURE);
        }
    }

    // Set file permissions
    umask(0);

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect standard file descriptors to /dev/null
    int devNull = open("/dev/null", O_RDWR);
    if (devNull != -1)
    {
        dup2(devNull, STDIN_FILENO);
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        if (devNull > STDERR_FILENO)
        {
            close(devNull);
        }
    }

    logger.log(TintinReporter::INFO, "Matt_daemon: started. PID: " + std::to_string(getpid()) + ".");
}

bool MattDaemon::createServer()
{
    logger.log(TintinReporter::INFO, "Matt_daemon: Creating server.");
    logger.log(TintinReporter::INFO, "Matt_daemon: Server created.");
    logger.log(TintinReporter::INFO, "Matt_daemon: started. PID: " + std::to_string(getpid()) + ".");
    std::cout << "Creating server on port " << PORT << std::endl;
    
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        std::string error_msg = "Socket creation failed: " + std::string(strerror(errno));
        std::cerr << error_msg << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: " + error_msg);
        return false;
    }
    std::cout << "Socket created successfully: " << serverSocket << std::endl;

    // Set socket options
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        std::string error_msg = "setsockopt failed: " + std::string(strerror(errno));
        std::cerr << error_msg << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: " + error_msg);
        close(serverSocket);
        serverSocket = -1;
        return false;
    }
    std::cout << "Socket options set successfully" << std::endl;

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);
    
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::string error_msg = "Bind failed: " + std::string(strerror(errno));
        std::cerr << error_msg << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: " + error_msg);
        close(serverSocket);
        serverSocket = -1;
        return false;
    }
    std::cout << "Socket bound successfully to port " << PORT << std::endl;

    if (listen(serverSocket, MAX_CLIENTS) < 0)
    {
        std::string error_msg = "Listen failed: " + std::string(strerror(errno));
        std::cerr << error_msg << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: " + error_msg);
        close(serverSocket);
        serverSocket = -1;
        return false;
    }

    std::cout << "Server listening on port " << PORT << std::endl;
    logger.log(TintinReporter::INFO, "Matt_daemon: Server created on port " + std::to_string(PORT));
    return true;
}

void MattDaemon::setupSignalHandlers()
{
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    
    std::cout << "Signal handlers setup complete" << std::endl;
}

void MattDaemon::signalHandler(int signal)
{
    if (instance)
    {
        instance->logger.log(TintinReporter::INFO, "Matt_daemon: Received signal " + std::to_string(signal));
        running = false;
    }
}

void MattDaemon::acceptNewConnection(fd_set &readFds)
{
    if (!FD_ISSET(serverSocket, &readFds))
    {
        return;
    }

    if (clientSockets.size() >= MAX_CLIENTS)
    {
        int newSocket = accept(serverSocket, nullptr, nullptr);
        if (newSocket != -1)
        {
            std::cout << "Connection rejected - max clients reached" << std::endl;
            logger.log(TintinReporter::ERROR, "Matt_daemon: Connection rejected - max clients reached");
            close(newSocket);
        }
        return;
    }

    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int newSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
    
    if (newSocket == -1)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            std::string error_msg = "Accept failed: " + std::string(strerror(errno));
            std::cerr << error_msg << std::endl;
            logger.log(TintinReporter::ERROR, "Matt_daemon: " + error_msg);
        }
        return;
    }

    clientSockets.push_back(newSocket);
    std::string client_ip = std::string(inet_ntoa(clientAddr.sin_addr));
    std::cout << "New client connected from " << client_ip << std::endl;
    logger.log(TintinReporter::INFO, "Matt_daemon: New client connected from " + client_ip);
}

void MattDaemon::handleClientData(int clientSocket, fd_set &readFds)
{
    if (!FD_ISSET(clientSocket, &readFds))
    {
        return;
    }

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesReceived <= 0)
    {
        if (bytesReceived == 0)
        {
            std::cout << "Client disconnected" << std::endl;
            logger.log(TintinReporter::INFO, "Matt_daemon: Client disconnected");
        }
        else
        {
            std::string error_msg = "Recv error: " + std::string(strerror(errno));
            std::cerr << error_msg << std::endl;
            logger.log(TintinReporter::ERROR, "Matt_daemon: " + error_msg);
        }
        close(clientSocket);
        clientSockets.erase(std::remove(clientSockets.begin(), clientSockets.end(), clientSocket), 
            clientSockets.end());
        return;
    }
    std::string message(buffer, bytesReceived);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
    {
        message.pop_back();
    }

    if (message == "quit")
    {
        std::cout << "Quit command received" << std::endl;
        logger.log(TintinReporter::INFO, "Matt_daemon: Quit command received");
        running = false;
    }
    else if (!message.empty())
    {
        std::cout << "Received message: " << message << std::endl;
        logger.log(TintinReporter::LOG, "Matt_daemon: User input: " + message);
    }
}

void MattDaemon::handleConnections()
{
    fd_set readFds;
    int maxFd;
    struct timeval timeout;
    std::cout << "Starting connection handler..." << std::endl;
    logger.log(TintinReporter::INFO, "Matt_daemon: Starting connection handler");

    while (running)
    {
        FD_ZERO(&readFds);
        FD_SET(serverSocket, &readFds);
        maxFd = serverSocket;
        for (int clientSocket : clientSockets)
        {
            FD_SET(clientSocket, &readFds);
            if (clientSocket > maxFd)
            {
                maxFd = clientSocket;
            }
        }
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(maxFd + 1, &readFds, nullptr, nullptr, &timeout);

        if (activity < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            std::string error_msg = "select error: " + std::string(strerror(errno));
            std::cerr << error_msg << std::endl;
            logger.log(TintinReporter::ERROR, "Matt_daemon: " + error_msg);
            break;
        }

        if (activity > 0)
        {
            acceptNewConnection(readFds);
            for (auto it = clientSockets.rbegin(); it != clientSockets.rend(); ++it)
            {
                handleClientData(*it, readFds);
                if (!running) break;
            }
        }
    }

    std::cout << "Connection handler stopped" << std::endl;
    logger.log(TintinReporter::INFO, "Matt_daemon: Connection handler stopped");
}

void MattDaemon::closeAllConnections()
{
    std::cout << "Closing all connections..." << std::endl;
    for (int clientSocket : clientSockets)
    {
        close(clientSocket);
    }
    clientSockets.clear();
    
    if (serverSocket != -1)
    {
        close(serverSocket);
        serverSocket = -1;
    }
}

void MattDaemon::cleanup()
{
    std::cout << "Cleaning up..." << std::endl;
    logger.log(TintinReporter::INFO, "Matt_daemon: Cleaning up and quitting");
    closeAllConnections();
    removeLockFile();
}

void MattDaemon::run()
{
    std::cout << "Matt_daemon starting up..." << std::endl;
    logger.log(TintinReporter::INFO, "Matt_daemon: Starting up");

    std::cout << "Skipping root privilege check for testing..." << std::endl;

    // if (!createLockFile())
    // {
    //     std::cerr << "Failed to create lock file, quitting" << std::endl;
    //     logger.log(TintinReporter::ERROR, "Matt_daemon: Failed to create lock file, quitting");
    //     return;
    // }

    if (!createServer())
    {
        std::cerr << "Failed to create server, quitting" << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: Failed to create server, quitting");
        cleanup();
        return;
    }
    setupSignalHandlers();
    std::cout << "Skipping daemonization for debugging..." << std::endl;
    // daemonize();
    handleConnections();
    cleanup();
}