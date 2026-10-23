# ---------------------------------------------------------------------------
# Origami Point Flat Folding SAT - build system
#
#   make            # same as `make normal`
#   make normal     # compile bin/main (objects go to obj/)
#   make run        # compile (if needed) and run with test_files/graph.in
#   make clean      # remove obj/ and bin/main
#   make plot       # render plot/instructions/origami.plot -> plot/img/origami.png
#   make plot-ccw   # render plot/instructions/ccw_test.plot -> plot/img/ccw_test.png
#   make plot-ccw-angles  # same, also printing the local angles (deg)
# ---------------------------------------------------------------------------

CXX      ?= g++
CXXFLAGS ?= -std=c++23 -Wall -Wextra
INCLUDES := -Iinclude

OBJDIR := obj
BINDIR := bin

SRCS   := src/main.cpp src/printing_functions.cpp
OBJS   := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS   := $(OBJS:.o=.d)
TARGET := $(BINDIR)/main

INPUT   ?= test_files/graph.in
PLOTTER ?= python3 scripts/graph_plotter.py

.PHONY: all normal run clean plot plot-ccw plot-ccw-angles

all: normal

# Default build: produces bin/main
normal: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

$(OBJDIR)/%.o: src/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJDIR) $(BINDIR):
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
