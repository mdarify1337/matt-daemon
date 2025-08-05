#include "TintinReporter.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <errno.h>
#include <cstring>
#include <asm-generic/fcntl.h>


const std::string TintinReporter::LOG_DIR = "/var/lock/matt_daemon/";  // <-- use /tmp or $HOME for dev
const std::string TintinReporter::LOG_FILE = LOG_DIR + "matt_daemon.log";

TintinReporter::TintinReporter()
{
    // Use home directory for logs
    // const char *home = std::getenv("HOME");
    // std::string logPath =  + "/matt_daemon.log";
    
    // std::cout << "Attempting to create log file at: " << logPath << std::endl;
    logFile.open(LOG_FILE, std::ios::app);
    
    if (!logFile.is_open())
    {
        // Fallback to current directory
        std::string fallbackPath = "./matt_daemon.log";
        std::cerr << "Failed to open home log file: " <<  fallbackPath
                  << ", trying current directory: " << fallbackPath << std::endl;
        
        logFile.open(fallbackPath, std::ios::app);
        
        if (!logFile.is_open())
        {
            std::cerr << "Failed to open fallback log file: " << fallbackPath << std::endl;
        }
        else
        {
            std::cout << "Log file created at: " << fallbackPath << std::endl;
        }
    }
    else
    {
        std::cout << "Log file created at: " << LOG_FILE << std::endl;
    }
}

TintinReporter::~TintinReporter()
{
    if (logFile.is_open())
    {
        logFile.close();
    }
}

TintinReporter::TintinReporter(const TintinReporter &other)
{
    (void)other;
    // const char *home = std::getenv("HOME");
    // std::string logPath = (home ? std::string(home) : ".") + "/matt_daemon.log";
    logFile.open(LOG_FILE, std::ios::app);
    
    if (!logFile.is_open())
    {
        std::string fallbackPath = "./matt_daemon.log";
        logFile.open(fallbackPath, std::ios::app);
    }
}

TintinReporter &TintinReporter::operator=(const TintinReporter &other)
{
    if (this != &other)
    {
        if (logFile.is_open())
        {
            logFile.close();
        }
        createLogDirectory();
        logFile.open(LOG_FILE, std::ios::app);
    }
    return *this;
}

void TintinReporter::createLogDirectory()
{
    struct stat st;
    if (stat(LOG_DIR.c_str(), &st) != 0)
    {
        mkdir(LOG_DIR.c_str(), 0755);
    }
}
std::string TintinReporter::getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(2) << tm.tm_mday << "/"
       << std::setw(2) << (tm.tm_mon + 1) << "/" << (tm.tm_year + 1900) << "-"
       << std::setw(2) << tm.tm_hour << ":" << std::setw(2) << tm.tm_min << ":"
       << std::setw(2) << tm.tm_sec << "]";
    return ss.str();
}

std::string TintinReporter::levelToString(LogLevel level)
{
    switch (level)
    {
    case INFO:
        return "[ INFO ]";
    case ERROR:
        return "[ ERROR ]";
    case LOG:
        return "[ LOG ]";
    default:
        return "[ UNKNOWN ]";
    }
}

void TintinReporter::log(LogLevel level, const std::string &message)
{
    std::lock_guard<std::mutex> lock(logMutex);
    
    if (logFile.is_open())
    {
        logFile << getCurrentTimestamp() << " " << levelToString(level)
                << " - " << message << std::endl;
        logFile.flush();
    }
    
    // Also output to console for debugging
    std::cout << getCurrentTimestamp() << " " << levelToString(level)
              << " - " << message << std::endl;
}

TintinReporter &TintinReporter::getInstance()
{
    static TintinReporter instance;
    return instance;
}