ifeq ($(DEBUG),true)

FLAGS=-D DEBUG
else
FLAGS=
endif

build:
	gcc -o hyc -g main.c -lev -std=c99 $(FLAGS)
