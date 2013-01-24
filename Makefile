oscprompt: main.cpp synth.cpp
	g++ -std=c++0x main.cpp synth.cpp -lrtosc -lcurses -lm -ljack -o oscprompt -g
