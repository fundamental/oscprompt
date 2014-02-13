all: oscprompt example-backend

LIBS = -lrtosc -lrtosc-cpp -llo -lm

CXX = clang++
CXXFLAGS = -std=c++11 -g -Wall -Wextra

example-backend: example.cpp
	$(CXX) $(CXXFLAGS)  example.cpp $(LIBS) -ljack -o example-backend

oscprompt: main.cpp render.cpp
	$(CXX) $(CXXFLAGS)  main.cpp render.cpp $(LIBS) -lcurses -o oscprompt

clean:
	rm -f *.o *~ oscprompt example-backend
