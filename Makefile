CXX = clang++
CXXFLAGS = -std=c++11 -O3 -Wall
TARGET = lab1

# Default: compile
all: $(TARGET)

# Compile program
$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET)

# Run program
run: $(TARGET)
	./$(TARGET)

# Clean compiled files
clean:
	rm -f $(TARGET)

.PHONY: all run clean