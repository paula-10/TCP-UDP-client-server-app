all: server subscriber

server: server.cpp
	g++ -g -o server server.cpp

subscriber: subscriber.cpp
	g++ -g -o subscriber subscriber.cpp

clean:
	rm -f server subscriber