CC = gcc
DEPS = solver.h sudoku.h util.h
SRCS = solver.c sudoku.c util.c main.c
OBJS = $(SRCS:.c=.o)
CFLAGS = -std=c99 -Wall -Werror
LDFLAGS =
EXEC = gensudoku

all : $(EXEC)

$(EXEC) : $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o : %.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY : clean all
clean :
	rm -f *.o
	rm -f $(EXEC)
