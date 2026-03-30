int return4() { return 4; }

int sub(int a, int b) { return a - b; }

long long test64(long long base) { return base * 2; }

int main() {
  int a = 7;
  int b = return4();
  int c = sub(a, b);
  long d = 255;
  long long twice = test64(1);
  return c;
}
