#include "Point.h"
#include <stdio.h>

Point::Point() {
}

Point::Point(const Point &p) {

	mpz_set(x.num,p.x.num);
	mpz_set(y.num,p.y.num);
	mpz_set(z.num,p.z.num);
}

Point::Point(Int *cx,Int *cy,Int *cz) {
	mpz_set(x.num,cx->num);
	mpz_set(y.num,cy->num);
	mpz_set(z.num,cz->num);
}

void Point::Clear() {
	mpz_set_ui(x.num,0);
	mpz_set_ui(y.num,0);
	mpz_set_ui(z.num,0);
}

void Point::Set(Int *cx, Int *cy,Int *cz) {
	mpz_set(x.num,cx->num);
	mpz_set(y.num,cy->num);
	mpz_set(z.num,cz->num);
}

Point::~Point() {

}

void Point::Set(Point &p) {
	mpz_set(x.num,p.x.num);
	mpz_set(y.num,p.y.num);
	mpz_set(z.num,p.z.num);
}

bool Point::isZero() {
	return x.IsZero() && y.IsZero();
}

void Point::Reduce() {
	Int i(&z);
	i.ModInv();
	x.ModMul(&x,&i);
	y.ModMul(&y,&i);
	z.SetInt32(1); 
}

bool Point::equals(Point &p) {
	return x.IsEqual(&p.x) && y.IsEqual(&p.y) && z.IsEqual(&p.z);
}

Point& Point::operator=(const Point& other)  {
	if (this == &other) {
		return *this;
	}
	mpz_set(x.num,other.x.num);
	mpz_set(y.num,other.y.num);
	mpz_set(z.num,other.z.num);

	return *this;
}
