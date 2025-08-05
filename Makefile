CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++20 -fsanitize=address
SRCDIR = src
SOURCES = $(SRCDIR)/main.cpp $(SRCDIR)/MattDaemon.cpp $(SRCDIR)/TintinReporter.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = Matt_daemon

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

fclean: clean
	rm -f $(TARGET)

re: fclean all

.PHONY: all clean fclean re