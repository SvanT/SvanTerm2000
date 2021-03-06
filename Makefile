all: asdsvanterm.o asdfindwindow.o asdterminal.o asdterminalwindow.o
	g++ -O3 build/svanterm.o build/findwindow.o build/terminal.o build/terminalwindow.o -o svanterm `pkg-config --libs gtk+-3.0 vte-2.91 gtkmm-3.0` -l notify

asdsvanterm.o:
	mkdir -p build
	g++ -O3 -g -std=c++11 -c `pkg-config --cflags gtk+-3.0 gtkmm-3.0` svanterm.cpp -o build/svanterm.o

asdfindwindow.o:
	mkdir -p build
	g++ -O3 -g -std=c++11 -c `pkg-config --cflags gtk+-3.0 gtkmm-3.0` findwindow.cpp -o build/findwindow.o

asdterminal.o:
	mkdir -p build
	g++ -O3 -g -std=c++11 -c `pkg-config --cflags gtk+-3.0 gtkmm-3.0` terminal.cpp -o build/terminal.o

asdterminalwindow.o:
	mkdir -p build
	g++ -O3 -g -std=c++11 -c `pkg-config --cflags gtk+-3.0 gtkmm-3.0` terminalwindow.cpp -o build/terminalwindow.o
