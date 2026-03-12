int return4() { return 4; }

int sub(int a, int b) { return a - b; }

int main() {
  int a = 7;
  int b = return4();
  int c = sub(a, b);
  long d = 255;
  return c;
}
