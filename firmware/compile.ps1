sdcc -mmcs51 -c ./gpif.c
sdcc -mmcs51 -c ./fbKeyEmu.c
sdcc fbKeyEmu.rel gpif.rel

./gen_inc.ps1 > fbKeyEmu.inc
