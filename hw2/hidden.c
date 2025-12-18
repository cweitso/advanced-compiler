// hidden.c
void hidden() { 
  int a, b, c, x, y, *p1, *p2, **pp;
  p1 = &x;
  p2 = &y;
  pp = &p1; // pp -> p1 -> x
  *p1 = a + y; // x = a + y;
  **pp = *p2 + b; // x = y + b;
  y = a + b;
}