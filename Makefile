all: oscprompt-backend oscprompt-frontend

oscprompt-backend: synth.cpp
	g++ -std=c++0x synth.cpp -lrtosc -llo -lm -ljack -o oscprompt-backend -g

oscprompt-frontend: main.cpp
	g++ -std=c++0x main.cpp -lrtosc -lcurses -lm -llo -o oscprompt-frontend -g

clean:
	rm *.o oscprompt-frontend oscprompt-backend
