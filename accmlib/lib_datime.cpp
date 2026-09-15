// ASCII C99 TAB4 CRLF
// Attribute: ArnCovenant
// LastCheck: 2026 Sep 15
// AllAuthor: @dosconio
// ModuTitle: Date and Time Standard C Library for MECOCOA
/*
	Copyright 2023 ArinaMgk

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

	http://www.apache.org/licenses/LICENSE-2.0
	http://unisym.org/license.html

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
*/

#include "aaaaa.h"
#include <time.h>
#include "c/ustring.h"

static struct tm g_tm_buf;
static char g_asctime_buf[26];

static const char* const g_wday_abbr[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char* const g_mon_abbr[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char* const g_wday_full[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char* const g_mon_full[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

static inline bool is_leap_year(int year)
{
	return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

static inline int days_in_month(int year, int mon)
{
	static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (mon == 1 && is_leap_year(year)) return 29;
	if (mon >= 0 && mon < 12) return mdays[mon];
	return 30;
}

static inline int days_before_month(int year, int mon)
{
	static const int cum_days[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	if (mon < 0) mon = 0;
	if (mon > 11) mon = 11;
	int d = cum_days[mon];
	if (mon > 1 && is_leap_year(year)) d += 1;
	return d;
}

static void put_2d(char* dst, int val)
{
	dst[0] = '0' + (char)((val / 10) % 10);
	dst[1] = '0' + (char)(val % 10);
}

static void put_4d(char* dst, int val)
{
	dst[0] = '0' + (char)((val / 1000) % 10);
	dst[1] = '0' + (char)((val / 100) % 10);
	dst[2] = '0' + (char)((val / 10) % 10);
	dst[3] = '0' + (char)(val % 10);
}

static struct tm* time_to_tm(const time_t* timer, struct tm* out)
{
	if (!timer || !out) return nullptr;

	time_t total_sec = *timer;
	sint64 days = total_sec / 86400;
	sint64 rem_sec = total_sec % 86400;
	if (rem_sec < 0) {
		rem_sec += 86400;
		days -= 1;
	}

	int hour = (int)(rem_sec / 3600);
	rem_sec %= 3600;
	int min = (int)(rem_sec / 60);
	int sec = (int)(rem_sec % 60);

	int wday = (int)((days + 4) % 7);
	if (wday < 0) wday += 7;

	int year = 1970;
	if (days >= 0) {
		while (days >= (is_leap_year(year) ? 366 : 365)) {
			days -= (is_leap_year(year) ? 366 : 365);
			year++;
		}
	} else {
		while (days < 0) {
			year--;
			days += (is_leap_year(year) ? 366 : 365);
		}
	}

	int yday = (int)days;
	int mon = 0;
	while (mon < 11 && days >= days_in_month(year, mon)) {
		days -= days_in_month(year, mon);
		mon++;
	}
	int mday = (int)days + 1;

	out->tm_sec = sec;
	out->tm_min = min;
	out->tm_hour = hour;
	out->tm_mday = mday;
	out->tm_mon = mon;
	out->tm_year = year - 1900;
	out->tm_wday = wday;
	out->tm_yday = yday;
	out->tm_isdst = 0;

	return out;
}

extern "C" clock_t clock(void)
{
	return (clock_t)syscall(syscall_t::TIME, 1, nil, nil);
}

extern "C" double difftime(time_t time1, time_t time0)
{
	return (double)(time1 - time0);
}

extern "C" time_t mktime(struct tm* timeptr)
{
	if (!timeptr) return (time_t)(-1);

	int year = timeptr->tm_year + 1900;
	int mon = timeptr->tm_mon;
	int mday = timeptr->tm_mday;
	int hour = timeptr->tm_hour;
	int min = timeptr->tm_min;
	int sec = timeptr->tm_sec;

	// Normalize seconds
	if (sec < 0 || sec >= 60) {
		int extra_min = sec / 60;
		sec %= 60;
		if (sec < 0) { sec += 60; extra_min -= 1; }
		min += extra_min;
	}
	// Normalize minutes
	if (min < 0 || min >= 60) {
		int extra_hour = min / 60;
		min %= 60;
		if (min < 0) { min += 60; extra_hour -= 1; }
		hour += extra_hour;
	}
	// Normalize hours
	int extra_days = 0;
	if (hour < 0 || hour >= 24) {
		extra_days = hour / 24;
		hour %= 24;
		if (hour < 0) { hour += 24; extra_days -= 1; }
	}
	// Normalize months
	if (mon < 0 || mon >= 12) {
		int extra_years = mon / 12;
		mon %= 12;
		if (mon < 0) { mon += 12; extra_years -= 1; }
		year += extra_years;
	}

	// Calculate days since 1970-01-01
	sint64 days = 0;
	if (year >= 1970) {
		for (int y = 1970; y < year; ++y) {
			days += is_leap_year(y) ? 366 : 365;
		}
	} else {
		for (int y = year; y < 1970; ++y) {
			days -= is_leap_year(y) ? 366 : 365;
		}
	}

	int yday = days_before_month(year, mon) + (mday - 1);
	days += yday + extra_days;

	// Recalculate year/mon/mday if extra_days shifted across month/year boundaries
	if (extra_days != 0) {
		sint64 d_tmp = days;
		int cur_y = 1970;
		if (d_tmp >= 0) {
			while (d_tmp >= (is_leap_year(cur_y) ? 366 : 365)) {
				d_tmp -= (is_leap_year(cur_y) ? 366 : 365);
				cur_y++;
			}
		} else {
			while (d_tmp < 0) {
				cur_y--;
				d_tmp += (is_leap_year(cur_y) ? 366 : 365);
			}
		}
		yday = (int)d_tmp;
		year = cur_y;
		mon = 0;
		while (mon < 11 && d_tmp >= days_in_month(year, mon)) {
			d_tmp -= days_in_month(year, mon);
			mon++;
		}
		mday = (int)d_tmp + 1;
	}

	int wday = (int)((days + 4) % 7);
	if (wday < 0) wday += 7;

	timeptr->tm_sec = sec;
	timeptr->tm_min = min;
	timeptr->tm_hour = hour;
	timeptr->tm_mday = mday;
	timeptr->tm_mon = mon;
	timeptr->tm_year = year - 1900;
	timeptr->tm_wday = wday;
	timeptr->tm_yday = yday;

	sint64 total_sec = days * 86400 + (sint64)hour * 3600 + (sint64)min * 60 + sec;
	return (time_t)total_sec;
}

extern "C" time_t time(time_t* timer)
{
	uint32 d = sysdate();
	uint32 t = systime();

	int year = (int)(((d >> 28) & 0xF) * 1000 + ((d >> 24) & 0xF) * 100 + ((d >> 20) & 0xF) * 10 + ((d >> 16) & 0xF));
	int month = (int)(((d >> 12) & 0xF) * 10 + ((d >> 8) & 0xF));
	int day = (int)(((d >> 4) & 0xF) * 10 + (d & 0xF));

	int hour = (int)(((t >> 20) & 0xF) * 10 + ((t >> 16) & 0xF));
	int min = (int)(((t >> 12) & 0xF) * 10 + ((t >> 8) & 0xF));
	int sec = (int)(((t >> 4) & 0xF) * 10 + (t & 0xF));

	struct tm tm_curr;
	tm_curr.tm_sec = sec;
	tm_curr.tm_min = min;
	tm_curr.tm_hour = hour;
	tm_curr.tm_mday = (day >= 1 && day <= 31) ? day : 1;
	tm_curr.tm_mon = (month >= 1 && month <= 12) ? (month - 1) : 0;
	tm_curr.tm_year = (year >= 1900) ? (year - 1900) : 70;
	tm_curr.tm_isdst = 0;

	time_t result = mktime(&tm_curr);
	if (timer) {
		*timer = result;
	}
	return result;
}

extern "C" struct tm* gmtime(const time_t* timer)
{
	return time_to_tm(timer, &g_tm_buf);
}

extern "C" struct tm* localtime(const time_t* timer)
{
	return time_to_tm(timer, &g_tm_buf);
}

extern "C" char* asctime(const struct tm* timeptr)
{
	if (!timeptr) return nullptr;

	int wday = timeptr->tm_wday;
	if (wday < 0 || wday > 6) wday = 0;
	int mon = timeptr->tm_mon;
	if (mon < 0 || mon > 11) mon = 0;

	const char* wstr = g_wday_abbr[wday];
	const char* mstr = g_mon_abbr[mon];

	g_asctime_buf[0] = wstr[0];
	g_asctime_buf[1] = wstr[1];
	g_asctime_buf[2] = wstr[2];
	g_asctime_buf[3] = ' ';
	g_asctime_buf[4] = mstr[0];
	g_asctime_buf[5] = mstr[1];
	g_asctime_buf[6] = mstr[2];
	g_asctime_buf[7] = ' ';

	int mday = timeptr->tm_mday;
	if (mday < 10) {
		g_asctime_buf[8] = ' ';
		g_asctime_buf[9] = '0' + (char)mday;
	} else {
		put_2d(&g_asctime_buf[8], mday);
	}
	g_asctime_buf[10] = ' ';

	put_2d(&g_asctime_buf[11], timeptr->tm_hour);
	g_asctime_buf[13] = ':';
	put_2d(&g_asctime_buf[14], timeptr->tm_min);
	g_asctime_buf[16] = ':';
	put_2d(&g_asctime_buf[17], timeptr->tm_sec);
	g_asctime_buf[19] = ' ';

	put_4d(&g_asctime_buf[20], timeptr->tm_year + 1900);
	g_asctime_buf[24] = '\n';
	g_asctime_buf[25] = '\0';

	return g_asctime_buf;
}

extern "C" char* ctime(const time_t* timer)
{
	return asctime(localtime(timer));
}

extern "C" size_t strftime(char* s, size_t maxsize, const char* format, const struct tm* timeptr)
{
	if (!s || maxsize == 0 || !format || !timeptr) return 0;

	size_t written = 0;
	auto append_char = [&](char c) -> bool {
		if (written + 1 >= maxsize) return false;
		s[written++] = c;
		return true;
	};
	auto append_str = [&](const char* str) -> bool {
		while (*str) {
			if (!append_char(*str++)) return false;
		}
		return true;
	};
	auto append_2d = [&](int val) -> bool {
		if (val < 0) val = 0;
		char buf[3];
		buf[0] = '0' + (char)((val / 10) % 10);
		buf[1] = '0' + (char)(val % 10);
		buf[2] = '\0';
		return append_str(buf);
	};
	auto append_num = [&](int val, int width) -> bool {
		if (val < 0) val = 0;
		char buf[16];
		int len = 0;
		int tmp = val;
		do {
			len++;
			tmp /= 10;
		} while (tmp > 0);
		int pad = (width > len) ? (width - len) : 0;
		for (int i = 0; i < pad; ++i) {
			if (!append_char('0')) return false;
		}
		int pos = len;
		buf[pos] = '\0';
		tmp = val;
		do {
			buf[--pos] = '0' + (char)(tmp % 10);
			tmp /= 10;
		} while (pos > 0);
		return append_str(buf);
	};

	int wday = (timeptr->tm_wday >= 0 && timeptr->tm_wday <= 6) ? timeptr->tm_wday : 0;
	int mon = (timeptr->tm_mon >= 0 && timeptr->tm_mon <= 11) ? timeptr->tm_mon : 0;
	int year = timeptr->tm_year + 1900;

	for (const char* p = format; *p; ++p) {
		if (*p != '%') {
			if (!append_char(*p)) { s[0] = '\0'; return 0; }
			continue;
		}
		++p;
		if (*p == '\0') break;
		if (*p == 'E' || *p == 'O') {
			++p;
			if (*p == '\0') break;
		}
		switch (*p) {
		case 'a':
			if (!append_str(g_wday_abbr[wday])) { s[0] = '\0'; return 0; }
			break;
		case 'A':
			if (!append_str(g_wday_full[wday])) { s[0] = '\0'; return 0; }
			break;
		case 'b':
		case 'h':
			if (!append_str(g_mon_abbr[mon])) { s[0] = '\0'; return 0; }
			break;
		case 'B':
			if (!append_str(g_mon_full[mon])) { s[0] = '\0'; return 0; }
			break;
		case 'c':
			if (!strftime(s + written, maxsize - written, "%a %b %e %H:%M:%S %Y", timeptr)) { s[0] = '\0'; return 0; }
			written = StrLength(s);
			break;
		case 'C':
			if (!append_2d(year / 100)) { s[0] = '\0'; return 0; }
			break;
		case 'd':
			if (!append_2d(timeptr->tm_mday)) { s[0] = '\0'; return 0; }
			break;
		case 'D':
			if (!append_2d(mon + 1) || !append_char('/') || !append_2d(timeptr->tm_mday) || !append_char('/') || !append_2d(year % 100)) { s[0] = '\0'; return 0; }
			break;
		case 'e':
			if (timeptr->tm_mday < 10) {
				if (!append_char(' ') || !append_char('0' + (char)timeptr->tm_mday)) { s[0] = '\0'; return 0; }
			} else {
				if (!append_2d(timeptr->tm_mday)) { s[0] = '\0'; return 0; }
			}
			break;
		case 'F':
			if (!append_num(year, 4) || !append_char('-') || !append_2d(mon + 1) || !append_char('-') || !append_2d(timeptr->tm_mday)) { s[0] = '\0'; return 0; }
			break;
		case 'H':
			if (!append_2d(timeptr->tm_hour)) { s[0] = '\0'; return 0; }
			break;
		case 'I': {
			int h12 = timeptr->tm_hour % 12;
			if (h12 == 0) h12 = 12;
			if (!append_2d(h12)) { s[0] = '\0'; return 0; }
			break;
		}
		case 'j':
			if (!append_num(timeptr->tm_yday + 1, 3)) { s[0] = '\0'; return 0; }
			break;
		case 'm':
			if (!append_2d(mon + 1)) { s[0] = '\0'; return 0; }
			break;
		case 'M':
			if (!append_2d(timeptr->tm_min)) { s[0] = '\0'; return 0; }
			break;
		case 'n':
			if (!append_char('\n')) { s[0] = '\0'; return 0; }
			break;
		case 'p':
			if (!append_str(timeptr->tm_hour >= 12 ? "PM" : "AM")) { s[0] = '\0'; return 0; }
			break;
		case 'r': {
			int h12 = timeptr->tm_hour % 12;
			if (h12 == 0) h12 = 12;
			if (!append_2d(h12) || !append_char(':') || !append_2d(timeptr->tm_min) || !append_char(':') || !append_2d(timeptr->tm_sec) || !append_char(' ') || !append_str(timeptr->tm_hour >= 12 ? "PM" : "AM")) { s[0] = '\0'; return 0; }
			break;
		}
		case 'R':
			if (!append_2d(timeptr->tm_hour) || !append_char(':') || !append_2d(timeptr->tm_min)) { s[0] = '\0'; return 0; }
			break;
		case 'S':
			if (!append_2d(timeptr->tm_sec)) { s[0] = '\0'; return 0; }
			break;
		case 't':
			if (!append_char('\t')) { s[0] = '\0'; return 0; }
			break;
		case 'T':
			if (!append_2d(timeptr->tm_hour) || !append_char(':') || !append_2d(timeptr->tm_min) || !append_char(':') || !append_2d(timeptr->tm_sec)) { s[0] = '\0'; return 0; }
			break;
		case 'u': {
			int u = (wday == 0) ? 7 : wday;
			if (!append_char('0' + (char)u)) { s[0] = '\0'; return 0; }
			break;
		}
		case 'U': {
			int u_week = (timeptr->tm_yday - wday + 7) / 7;
			if (!append_2d(u_week)) { s[0] = '\0'; return 0; }
			break;
		}
		case 'w':
			if (!append_char('0' + (char)wday)) { s[0] = '\0'; return 0; }
			break;
		case 'W': {
			int monday_wday = (wday == 0) ? 6 : (wday - 1);
			int w_week = (timeptr->tm_yday - monday_wday + 7) / 7;
			if (!append_2d(w_week)) { s[0] = '\0'; return 0; }
			break;
		}
		case 'x':
			if (!strftime(s + written, maxsize - written, "%m/%d/%y", timeptr)) { s[0] = '\0'; return 0; }
			written = StrLength(s);
			break;
		case 'X':
			if (!strftime(s + written, maxsize - written, "%H:%M:%S", timeptr)) { s[0] = '\0'; return 0; }
			written = StrLength(s);
			break;
		case 'y':
			if (!append_2d(year % 100)) { s[0] = '\0'; return 0; }
			break;
		case 'Y':
			if (!append_num(year, 4)) { s[0] = '\0'; return 0; }
			break;
		case '%':
			if (!append_char('%')) { s[0] = '\0'; return 0; }
			break;
		default:
			if (!append_char(*p)) { s[0] = '\0'; return 0; }
			break;
		}
	}

	s[written] = '\0';
	return written;
}
