#include "../inc/stddef.h"
#include "../inc/stdio.h"
#include "../inc/string.h"
#include "../inc/cocoapp.h"
#include "../inc/stdlib.h"

static int hash(int n)
{
	uint64 r = 6364136223846793005ULL * n + 1;
	return r >> 33;
}

static uint32 seed;

void srand(int s)
{
	seed = s;
}

uint32 rand()
{
	seed = hash(seed);
	return seed;
}