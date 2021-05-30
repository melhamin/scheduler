all: schedule

schedule: 
	gcc *.c -o schedule -g -lpthread -lm

clean:
	rm -fr schedule *dSYM