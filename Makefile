LC_ALL=CPP
LANG=CPP

all : main

main : 
	g++ -o main main.cpp -lwiringPi -Wall
