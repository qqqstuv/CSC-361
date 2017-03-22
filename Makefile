compile: rdps.c rdpr.c
	gcc -Wall -g -o rdpr rdpr.c sendlogic.c
	gcc -Wall -g -o rdps rdps.c sendlogic.c
clean:
	rm -f rdps
	rm -f rdpr
	rm -f received.dat
runs:
	./rdps 192.168.1.100 6969 10.10.1.100 6969 small.txt

runr:
	./rdpr 10.10.1.100 6969 received.dat

runsl:
	./rdps 192.168.1.100 6969 10.10.1.100 6969 small.txt | tee slog.txt

runrl:
	./rdpr 10.10.1.100 6969 received.dat | tee rlog.txt

isvalid:
	md5sum small.txt received.dat
