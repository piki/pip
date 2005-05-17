#ifndef COMMON_H
#define COMMON_H

#include <sys/time.h>

inline long operator-(const timeval &a, const timeval &b) {
	return 1000000*(a.tv_sec - b.tv_sec) + (a.tv_usec - b.tv_usec);
}

inline timeval operator+(const timeval &a, int ms) {
	timeval ret;
	ret.tv_usec = a.tv_usec + (ms % 1000000);
	ret.tv_sec = a.tv_sec + (ms / 1000000);
	while (ret.tv_usec >= 1000000) {
		ret.tv_usec -= 1000000;
		ret.tv_sec++;
	}
	return ret;
}

inline bool operator<(const timeval &a, const timeval &b) {
	return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_usec < b.tv_usec);
}

inline bool operator>(const timeval &a, const timeval &b) {
	return a.tv_sec > b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_usec > b.tv_usec);
}

inline bool operator<=(const timeval &a, const timeval &b) {
	return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_usec <= b.tv_usec);
}

inline bool operator>=(const timeval &a, const timeval &b) {
	return a.tv_sec > b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_usec >= b.tv_usec);
}

inline bool operator==(const timeval &a, const timeval &b) {
	return a.tv_sec == b.tv_sec && a.tv_usec == b.tv_usec;
}

#endif
