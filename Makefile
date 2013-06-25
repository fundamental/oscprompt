all: oscprompt-backend oscprompt-frontend

LIBS = -lrtosc -lrtosc-cpp -llo -lm

CXX = clang++

oscprompt-backend: synth.cpp
	$(CXX) -std=c++11 synth.cpp $(LIBS) -ljack -o oscprompt-backend -g -Wall -Wextra

oscprompt-frontend: main.cpp
	$(CXX) -std=c++11 main.cpp $(LIBS) -lcurses -o oscprompt-frontend -g -Wall -Wextra

clean:
	rm -f *.o oscprompt-frontend oscprompt-backend
