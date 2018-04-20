TARGET   = ryx
SOURCE   = ryx.cc
CXX      = clang++
CFLAGS   = -Weverything -Wno-padded -Wno-switch-enum -Wno-unused-macros -Wno-unused-function
CXXFLAGS = -std=c++14 -Wno-c++98-compat -Wno-c++98-compat-pedantic

default: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $^ -o $@

.PHONY: clean
clean:
	rm $(TARGET)
