CFLAGS = -Wall -O3 
CC = gcc
PROGRAM = pingstat
MAIN = pingstat.c

# Just main
${PROGRAM} : pingstat.c
	${CC} ${CFLAGS} ${MAIN} -o ${PROGRAM}
clean:
	/bin/rm pingstat