#ifndef INTGROUPH
#define INTGROUPH
#include "Int.h"
class IntGroup {
public:
	IntGroup(int size);
	~IntGroup();
	void Set(Int *pts);
	void ModInv(int count = -1);
	Int *ints;
private:
	Int *subp;
	int size;
};
#endif
