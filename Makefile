TARGET    = ryx
SOURCE    = ryx.cc codegen.cc
OBJECT    = $(SOURCE:%.cc=%.o)
CXX       = clang++
LINK      = $(CXX)
CFLAGS    = -Weverything -Wno-padded -Wno-switch-enum -Wno-unused-macros -Wno-unused-function
CXXFLAGS  = -std=c++14 -Wno-c++98-compat -Wno-c++98-compat-pedantic
LINKFLAGS =

HOST      = $(shell uname)
NJOB      = 1

# Linux
ifeq ($(HOST),Linux)
	ifeq ($(shell sh -c 'if which nproc > /dev/null 2>&1; then echo 1; else echo 0; fi'),1)
		NJOB = $(shell nproc)
	endif
else
	NJOB = 2
endif

default: in_parallel

.PHONY: in_parallel
in_parallel:
	@$(MAKE) $(TARGET) -j $(NJOB)

$(TARGET): $(OBJECT)
	$(LINK) $(LINKFLAGS) $^ -o $@

%.o: %.cc
	$(CXX) $(CFLAGS) $(CXXFLAGS) $< -o $@ -c

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJECT) ryx_parse*

codegen.o: codegen.h

