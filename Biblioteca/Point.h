#ifndef POINTH
#define POINTH

#include "Int.h"

class Point {
public:
	Point();
	Point(Int *cx, Int *cy,Int *cz);
	Point& operator=(const Point& other);
	Point(const Point &p);
	~Point();
	bool isZero();
	bool equals(Point &p);
	void Set(Point &p);
	void Set(Int *cx, Int *cy,Int *cz);
	void Clear();
	void Reduce();
	void print(const char *str);
	Int x;
	Int y;
	Int z;
};

#endif