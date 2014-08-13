compile: partyfy.c mongoose.c
	gcc partyfy.c mongoose.c -pthread -o partyfy
run: compile
	./partyfy
