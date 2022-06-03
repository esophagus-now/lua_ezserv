ezserv.so: ezserv.cpp
	g++ -shared -fPIC -g -Wall -fno-diagnostics-show-caret \
	-o ezserv.so ezserv.cpp -pthread