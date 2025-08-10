CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++20 -fsanitize=address
SRCDIR = src
SOURCES = $(SRCDIR)/main.cpp $(SRCDIR)/MattDaemon.cpp $(SRCDIR)/TintinReporter.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = Matt_daemon

all: $(TARGET)
	sudo mkdir -p /etc/matt_daemon
	sudo mkdir -p /var/log/matt_daemon
	sudo mkdir -p /var/lock
	sudo touch /var/lock/matt_daemon.lock
	sudo chmod 777 /var/lock/matt_daemon.lock
	sudo touch /var/log/matt_daemon/matt_daemon.log
	sudo chmod 0777 /var/log/matt_daemon/matt_daemon.log

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	sudo rm -f /var/log/matt_daemon/*.log
	sudo rm -f /var/lock/matt_daemon.lock

fclean: clean
	rm -f $(TARGET)
	sudo rm -f /var/log/matt_daemon/*.log
	sudo rm -f /var/lock/matt_daemon.lock

re: fclean all

.PHONY: all clean fclean re