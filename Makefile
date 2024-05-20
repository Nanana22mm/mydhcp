.PHONY: all clean
all: mydhcpc mydhcpd

mydhcpc: mydhcpc.c mydhcp.h
	gcc -Wall -Wextra -o mydhcpc mydhcpc.c

mydhcpd: mydhcpd.c list.h mydhcp.h
	gcc -Wall -Wextra -o mydhcpd mydhcpd.c list.c

clean:
	rm -r mydhcpc mydhcpd 
