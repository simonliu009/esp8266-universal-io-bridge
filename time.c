#include "time.h"

#include "config.h"
#include "stats.h"

#include <user_interface.h>
#include <sntp.h>

// system

static unsigned int system_last_us;
static unsigned int system_base_us;
static unsigned int system_wraps;

irom static void system_init(void)
{
	system_last_us = 0;
	system_wraps = 0;

	system_base_us = system_get_time();
}

irom static void system_periodic(void)
{
	unsigned int system_now = system_get_time();

	if(system_now < system_last_us)
		system_wraps++;

	system_last_us = system_now;
}

irom void time_system_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	uint64_t system_us;

	system_us = (((uint64_t)system_get_time()) | ((uint64_t)system_wraps << 32)) - system_base_us;

	if(s)
		*s = system_us / 1000000;

	if(ms)
		*ms = (system_us % 1000000) / 1000;

	if(raw1)
		*raw1 = system_us >> 32;

	if(raw2)
		*raw2 = system_us & 0xffffffff;

	if(base)
		*base = system_base_us;

	if(wraps)
		*wraps = 0;
}

// rtc

static uint64_t rtc_current_ns;
static unsigned int rtc_base_s;
static unsigned int rtc_last_value;
static unsigned int rtc_wraps;

irom static void rtc_init(void)
{
	unsigned int rtc_current_value;
	uint64_t calvalue_ns;

	rtc_current_value = system_get_rtc_time();
	rtc_last_value = rtc_current_value;

	calvalue_ns = ((uint64_t)system_rtc_clock_cali_proc() * 1000) >> 12;
	rtc_base_s = (rtc_current_value * calvalue_ns) / 1000000000;

	rtc_current_ns = 0;
	rtc_wraps = 0;
}

irom static void rtc_periodic(void)
{
	unsigned int rtc_current_value;
	uint64_t calvalue_ns, diff;

	rtc_current_value = system_get_rtc_time();

	if(rtc_current_value >= rtc_last_value)
		diff = (uint64_t)rtc_current_value - (uint64_t)rtc_last_value;
	else
	{
		rtc_wraps++;
		diff = ((uint64_t)1 << 32) - ((uint64_t)rtc_last_value - (uint64_t)rtc_current_value);
	}

	calvalue_ns = ((uint64_t)system_rtc_clock_cali_proc() * 1000) >> 12;
	diff *= calvalue_ns;

	rtc_current_ns += diff;

	rtc_last_value = rtc_current_value;
}

irom void time_rtc_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	uint64_t current;

	current = rtc_current_ns;

	if(s)
		*s = (current / 1000000000);

	if(ms)
		*ms = (current % 1000000000) / 1000000;

	if(raw1)
		*raw1 = current & 0xffffffff;

	if(raw2)
		*raw2 = current >> 32;

	if(base)
		*base = rtc_base_s;

	if(wraps)
		*wraps = rtc_wraps;
}

// timer

static unsigned int timer_s;
static unsigned int timer_ms;
static unsigned int timer_wraps;

irom static void timer_init(void)
{
	timer_s = 0;
	timer_ms = 0;
	timer_wraps = 0;
}

irom static void timer_periodic(void)
{
	timer_ms += 100;

	if(timer_ms > 999)
	{
		timer_ms = 0;

		if(++timer_s == 0)
			timer_wraps++;
	}
}

irom void time_timer_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	if(s)
		*s = timer_s;

	if(ms)
		*ms = timer_ms;

	if(raw1)
		*raw1 = timer_s;

	if(raw2)
		*raw2 = timer_ms;

	if(base)
		*base = 0;

	if(wraps)
		*wraps = timer_wraps;
}

// ntp

static unsigned int ntp_base_s = 0;

irom static void ntp_init(void)
{
	sntp_stop();
	sntp_setserver(0, &config.ntp.server);
	sntp_set_timezone(config.ntp.timezone);
	sntp_init();
}

irom static void ntp_periodic(void)
{
	static int delay = 0;
	static bool_t initial_burst = true;
	time_t ntp_s;

	if(!ip_addr_valid(config.ntp.server))
		return;

	delay++;

	if(delay < 10) // always check at most once a second or less frequently
		return;

	if(!initial_burst && (delay < 6000)) // after initial burst only check every 10 minutes
		return;

	delay = 0;

	if((ntp_s = sntp_get_current_timestamp()) > 0)
	{
		initial_burst = false;
		stat_update_ntp++;
		ntp_init(); // FIXME SDK bug, stop and start ntp to get continuous updating
	}

	if(ntp_base_s == 0)
		ntp_base_s = ntp_s;
}

irom void time_ntp_get(unsigned int *s, unsigned int *ms,
		unsigned int *raw1, unsigned int *raw2,
		unsigned int *base, unsigned int *wraps)
{
	time_t current = sntp_get_current_timestamp();

	if(s)
	{
		if(current > 0)
			*s = current - ntp_base_s;
		else
			*s = 0;
	}

	if(ms)
		*ms = 0;

	if(raw1)
		*raw1 = current;

	if(raw2)
		*raw2 = 0;

	if(base)
		*base = ntp_base_s;

	if(wraps)
		*wraps = 0;
}

// generic interface

static unsigned int time_base_s;

irom void time_init(void)
{
	time_base_s = 0;

	system_init();
	rtc_init();
	timer_init();
	ntp_init();
}

irom void time_periodic(void)
{
	system_periodic();
	rtc_periodic();
	timer_periodic();
	ntp_periodic();
}

irom void time_set(unsigned int base)
{
	time_base_s = base;
	system_init();
	rtc_init();
	timer_init();
}

irom void time_set_hms(unsigned int h, unsigned int m, unsigned int s)
{
	time_set((h * 3600) + (m * 60) + s);
}

irom const char *time_get(unsigned int *h, unsigned int *m, unsigned int *s,
			unsigned int *Y, unsigned int *M, unsigned int *D)
{
	unsigned int time_s = 0;
	time_t ticks_s;
	const char *source;
	struct tm *tm;

	if(ntp_base_s > 0) // we have ntp sync
	{
		source = "ntp";
		time_ntp_get(0, 0, &time_s, 0, 0, 0);
	}
	else
	{
		source = "rtc";
		time_rtc_get(&time_s, 0, 0, 0, 0, 0);
		time_s = (unsigned int)(time_s + time_base_s);
	}

	ticks_s  = time_s;
	tm = sntp_localtime(&ticks_s);

	if(Y)
		*Y = tm->tm_year + 1900;

	if(M)
		*M  = tm->tm_mon + 1;

	if(D)
		*D = tm->tm_mday;

	if(h)
		*h = tm->tm_hour;

	if(m)
		*m = tm->tm_min;

	if(s)
		*s = tm->tm_sec;

	return(source);
}
