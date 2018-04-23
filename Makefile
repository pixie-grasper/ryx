TARGET    = ryx
SOURCE    = ryx.cc codegen.cc
OBJECT    = $(SOURCE:%.cc=%.o)
CXX       = clang++
LINK      = $(CXX)
CFLAGS    = -Weverything -Wno-padded -Wno-switch-enum -Wno-unused-macros -Wno-unused-function
CXXFLAGS  = -std=c++14 -Wno-c++98-compat -Wno-c++98-compat-pedantic
LINKFLAGS =

default: $(TARGET)

$(TARGET): $(OBJECT)
	$(LINK) $(LINKFLAGS) $^ -o $@

%.o: %.cc
	$(CXX) $(CFLAGS) $(CXXFLAGS) $< -o $@ -c

.PHONY: clean
clean:
	rm $(TARGET)

codegen.o: codegen.h

