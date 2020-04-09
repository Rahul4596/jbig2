all:
	@make -C jbig2dec-0.10
	@make -C leptonlib-1.58/src
	@make -C agl-jbig2enc-a13b7e8

clean:
	@make -C agl-jbig2enc-a13b7e8 clean
	@make -C leptonlib-1.58/src clean
	@make -C jbig2dec-0.10 clean

distclean:
	@make -C agl-jbig2enc-a13b7e8 clean
	@make -C leptonlib-1.58/src clean
	@make -C jbig2dec-0.10 distclean
