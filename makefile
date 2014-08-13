compile: partyfy.c mongoose.c
	gcc partyfy.c mongoose.c sp_key.c -pthread -o partyfy
run: compile
	./partyfy
