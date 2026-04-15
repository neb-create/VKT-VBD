#include "math.h"

int ceilToNearest(int v, int m) {
	int f = v % m;
	int g = v / m;
	return g * m + (f != 0 ? 1 : 0);
}
int ceilDiv(int num, int den) {
	return (num + den - 1) / den;
}

float trace(glm::mat2x2 m) {
	return m[0][0] + m[1][1];
}