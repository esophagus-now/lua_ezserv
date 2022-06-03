ezserv.so: ezserv.cpp
	g++ -shared -fPIC -g -Wall -fno-diagnostics-show-caret \
	-o ezserv.so ezserv.cpp -pthread

run_valgrind: ezserv.so
	valgrind --leak-check=full lua ideal.lua 2>vgrind.txt