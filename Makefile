LC_ALL=CPP
LANG=CPP

all : main

clean : 
	rm main
main : 
	g++ -o main main.cpp -lwiringPi -Wall
