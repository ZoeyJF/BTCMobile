#ifndef INTGROUPH

#define INTGROUPH



#include "Int.h"

#include <vector>



class IntGroup {

public:

	IntGroup(int size);

	~IntGroup();

	void Set(Int *pts);

	void ModInv();

        Int *ints;

private:

	Int *subp;

	int size;

};



#endif


