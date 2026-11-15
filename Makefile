# ---------------------------------------------------------------------------
# Origami Point Flat Folding SAT - build system
#
# Layout (all sources live under code/):
#   code/main.cpp                 entry point: named graphs + command console
#   code/oriGraphType/            graph_types.hpp/.cpp (origamiGraph, independent of the solver)
#   code/solver/include/          mv_solver.hpp (cvc5 encoder)
#   code/solver/src/              mv_solver.cpp
#   code/plot_generator/include/  printing_functions.hpp
#   code/plot_generator/src/      printing_functions.cpp
#   code/plot_generator/scripts/  graph_plotter.py
#
#   make            # same as `make normal`
#   make normal     # compile bin/main (objects go to obj/, mirroring code/ paths)
#   make run        # compile (if needed) and start bin/main (commands come from stdin)
#   make clean      # remove obj/ and bin/main
#   make plot       # render plot/instructions/origami.txt -> plot/img/origami.png
#   make plot-ccw   # render plot/instructions/ccw_test.txt -> plot/img/ccw_test.png
#   make plot-ccw-angles  # same, also printing the local angles (deg)
# ---------------------------------------------------------------------------

CXX      ?= g++
CXXFLAGS ?= -std=c++23 -Wall -Wextra

# cvc5 (SMT solver). Prefer the local 1.4.2 install (TermManager API); fall back
# to the distro package (-lcvc5) when that prefix is missing.
CVC5_PREFIX ?= $(HOME)/.local/opt/cvc5-1.4.2
ifeq ($(wildcard $(CVC5_PREFIX)/include/cvc5/cvc5.h),)
CVC5_CFLAGS := $(shell pkg-config --cflags cvc5 2>/dev/null)
CVC5_LIBS   := $(shell pkg-config --libs cvc5 2>/dev/null || echo -lcvc5)
else
CVC5_CFLAGS := -I$(CVC5_PREFIX)/include
CVC5_LIBS   := -L$(CVC5_PREFIX)/lib -lcvc5 -Wl,-rpath,$(CVC5_PREFIX)/lib
endif

CODE_DIR   := code
GRAPH_INC  := $(CODE_DIR)/oriGraphType
SOLVER_INC := $(CODE_DIR)/solver/include
PLOT_INC   := $(CODE_DIR)/plot_generator/include

INCLUDES := -I$(GRAPH_INC) -I$(SOLVER_INC) -I$(PLOT_INC) $(CVC5_CFLAGS)

OBJDIR := obj
BINDIR := bin

# Sources: the console entry point plus everything it links against.
LIBSRCS := $(CODE_DIR)/oriGraphType/graph_types.cpp \
           $(CODE_DIR)/solver/src/mv_solver.cpp \
           $(CODE_DIR)/plot_generator/src/printing_functions.cpp

SRCS   := $(CODE_DIR)/main.cpp $(LIBSRCS)
OBJS   := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS   := $(OBJS:.o=.d)
TARGET := $(BINDIR)/main

PLOTTER ?= python3 $(CODE_DIR)/plot_generator/scripts/graph_plotter.py

.PHONY: all normal run clean plot plot-ccw plot-ccw-angles

all: normal

# Default build: produces bin/main
normal: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(CVC5_LIBS)

# Objects mirror the source tree: obj/code/solver/src/mv_solver.o, ...
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BINDIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

plot:
	$(PLOTTER) --no-show

plot-ccw:
	$(PLOTTER) --ccw --no-show

plot-ccw-angles:
	$(PLOTTER) --ccw --angles --no-show -o plot/img/ccw_test_angles.png

clean:
	rm -rf $(OBJDIR) $(TARGET)

-include $(DEPS)
