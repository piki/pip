#ifndef COMMON_H
#define COMMON_H

#include <sys/time.h>

inline long operator-(const timeval &a, const timeval &b) {
	return 1000000*(a.tv_sec - b.tv_sec) + (a.tv_usec - b.tv_usec);
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
