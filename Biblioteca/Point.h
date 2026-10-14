#ifndef POINTH
#define POINTH
#include "Int.h"
class Point {
public:
	Point();
	Point(Int *x, Int *y, Int *z);
	Point(Int *x, Int *y);
	Point(Int x, Int y);
	Point(const Point &p);
	~Point();
	void Set(Int *x, Int *y, Int *z);
	void Set(Int *x, Int *y);
	void Set(Point &p);
	void Clear();
	void Reduce();
	Int x;
	Int y;
	Int z;
	Point& operator=(const Point& other);
};
#endif
