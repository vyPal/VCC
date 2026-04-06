int sub(int a, int b) { return a - b; }

int takes_pointer(int *a) { return *a; }

int main() {
  int a = 7;
  int c = sub(a, 4);
  c = takes_pointer(&c);
  return c;
}
