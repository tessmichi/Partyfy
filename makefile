compile: partyfy.c mongoose.c
	gcc -o partyfy partyfy.c mongoose.c

run: compile
	./partyfy