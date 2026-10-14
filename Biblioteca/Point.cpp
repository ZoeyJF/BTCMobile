#include "Point.h"
Point::Point() {
	Clear();
}
Point::Point(Int *x, Int *y, Int *z) {
	this->x.Set(x);
	this->y.Set(y);
	this->z.Set(z);
}
Point::Point(Int *x, Int *y) {
	this->x.Set(x);
	this->y.Set(y);
	this->z.SetInt32(1);
}
Point::Point(Int x, Int y) {
	this->x.Set(&x);
	this->y.Set(&y);
	this->z.SetInt32(1);
}
Point::Point(const Point &p) {
	this->x.Set(&p.x);
	this->y.Set(&p.y);
	this->z.Set(&p.z);
}
Point::~Point() {
}
Point& Point::operator=(const Point& other) {
	if (this != &other) {
		this->x.Set(&other.x);
		this->y.Set(&other.y);
		this->z.Set(&other.z);
	}
	return *this;
}
void Point::Set(Int *x, Int *y, Int *z) {
	this->x.Set(x);
	this->y.Set(y);
	this->z.Set(z);
}
void Point::Set(Int *x, Int *y) {
	this->x.Set(x);
	this->y.Set(y);
	this->z.SetInt32(1);
}
void Point::Set(Point &p) {
	this->x.Set(&p.x);
	this->y.Set(&p.y);
	this->z.Set(&p.z);
}
void Point::Clear() {
	x.CLEAR();
	y.CLEAR();
	z.CLEAR();
}
void Point::Reduce() {
	if (z.IsOne() || z.IsZero()) return;
	Int z_inv(&z);
	z_inv.ModInv();
	x.ModMulK1(&z_inv);
	y.ModMulK1(&z_inv);
	z.SetInt32(1);
}
