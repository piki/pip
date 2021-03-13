/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef BOOLARRAY_H
#define BOOLARRAY_H

#include <assert.h>
#include <string.h>

class BoolArray {
public:
	BoolArray(int size) {
		int realsz = (size + 7) / 8;
		data = new unsigned char[realsz];
		bzero(data, realsz);
	}
	~BoolArray(void) { delete[] data; }
	inline bool operator[] (int n) const {
		assert(n >= 0);
		return ((data[n/8] >> (n%8)) & 1) != 0;
	}
	inline void set(int n, bool d) const {
		assert(n >= 0);
		if (d)
			data[n/8] |= 1 << (n%8);
		else
			data[n/8] &= ~(1 << (n%8));
	}
	int intersect_count(const BoolArray &other, int size) const {
		int count = 0;
		int i = (size + 7 ) / 8;
		for (i--; i>=0; i--) {
			unsigned char n = data[i] & other.data[i];
			while (n) {
				if (n & 1) count++;
				n >>= 1;
			}
		}
		return count;
	}
	bool empty(int size) const {
		int i = (size + 7) / 8;
		for (i--; i>=0; i--)
			if (data[i]) return false;
		return true;
	}
	void print(int size) const {
		int realsz = (size + 7) / 8;
		for (int i=0; i<realsz; i++)
			printf(" %02x", data[i]);
		putchar('\n');
	}
private:
	unsigned char *data;
	BoolArray(const BoolArray &other) {}
	BoolArray &operator= (const BoolArray &other) {return *this;}
};

#endif
