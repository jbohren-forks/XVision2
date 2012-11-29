MODULES	   = Images Tools Devices Consoles Tracking Segmentation drivers drivers/mpeg_lib-1.3.1 frp Examples 
#HOST="--host=i686-pc-cygwin"  
all: ;	@for i in  $(MODULES); \
	do (cd src/$$i; $(MAKE) install); done

install:
	mkdir -p $(prefix)/lib
	cp -a lib/* $(prefix)/lib
	mkdir -p $(prefix)/include/XVision2
	mkdir -p $(prefix)/bin
	cp include/*.h $(prefix)/include/XVision2
	cp include/*.icc $(prefix)/include/XVision2
	cp bin/* $(prefix)/bin


clean: ;	@for i in $(MODULES); \
	do (cd src/$$i; $(MAKE) clean); done

veryclean: clean
	rm -f lib/*
	rm -f bin/* 

dist: veryclean
	rm -f config.status config.log config.cache include/config.h
	(cd include; $(MAKE) clean)

config: ;	./configure 
		(cd include; $(MAKE) install)
		(cd src/Tools; $(MAKE) screendepth)
		mkdir -p bin lib
		@for i in $(MODULES);\
		do (cd src/$$i; $(MAKE) .depend); done
		@echo --- If there was no error, use \'make\' to compile. ---

doc: ;
	(cd docs; $(MAKE) doc)


# adds or updates the copyright from the Copyright file to each of the 
# *.h, *.cc, and *.icc files

addcr: ;
	@for i in $(MODULES);\
	do (cd src/$$i; $(MAKE) addcr); done
