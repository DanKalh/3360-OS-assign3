Firstly, to compile:

 g++ -std=c++11 -Wall -pthread main.cpp -o main

to run my code may be a little different, at the end I pass through a string dictating with algorithm to run (no quotes) e.g.:

./main text.txt ALL

"LIFO" "MRU" "LRU-X" "LFU" "OPT-lookahead-X" "WS" and "ALL"

The code reads the input file, gets the configs for the simulation and implements the different page-replacement algorithms to manage pages within precesses. Used semaphores/mutexes for concurrency and synchronization, simulate is the main function, then a cleanup. I attempted the bonus, but am swamped with other finals so please forgive any vestiges.