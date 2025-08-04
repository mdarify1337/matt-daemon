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

const std::string MattDaemon::LOCK_FILE = "/var/lock/matt_daemon.lock";
MattDaemon* MattDaemon::instance = nullptr;
std::atomic<bool> MattDaemon::running{true};

MattDaemon::MattDaemon() : serverSocket(-1), lockFileDescriptor(-1), logger(TintinReporter::getInstance()) {
    instance = this;
    clientSockets.reserve(MAX_CLIENTS);
}

MattDaemon::~MattDaemon() {
    cleanup();
}

bool MattDaemon::checkRootPrivileges() {
    if (getuid() != 0) {
        std::cerr << "Matt_daemon must be run as root" << std::endl;
        return false;
    }
    return true;
}

bool MattDaemon::createLockFile() {
    lockFileDescriptor = open(LOCK_FILE.c_str(), O_CREAT | O_RDWR, 0644);
    if (lockFileDescriptor == -1) {
        std::cerr << "Can't open :" << LOCK_FILE << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: Error file locked.");
        return false;
    }

    if (flock(lockFileDescriptor, LOCK_EX | LOCK_NB) == -1) {
        std::cerr << "Can't open :" << LOCK_FILE << std::endl;
        logger.log(TintinReporter::ERROR, "Matt_daemon: Error file locked.");
        close(lockFileDescriptor);
        return false;
    }

    return true;
}

void MattDaemon::removeLockFile() {
    if (lockFileDescriptor != -1) {
        flock(lockFileDescriptor, LOCK_UN);
        close(lockFileDescriptor);
        unlink(LOCK_FILE.c_str());
        lockFileDescriptor = -1;
    }
}

void MattDaemon::daemonize() {
    logger.log(TintinReporter::INFO, "Matt_daemon: Entering Daemon mode.");

    // First fork
    pid_t pid = fork();
    if (pid < 0) {
        logger.log(TintinReporter::ERROR, "Matt_daemon: First fork failed.");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Parent exits
    }

    // Create new session
    if (setsid() < 0) {
        logger.log(TintinReporter::ERROR, "Matt_daemon: setsid failed.");
        exit(EXIT_FAILURE);
    }

    // Second fork
    pid = fork();
    if (pid < 0) {
        logger.log(TintinReporter::ERROR, "Matt_daemon: Second fork failed.");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Parent exits
    }

    // Change working directory
    if (chdir("/") < 0) {
        logger.log(TintinReporter::ERROR, "Matt_daemon: chdir failed.");
        exit(EXIT_FAILURE);
    }

    // Set file mode creation mask
    umask(0);

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect standard file descriptors to /dev/null
    int devNull = open("/dev/null", O_RDWR);
    if (devNull != -1) {
        dup2(devNull, STDIN_FILENO);
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        if (devNull > STDERR_FILENO) {
            close(devNull);
        }
    }

    logger.log(TintinReporter::INFO, "Matt_daemon: started. PID: " + std::to_string(getpid()) + ".");
}

bool MattDaemon::createServer() {
    logger.log(TintinReporter::INFO, "Matt_daemon: Creating server.");

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        logger.log(TintinReporter::ERROR, "Matt_daemon: Socket creation failed.");
        return false;
    }

    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logger.log(TintinReporter::ERROR, "Matt_daemon: setsockopt failed.");
        close(serverSocket);
        return false;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        logger.log(TintinReporter::ERROR, "Matt_daemon: Bind failed.");
        close(serverSocket);
        return false;
    }

    if (listen(serverSocket, MAX_CLIENTS) < 0) {
        logger.log(TintinReporter::ERROR, "Matt_daemon: Listen failed.");
        close(serverSocket);
        return false;
    }

    logger.log(TintinReporter::INFO, "Matt_daemon: Server created.");
    return true;
}

void MattDaemon::setupSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}

void MattDaemon::signalHandler(int signal) {
    std::cout << "signal ==> " << signal << std::endl;
    if (instance) {
        instance->logger.log(TintinReporter::INFO, "Matt_daemon: Signal handler.");
        running = false;
    }
}

void MattDaemon::acceptNewConnection(fd_set& readFds) {
    if (!FD_ISSET(serverSocket, &readFds)) {
        return;
    }

    if (clientSockets.size() >= MAX_CLIENTS) {
        // Accept and immediately close if max clients reached
        int newSocket = accept(serverSocket, nullptr, nullptr);
        if (newSocket != -1) {
            close(newSocket);
        }
        return;
    }

    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int newSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
    
    if (newSocket != -1) {
        clientSockets.push_back(newSocket);
        logger.log(TintinReporter::INFO, "Matt_daemon: New client connected.");
    }
}

void MattDaemon::handleClientData(int clientSocket, fd_set& readFds) {
    if (!FD_ISSET(clientSocket, &readFds)) {
        return;
    }

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesReceived <= 0) {
        // Client disconnected
        close(clientSocket);
        clientSockets.erase(std::remove(clientSockets.begin(), clientSockets.end(), clientSocket), clientSockets.end());
        logger.log(TintinReporter::INFO, "Matt_daemon: Client disconnected.");
        return;
    }

    // Remove newline characters
    std::string message(buffer, bytesReceived);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
    }

    if (message == "quit") {
        logger.log(TintinReporter::INFO, "Matt_daemon: Request quit.");
        running = false;
    } else if (!message.empty()) {
        logger.log(TintinReporter::LOG, "Matt_daemon: User input: " + message);
    }
}

void MattDaemon::handleConnections() {
    fd_set readFds;
    int maxFd;
    struct timeval timeout;

    while (running) {
        FD_ZERO(&readFds);
        FD_SET(serverSocket, &readFds);
        maxFd = serverSocket;

        for (int clientSocket : clientSockets) {
            FD_SET(clientSocket, &readFds);
            if (clientSocket > maxFd) {
                maxFd = clientSocket;
            }
        }

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(maxFd + 1, &readFds, nullptr, nullptr, &timeout);
        
        if (activity < 0 && errno != EINTR) {
            logger.log(TintinReporter::ERROR, "Matt_daemon: select error.");
            break;
        }

        if (activity > 0) {
            acceptNewConnection(readFds);

            // Handle existing clients (iterate backwards to safely remove)
            for (auto it = clientSockets.rbegin(); it != clientSockets.rend(); ++it) {
                handleClientData(*it, readFds);
            }
        }
    }
}

void MattDaemon::closeAllConnections() {
    for (int clientSocket : clientSockets) {
        close(clientSocket);
    }
    clientSockets.clear();

    if (serverSocket != -1) {
        close(serverSocket);
        serverSocket = -1;
    }
}

void MattDaemon::cleanup() {
    logger.log(TintinReporter::INFO, "Matt_daemon: Quitting.");
    closeAllConnections();
    removeLockFile();
}

void MattDaemon::run() {
    if (!checkRootPrivileges()) {
        return;
    }

    logger.log(TintinReporter::INFO, "Matt_daemon: Started.");

    if (!createLockFile()) {
        logger.log(TintinReporter::INFO, "Matt_daemon: Quitting.");
        return;
    }

    if (!createServer()) {
        cleanup();
        return;
    }

    setupSignalHandlers();
    daemonize();
    handleConnections();
    cleanup();
}