CC = gcc

all: dsk2nib nib2dsk

clean:
	@rm -f *.o
	@rm -f dsk2nib
	@rm -f nib2dsk

dsk2nib: dsk2nib.o

nib2dsk: nib2dsk.o

.c.o:
	$(CC) $(CFLAGS) -c $<
