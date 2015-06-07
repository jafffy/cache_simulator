./sim -is 8192 -ds 8192 -bs 16 -a 1 -wb -wa test.trace > ./output/1.txt
./sim -is 65536 -ds 65536 -bs 16 -a 1 -wb -wa test.trace > ./output/2.txt
./sim -us 8192 -bs 32 -a 1 -wb -wa test.trace > ./output/3.txt
./sim -is 32768 -ds 32768 -bs 128 -a 1 -wb -wa test.trace > ./output/4.txt
./sim -is 8192 -ds 8192 -bs 64 -a 128 -wb -wa test.trace > ./output/5.txt
./sim -is 8192 -ds 8192 -bs 16 -a 1 -wt -wa test.trace > ./output/6.txt
./sim -is 8192 -ds 8192 -bs 64 -a 2 -wt -wa test.trace > ./output/7.txt
./sim -is 8192 -ds 8192 -bs 32 -a 1 -wb -nw test.trace > ./output/8.txt
./sim -is 8192 -ds 8192 -bs 64 -a 2 -wb -nw test.trace > ./output/9.txt
