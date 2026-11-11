# ---------------------------------------------------------------------------
# Origami Point Flat Folding SAT - build system
#
# Layout:
#   solver/include/          graph_types.hpp, mv_solver.hpp (cvc5 encoder)
#   solver/src/              main.cpp (input + geometry), mv_solver.cpp
#   plot_generator/include/  printing_functions.hpp
#   plot_generator/src/      printing_functions.cpp
#   plot_generator/scripts/  graph_plotter.py
#
#   make            # same as `make normal`
#   make normal     # compile bin/main (objects go to obj/, mirroring src paths)
#   make run        # compile (if needed) and run with test_files/graph.in
#   make clean      # remove obj/ and bin/main
#   make plot       # render plot/instructions/origami.plot -> plot/img/origami.png
#   make plot-ccw   # render plot/instructions/ccw_test.plot -> plot/img/ccw_test.png
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

INCLUDES := -Isolver/include -Iplot_generator/include $(CVC5_CFLAGS)

OBJDIR := obj
BINDIR := bin

SRCS   := solver/src/main.cpp \
          solver/src/mv_solver.cpp \
          plot_generator/src/printing_functions.cpp
OBJS   := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS   := $(OBJS:.o=.d)
TARGET := $(BINDIR)/main

INPUT   ?= test_files/graph.in
PLOTTER ?= python3 plot_generator/scripts/graph_plotter.py

.PHONY: all normal run clean plot plot-ccw plot-ccw-angles

all: normal

# Default build: produces bin/main
normal: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(CVC5_LIBS)

# Objects mirror the source tree: obj/solver/src/main.o, ...
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BINDIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET) < $(INPUT)

plot:
	$(PLOTTER) --no-show

plot-ccw:
	$(PLOTTER) --ccw --no-show

plot-ccw-angles:
	$(PLOTTER) --ccw --angles --no-show -o plot/img/ccw_test_angles.png

clean:
	rm -rf $(OBJDIR) $(TARGET)

-include $(DEPS)
