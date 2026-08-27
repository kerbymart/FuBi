extern "C" int CdeclZero() { return 17; }
extern "C" int CdeclAdd(int left, int right) { return left + right; }
extern "C" int __stdcall StdcallAdd(int left, int right) { return left + right; }
extern "C" int CdeclFour(int a, int b, int c, int d) { return a * 1000 + b * 100 + c * 10 + d; }
extern "C" int CdeclEight(int a, int b, int c, int d, int e, int f, int g, int h) { return a + b + c + d + e + f + g + h; }
