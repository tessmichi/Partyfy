compile: partyfy.c mongoose.c
	gcc -L./libspotify partyfy.c mongoose.c sp_key.c -pthread -lspotify -o partyfy
run: compile
	./partyfy
