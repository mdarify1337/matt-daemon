#pragma once

#include <fstream>
#include <string>
#include <mutex>

class TintinReporter {
public:
    enum LogLevel {
        INFO,
        ERROR,
        LOG
    };

    // Coplien form
    TintinReporter();
    ~TintinReporter();
    TintinReporter(const TintinReporter& other);
    TintinReporter& operator=(const TintinReporter& other);

    void log(LogLevel level, const std::string& message);
    static TintinReporter& getInstance();

private:
    std::ofstream logFile;
    std::mutex logMutex;
    static const std::string LOG_DIR;
    static const std::string LOG_FILE;
    
    void createLogDirectory();
    std::string getCurrentTimestamp();
    std::string levelToString(LogLevel level);
};
