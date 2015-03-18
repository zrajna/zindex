include ../Makefile

PROJNAME = znzlib

INCFLAGS = $(ZLIB_INC)
LIBS = $(ZLIB_LIBS) $(ZNZ_LIBS)

SRCS=znzlib.c zindex.c
OBJS=znzlib.o zindex.o

TESTXFILES = testprog

depend:	
	$(RM) -f depend.mk
	$(MAKE) depend.mk

depend.mk:
	$(CC) $(DEPENDFLAGS) $(INCFLAGS) $(SRCS) >> depend.mk

lib: libznz.a

test: $(TESTXFILES)

znzlib.o: znzlib.c znzlib.h
	$(CC) -fPIC -c $(CFLAGS) $(USEZLIB) $(INCFLAGS) $<

zindex.o: zindex.c zindex.h
	$(CC) -fPIC -c $(CFLAGS) $(USEZLIB) $(INCFLAGS) $<

libznz.a: $(OBJS)
	$(AR) -r libznz.a $(OBJS)
	$(RANLIB) $@
	$(CC) -shared -o libznz.so.2.zindex znzlib.o zindex.o -L./ -lznz

testprog: libznz.a testprog.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o testprog testprog.c $(ZLIB_LIBS)

zindex: zindex.o main.c
	$(CC) -o $@ $^ $(CFLAGS) $(ZLIB_LIBS)

include depend.mk
