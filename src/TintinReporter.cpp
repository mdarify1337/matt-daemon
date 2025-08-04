#include "TintinReporter.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>

const std::string TintinReporter::LOG_DIR = "/var/log/matt_daemon/";
const std::string TintinReporter::LOG_FILE = LOG_DIR + "matt_daemon.log";

TintinReporter::TintinReporter()
{
    createLogDirectory();
    logFile.open(LOG_FILE, std::ios::app);
    if (!logFile.is_open())
    {
        std::cerr << "Failed to open log file: " << LOG_FILE << std::endl;
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
    createLogDirectory();
    logFile.open(LOG_FILE, std::ios::app);
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
}

TintinReporter &TintinReporter::getInstance()
{
    static TintinReporter instance;
    return instance;
}
