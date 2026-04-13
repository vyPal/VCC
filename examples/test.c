int sub(int a, int b) { return a - b; }

int takes_pointer(int *a) { return *a; }
int takes_nested_pointer(int **a) { return takes_pointer(*a); }

int main() {
  int a = 7;
  int c = sub(a, 4);
  int *d = &c;
  c = takes_nested_pointer(&d);
  return c == 3;
}
