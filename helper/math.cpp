#include "math.h"

int ceilToNearest(int v, int m) {
	int f = v % m;
	int g = v / m;
	return g * m + (f != 0 ? 1 : 0);
}
int ceilDiv(int num, int den) {
	return (num + den - 1) / den;
}