#include "IntGroup.h"

IntGroup::IntGroup(int size) {
  this->size = size;
  subp = new Int[size];
}

IntGroup::~IntGroup() {
  delete[] subp;
}

void IntGroup::Set(Int *pts) {
  ints = pts;
}

void IntGroup::ModInv(int count) {
  int n = (count <= 0 || count > size) ? size : count;
  if (n <= 0) return;
  if (n == 1) {
    ints[0].ModInv();
    return;
  }
  Int newValue;
  Int inverse;
  subp[0].Set(&ints[0]);
  for (int i = 1; i < n; i++) {
    subp[i].ModMulK1(&subp[i - 1], &ints[i]);
  }
  inverse.Set(&subp[n - 1]);
  inverse.ModInv();
  for (int i = n - 1; i > 0; i--) {
    newValue.ModMulK1(&subp[i - 1], &inverse);
    inverse.ModMulK1(&ints[i]);
    ints[i].Set(&newValue);
  }
  ints[0].Set(&inverse);
}
