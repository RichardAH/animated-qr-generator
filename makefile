all:
	gcc xpop.c qrcodegen.c ascii85.c -lbrotlienc -o xpop
