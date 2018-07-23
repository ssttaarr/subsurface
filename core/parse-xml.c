// SPDX-License-Identifier: GPL-2.0
#ifdef __clang__
// Clang has a bug on zero-initialization of C structs.
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "ssrf.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#define __USE_XOPEN
#include <time.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/tree.h>
#include <libxslt/transform.h>
#include <libdivecomputer/parser.h>

#include "gettext.h"

#include "dive.h"
#include "subsurface-string.h"
#include "parse.h"
#include "divelist.h"
#include "device.h"
#include "membuffer.h"
#include "qthelper.h"

int verbose, quit, force_root;
int last_xml_version = -1;

static xmlDoc *test_xslt_transforms(xmlDoc *doc, const char **params);

struct units xml_parsing_units;
const struct units SI_units = SI_UNITS;
const struct units IMPERIAL_units = IMPERIAL_UNITS;

static void divedate(const char *buffer, timestamp_t *when)
{
	int d, m, y;
	int hh, mm, ss;

	hh = 0;
	mm = 0;
	ss = 0;
	if (sscanf(buffer, "%d.%d.%d %d:%d:%d", &d, &m, &y, &hh, &mm, &ss) >= 3) {
		/* This is ok, and we got at least the date */
	} else if (sscanf(buffer, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hh, &mm, &ss) >= 3) {
		/* This is also ok */
	} else {
		fprintf(stderr, "Unable to parse date '%s'\n", buffer);
		return;
	}
	cur_tm.tm_year = y;
	cur_tm.tm_mon = m - 1;
	cur_tm.tm_mday = d;
	cur_tm.tm_hour = hh;
	cur_tm.tm_min = mm;
	cur_tm.tm_sec = ss;

	*when = utc_mktime(&cur_tm);
}

static void divetime(const char *buffer, timestamp_t *when)
{
	int h, m, s = 0;

	if (sscanf(buffer, "%d:%d:%d", &h, &m, &s) >= 2) {
		cur_tm.tm_hour = h;
		cur_tm.tm_min = m;
		cur_tm.tm_sec = s;
		*when = utc_mktime(&cur_tm);
	}
}

/* Libdivecomputer: "2011-03-20 10:22:38" */
static void divedatetime(char *buffer, timestamp_t *when)
{
	int y, m, d;
	int hr, min, sec;

	if (sscanf(buffer, "%d-%d-%d %d:%d:%d",
		   &y, &m, &d, &hr, &min, &sec) == 6) {
		cur_tm.tm_year = y;
		cur_tm.tm_mon = m - 1;
		cur_tm.tm_mday = d;
		cur_tm.tm_hour = hr;
		cur_tm.tm_min = min;
		cur_tm.tm_sec = sec;
		*when = utc_mktime(&cur_tm);
	}
}

enum ParseState {
	FINDSTART,
	FINDEND
};
static void divetags(char *buffer, struct tag_entry **tags)
{
	int i = 0, start = 0, end = 0;
	enum ParseState state = FINDEND;
	int len = buffer ? strlen(buffer) : 0;

	while (i < len) {
		if (buffer[i] == ',') {
			if (state == FINDSTART) {
				/* Detect empty tags */
			} else if (state == FINDEND) {
				/* Found end of tag */
				if (i > 0 && buffer[i - 1] != '\\') {
					buffer[i] = '\0';
					state = FINDSTART;
					taglist_add_tag(tags, buffer + start);
				} else {
					state = FINDSTART;
				}
			}
		} else if (buffer[i] == ' ') {
			/* Handled */
		} else {
			/* Found start of tag */
			if (state == FINDSTART) {
				state = FINDEND;
				start = i;
			} else if (state == FINDEND) {
				end = i;
			}
		}
		i++;
	}
	if (state == FINDEND) {
		if (end < start)
			end = len - 1;
		if (len > 0) {
			buffer[end + 1] = '\0';
			taglist_add_tag(tags, buffer + start);
		}
	}
}

enum number_type {
	NEITHER,
	FLOAT
};

static enum number_type parse_float(const char *buffer, double *res, const char **endp)
{
	double val;
	static bool first_time = true;

	errno = 0;
	val = ascii_strtod(buffer, endp);
	if (errno || *endp == buffer)
		return NEITHER;
	if (**endp == ',') {
		if (IS_FP_SAME(val, rint(val))) {
			/* we really want to send an error if this is a Subsurface native file
			 * as this is likely indication of a bug - but right now we don't have
			 * that information available */
			if (first_time) {
				fprintf(stderr, "Floating point value with decimal comma (%s)?\n", buffer);
				first_time = false;
			}
			/* Try again in permissive mode*/
			val = strtod_flags(buffer, endp, 0);
		}
	}

	*res = val;
	return FLOAT;
}

union int_or_float {
	double fp;
};

static enum number_type integer_or_float(char *buffer, union int_or_float *res)
{
	const char *end;
	return parse_float(buffer, &res->fp, &end);
}

static void pressure(char *buffer, pressure_t *pressure)
{
	double mbar = 0.0;
	union int_or_float val;

	switch (integer_or_float(buffer, &val)) {
	case FLOAT:
		/* Just ignore zero values */
		if (!val.fp)
			break;
		switch (xml_parsing_units.pressure) {
		case PASCAL:
			mbar = val.fp / 100;
			break;
		case BAR:
			/* Assume mbar, but if it's really small, it's bar */
			mbar = val.fp;
			if (fabs(mbar) < 5000)
				mbar = mbar * 1000;
			break;
		case PSI:
			mbar = psi_to_mbar(val.fp);
			break;
		}
		if (fabs(mbar) > 5 && fabs(mbar) < 5000000) {
			pressure->mbar = lrint(mbar);
			break;
		}
	/* fallthrough */
	default:
		printf("Strange pressure reading %s\n", buffer);
	}
}

static void cylinder_use(char *buffer, enum cylinderuse *cyl_use)
{
	if (trimspace(buffer)) {
		int use = cylinderuse_from_text(buffer);
		*cyl_use = use;
		if (use == OXYGEN)
			o2pressure_sensor = cur_cylinder_index;
	}
}

static void salinity(char *buffer, int *salinity)
{
	union int_or_float val;
	switch (integer_or_float(buffer, &val)) {
	case FLOAT:
		*salinity = lrint(val.fp * 10.0);
		break;
	default:
		printf("Strange salinity reading %s\n", buffer);
	}
}

static void depth(char *buffer, depth_t *depth)
{
	union int_or_float val;

	switch (integer_or_float(buffer, &val)) {
	case FLOAT:
		switch (xml_parsing_units.length) {
		case METERS:
			depth->mm = lrint(val.fp * 1000);
			break;
		case FEET:
			depth->mm = feet_to_mm(val.fp);
			break;
		}
		break;
	default:
		printf("Strange depth reading %s\n", buffer);
	}
}

static void extra_data_start(void)
{
	memset(&cur_extra_data, 0, sizeof(struct extra_data));
}

static void extra_data_end(void)
{
	// don't save partial structures - we must have both key and value
	if (cur_extra_data.key && cur_extra_data.value)
		add_extra_data(get_dc(), cur_extra_data.key, cur_extra_data.value);
}

static void weight(char *buffer, weight_t *weight)
{
	union int_or_float val;

	switch (integer_or_float(buffer, &val)) {
	case FLOAT:
		switch (xml_parsing_units.weight) {
		case KG:
			weight->grams = lrint(val.fp * 1000);
			break;
		case LBS:
			weight->grams = lbs_to_grams(val.fp);
			break;
		}
		break;
	default:
		printf("Strange weight reading %s\n", buffer);
	}
}

static void temperature(char *buffer, temperature_t *temperature)
{
	union int_or_float val;

	switch (integer_or_float(buffer, &val)) {
	case FLOAT:
		switch (xml_parsing_units.temperature) {
		case KELVIN:
			temperature->mkelvin = lrint(val.fp * 1000);
			break;
		case CELSIUS:
			temperature->mkelvin = C_to_mkelvin(val.fp);
			break;
		case FAHRENHEIT:
			temperature->mkelvin = F_to_mkelvin(val.fp);
			break;
		}
		break;
	default:
		printf("Strange temperature reading %s\n", buffer);
	}
	/* temperatures outside -40C .. +70C should be ignored */
	if (temperature->mkelvin < ZERO_C_IN_MKELVIN - 40000 ||
	    temperature->mkelvin > ZERO_C_IN_MKELVIN + 70000)
		temperature->mkelvin = 0;
}

static void sampletime(char *buffer, duration_t *time)
{
	int i;
	int min, sec;

	i = sscanf(buffer, "%d:%d", &min, &sec);
	switch (i) {
	case 1:
		sec = min;
		min = 0;
	/* fallthrough */
	case 2:
		time->seconds = sec + min * 60;
		break;
	default:
		printf("Strange sample time reading %s\n", buffer);
	}
}

static void offsettime(char *buffer, offset_t *time)
{
	duration_t uoffset;
	int sign = 1;
	if (*buffer == '-') {
		sign = -1;
		buffer++;
	}
	/* yes, this could indeed fail if we have an offset > 34yrs
	 * - too bad */
	sampletime(buffer, &uoffset);
	time->seconds = sign * uoffset.seconds;
}

static void duration(char *buffer, duration_t *time)
{
	/* DivingLog 5.08 (and maybe other versions) appear to sometimes
	 * store the dive time as 44.00 instead of 44:00;
	 * This attempts to parse this in a fairly robust way */
	if (!strchr(buffer, ':') && strchr(buffer, '.')) {
		char *mybuffer = strdup(buffer);
		char *dot = strchr(mybuffer, '.');
		*dot = ':';
		sampletime(mybuffer, time);
		free(mybuffer);
	} else {
		sampletime(buffer, time);
	}
}

static void percent(char *buffer, fraction_t *fraction)
{
	double val;
	const char *end;

	switch (parse_float(buffer, &val, &end)) {
	case FLOAT:
		/* Turn fractions into percent unless explicit.. */
		if (val <= 1.0) {
			while (isspace(*end))
				end++;
			if (*end != '%')
				val *= 100;
		}

		/* Then turn percent into our integer permille format */
		if (val >= 0 && val <= 100.0) {
			fraction->permille = lrint(val * 10);
			break;
		}
	default:
		printf(translate("gettextFromC", "Strange percentage reading %s\n"), buffer);
		break;
	}
}

static void gasmix(char *buffer, fraction_t *fraction)
{
	/* libdivecomputer does negative percentages. */
	if (*buffer == '-')
		return;
	if (cur_cylinder_index < MAX_CYLINDERS)
		percent(buffer, fraction);
}

static void gasmix_nitrogen(char *buffer, struct gasmix *gasmix)
{
	UNUSED(buffer);
	UNUSED(gasmix);
	/* Ignore n2 percentages. There's no value in them. */
}

static void cylindersize(char *buffer, volume_t *volume)
{
	union int_or_float val;

	switch (integer_or_float(buffer, &val)) {
	case FLOAT:
		volume->mliter = lrint(val.fp * 1000);
		break;

	default:
		printf("Strange volume reading %s\n", buffer);
		break;
	}
}

static void event_name(char *buffer, char *name)
{
	int size = trimspace(buffer);
	if (size >= MAX_EVENT_NAME)
		size = MAX_EVENT_NAME - 1;
	memcpy(name, buffer, size);
	name[size] = 0;
}

// We don't use gauge as a mode, and pscr doesn't exist as a libdc divemode
const char *libdc_divemode_text[] = { "oc", "cc", "pscr", "freedive", "gauge"};

/* Extract the dive computer type from the xml text buffer */
static void get_dc_type(char *buffer, enum divemode_t *dct)
{
	if (trimspace(buffer)) {
		for (enum divemode_t i = 0; i < NUM_DIVEMODE; i++) {
			if (strcmp(buffer, divemode_text[i]) == 0)
				*dct = i;
			else if (strcmp(buffer, libdc_divemode_text[i]) == 0)
				*dct = i;
		}
	}
}

/* For divemode_text[] (defined in dive.h) determine the index of
 * the string contained in the xml divemode attribute and passed
 * in buffer, below. Typical xml input would be:
 * <event name='modechange' divemode='OC' /> */
static void event_divemode(char *buffer, int *value)
{
	int size = trimspace(buffer);
	if (size >= MAX_EVENT_NAME)
		size = MAX_EVENT_NAME - 1;
	buffer[size] = 0x0;
	for (int i = 0; i < NUM_DIVEMODE; i++) {
		if (!strcmp(buffer,divemode_text[i])) {
			*value = i;
			break;
		}
	}
}

#define MATCH(pattern, fn, dest) ({ 			\
	/* Silly type compatibility test */ 		\
	if (0) (fn)("test", dest);			\
	match(pattern, strlen(pattern), name, (matchfn_t) (fn), buf, dest); })

static void get_index(char *buffer, int *i)
{
	*i = atoi(buffer);
}

static void get_bool(char *buffer, bool *i)
{
	*i = atoi(buffer);
}

static void get_uint8(char *buffer, uint8_t *i)
{
	*i = atoi(buffer);
}

static void get_uint16(char *buffer, uint16_t *i)
{
	*i = atoi(buffer);
}

static void get_bearing(char *buffer, bearing_t *bearing)
{
	bearing->degrees = atoi(buffer);
}

static void get_rating(char *buffer, int *i)
{
	int j = atoi(buffer);
	if (j >= 0 && j <= 5) {
		*i = j;
	}
}

static void double_to_o2pressure(char *buffer, o2pressure_t *i)
{
	i->mbar = lrint(ascii_strtod(buffer, NULL) * 1000.0);
}

static void hex_value(char *buffer, uint32_t *i)
{
	*i = strtoul(buffer, NULL, 16);
}

static void get_tripflag(char *buffer, tripflag_t *tf)
{
	*tf = strcmp(buffer, "NOTRIP") ? TF_NONE : NO_TRIP;
}

/*
 * Divinglog is crazy. The temperatures are in celsius. EXCEPT
 * for the sample temperatures, that are in Fahrenheit.
 * WTF?
 *
 * Oh, and I think Diving Log *internally* probably kept them
 * in celsius, because I'm seeing entries like
 *
 *	<Temp>32.0</Temp>
 *
 * in there. Which is freezing, aka 0 degC. I bet the "0" is
 * what Diving Log uses for "no temperature".
 *
 * So throw away crap like that.
 *
 * It gets worse. Sometimes the sample temperatures are in
 * Celsius, which apparently happens if you are in a SI
 * locale. So we now do:
 *
 * - temperatures < 32.0 == Celsius
 * - temperature == 32.0  -> garbage, it's a missing temperature (zero converted from C to F)
 * - temperatures > 32.0 == Fahrenheit
 */
static void fahrenheit(char *buffer, temperature_t *temperature)
{
	union int_or_float val;

	switch (integer_or_float(buffer, &val)) {
	case FLOAT:
		if (IS_FP_SAME(val.fp, 32.0))
			break;
		if (val.fp < 32.0)
			temperature->mkelvin = C_to_mkelvin(val.fp);
		else
			temperature->mkelvin = F_to_mkelvin(val.fp);
		break;
	default:
		fprintf(stderr, "Crazy Diving Log temperature reading %s\n", buffer);
	}
}

/*
 * Did I mention how bat-shit crazy divinglog is? The sample
 * pressures are in PSI. But the tank working pressure is in
 * bar. WTF^2?
 *
 * Crazy stuff like this is why subsurface has everything in
 * these inconvenient typed structures, and you have to say
 * "pressure->mbar" to get the actual value. Exactly so that
 * you can never have unit confusion.
 *
 * It gets worse: sometimes apparently the pressures are in
 * bar, sometimes in psi. Dirk suspects that this may be a
 * DivingLog Uemis importer bug, and that they are always
 * supposed to be in bar, but that the importer got the
 * sample importing wrong.
 *
 * Sadly, there's no way to really tell. So I think we just
 * have to have some arbitrary cut-off point where we assume
 * that smaller values mean bar.. Not good.
 */
static void psi_or_bar(char *buffer, pressure_t *pressure)
{
	union int_or_float val;

	switch (integer_or_float(buffer, &val)) {
	case FLOAT:
		if (val.fp > 400)
			pressure->mbar = psi_to_mbar(val.fp);
		else
			pressure->mbar = lrint(val.fp * 1000);
		break;
	default:
		fprintf(stderr, "Crazy Diving Log PSI reading %s\n", buffer);
	}
}

static int divinglog_fill_sample(struct sample *sample, const char *name, char *buf)
{
	return MATCH("time.p", sampletime, &sample->time) ||
	       MATCH("depth.p", depth, &sample->depth) ||
	       MATCH("temp.p", fahrenheit, &sample->temperature) ||
	       MATCH("press1.p", psi_or_bar, &sample->pressure[0]) ||
	       0;
}

static void uddf_gasswitch(char *buffer, struct sample *sample)
{
	int idx = atoi(buffer);
	int seconds = sample->time.seconds;
	struct dive *dive = cur_dive;
	struct divecomputer *dc = get_dc();

	add_gas_switch_event(dive, dc, seconds, idx);
}

static int uddf_fill_sample(struct sample *sample, const char *name, char *buf)
{
	return MATCH("divetime", sampletime, &sample->time) ||
	       MATCH("depth", depth, &sample->depth) ||
	       MATCH("temperature", temperature, &sample->temperature) ||
	       MATCH("tankpressure", pressure, &sample->pressure[0]) ||
	       MATCH("ref.switchmix", uddf_gasswitch, sample) ||
	       0;
}

static void eventtime(char *buffer, duration_t *duration)
{
	sampletime(buffer, duration);
	if (cur_sample)
		duration->seconds += cur_sample->time.seconds;
}

static void try_to_match_autogroup(const char *name, char *buf)
{
	bool autogroupvalue;

	start_match("autogroup", name, buf);
	if (MATCH("state.autogroup", get_bool, &autogroupvalue)) {
		set_autogroup(autogroupvalue);
		return;
	}
	nonmatch("autogroup", name, buf);
}

void add_gas_switch_event(struct dive *dive, struct divecomputer *dc, int seconds, int idx)
{
	/* sanity check so we don't crash */
	if (idx < 0 || idx >= MAX_CYLINDERS)
		return;
	/* The gas switch event format is insane for historical reasons */
	struct gasmix *mix = &dive->cylinder[idx].gasmix;
	int o2 = get_o2(mix);
	int he = get_he(mix);
	struct event *ev;
	int value;

	o2 = (o2 + 5) / 10;
	he = (he + 5) / 10;
	value = o2 + (he << 16);

	ev = add_event(dc, seconds, he ? SAMPLE_EVENT_GASCHANGE2 : SAMPLE_EVENT_GASCHANGE, 0, value, "gaschange");
	if (ev) {
		ev->gas.index = idx;
		ev->gas.mix = *mix;
	}
}

static void get_cylinderindex(char *buffer, uint8_t *i)
{
	*i = atoi(buffer);
	if (lastcylinderindex != *i) {
		add_gas_switch_event(cur_dive, get_dc(), cur_sample->time.seconds, *i);
		lastcylinderindex = *i;
	}
}

static void get_sensor(char *buffer, uint8_t *i)
{
	*i = atoi(buffer);
}

static void parse_libdc_deco(char *buffer, struct sample *s)
{
	if (strcmp(buffer, "deco") == 0) {
		s->in_deco = true;
	} else if (strcmp(buffer, "ndl") == 0) {
		s->in_deco = false;
		// The time wasn't stoptime, it was ndl
		s->ndl = s->stoptime;
		s->stoptime.seconds = 0;
	}
}

static void try_to_fill_dc_settings(const char *name, char *buf)
{
	start_match("divecomputerid", name, buf);
	if (MATCH("model.divecomputerid", utf8_string, &cur_settings.dc.model))
		return;
	if (MATCH("deviceid.divecomputerid", hex_value, &cur_settings.dc.deviceid))
		return;
	if (MATCH("nickname.divecomputerid", utf8_string, &cur_settings.dc.nickname))
		return;
	if (MATCH("serial.divecomputerid", utf8_string, &cur_settings.dc.serial_nr))
		return;
	if (MATCH("firmware.divecomputerid", utf8_string, &cur_settings.dc.firmware))
		return;

	nonmatch("divecomputerid", name, buf);
}

static void try_to_fill_event(const char *name, char *buf)
{
	start_match("event", name, buf);
	if (MATCH("event", event_name, cur_event.name))
		return;
	if (MATCH("name", event_name, cur_event.name))
		return;
	if (MATCH("time", eventtime, &cur_event.time))
		return;
	if (MATCH("type", get_index, &cur_event.type))
		return;
	if (MATCH("flags", get_index, &cur_event.flags))
		return;
	if (MATCH("value", get_index, &cur_event.value))
		return;
	if (MATCH("divemode", event_divemode, &cur_event.value))
		return;
	if (MATCH("cylinder", get_index, &cur_event.gas.index)) {
		/* We add one to indicate that we got an actual cylinder index value */
		cur_event.gas.index++;
		return;
	}
	if (MATCH("o2", percent, &cur_event.gas.mix.o2))
		return;
	if (MATCH("he", percent, &cur_event.gas.mix.he))
		return;
	nonmatch("event", name, buf);
}

static int match_dc_data_fields(struct divecomputer *dc, const char *name, char *buf)
{
	if (MATCH("maxdepth", depth, &dc->maxdepth))
		return 1;
	if (MATCH("meandepth", depth, &dc->meandepth))
		return 1;
	if (MATCH("max.depth", depth, &dc->maxdepth))
		return 1;
	if (MATCH("mean.depth", depth, &dc->meandepth))
		return 1;
	if (MATCH("duration", duration, &dc->duration))
		return 1;
	if (MATCH("divetime", duration, &dc->duration))
		return 1;
	if (MATCH("divetimesec", duration, &dc->duration))
		return 1;
	if (MATCH("last-manual-time", duration, &dc->last_manual_time))
		return 1;
	if (MATCH("surfacetime", duration, &dc->surfacetime))
		return 1;
	if (MATCH("airtemp", temperature, &dc->airtemp))
		return 1;
	if (MATCH("watertemp", temperature, &dc->watertemp))
		return 1;
	if (MATCH("air.temperature", temperature, &dc->airtemp))
		return 1;
	if (MATCH("water.temperature", temperature, &dc->watertemp))
		return 1;
	if (MATCH("pressure.surface", pressure, &dc->surface_pressure))
		return 1;
	if (MATCH("salinity.water", salinity, &dc->salinity))
		return 1;
	if (MATCH("key.extradata", utf8_string, &cur_extra_data.key))
		return 1;
	if (MATCH("value.extradata", utf8_string, &cur_extra_data.value))
		return 1;
	if (MATCH("divemode", get_dc_type, &dc->divemode))
		return 1;
	if (MATCH("salinity", salinity, &dc->salinity))
		return 1;
	if (MATCH("atmospheric", pressure, &dc->surface_pressure))
		return 1;
	return 0;
}

/* We're in the top-level dive xml. Try to convert whatever value to a dive value */
static void try_to_fill_dc(struct divecomputer *dc, const char *name, char *buf)
{
	unsigned int deviceid;

	start_match("divecomputer", name, buf);

	if (MATCH("date", divedate, &dc->when))
		return;
	if (MATCH("time", divetime, &dc->when))
		return;
	if (MATCH("model", utf8_string, &dc->model))
		return;
	if (MATCH("deviceid", hex_value, &deviceid)) {
		set_dc_deviceid(dc, deviceid);
		return;
	}
	if (MATCH("diveid", hex_value, &dc->diveid))
		return;
	if (MATCH("dctype", get_dc_type, &dc->divemode))
		return;
	if (MATCH("no_o2sensors", get_sensor, &dc->no_o2sensors))
		return;
	if (match_dc_data_fields(dc, name, buf))
		return;

	nonmatch("divecomputer", name, buf);
}

/* We're in samples - try to convert the random xml value to something useful */
static void try_to_fill_sample(struct sample *sample, const char *name, char *buf)
{
	int in_deco;
	pressure_t p;

	start_match("sample", name, buf);
	if (MATCH("pressure.sample", pressure, &sample->pressure[0]))
		return;
	if (MATCH("cylpress.sample", pressure, &sample->pressure[0]))
		return;
	if (MATCH("pdiluent.sample", pressure, &sample->pressure[0]))
		return;
	if (MATCH("o2pressure.sample", pressure, &sample->pressure[1]))
		return;
	/* Christ, this is ugly */
	if (MATCH("pressure0.sample", pressure, &p)) {
		add_sample_pressure(sample, 0, p.mbar);
		return;
	}
	if (MATCH("pressure1.sample", pressure, &p)) {
		add_sample_pressure(sample, 1, p.mbar);
		return;
	}
	if (MATCH("pressure2.sample", pressure, &p)) {
		add_sample_pressure(sample, 2, p.mbar);
		return;
	}
	if (MATCH("pressure3.sample", pressure, &p)) {
		add_sample_pressure(sample, 3, p.mbar);
		return;
	}
	if (MATCH("pressure4.sample", pressure, &p)) {
		add_sample_pressure(sample, 4, p.mbar);
		return;
	}
	if (MATCH("cylinderindex.sample", get_cylinderindex, &sample->sensor[0]))
		return;
	if (MATCH("sensor.sample", get_sensor, &sample->sensor[0]))
		return;
	if (MATCH("depth.sample", depth, &sample->depth))
		return;
	if (MATCH("temp.sample", temperature, &sample->temperature))
		return;
	if (MATCH("temperature.sample", temperature, &sample->temperature))
		return;
	if (MATCH("sampletime.sample", sampletime, &sample->time))
		return;
	if (MATCH("time.sample", sampletime, &sample->time))
		return;
	if (MATCH("ndl.sample", sampletime, &sample->ndl))
		return;
	if (MATCH("tts.sample", sampletime, &sample->tts))
		return;
	if (MATCH("in_deco.sample", get_index, &in_deco)) {
		sample->in_deco = (in_deco == 1);
		return;
	}
	if (MATCH("stoptime.sample", sampletime, &sample->stoptime))
		return;
	if (MATCH("stopdepth.sample", depth, &sample->stopdepth))
		return;
	if (MATCH("cns.sample", get_uint16, &sample->cns))
		return;
	if (MATCH("rbt.sample", sampletime, &sample->rbt))
		return;
	if (MATCH("sensor1.sample", double_to_o2pressure, &sample->o2sensor[0])) // CCR O2 sensor data
		return;
	if (MATCH("sensor2.sample", double_to_o2pressure, &sample->o2sensor[1]))
		return;
	if (MATCH("sensor3.sample", double_to_o2pressure, &sample->o2sensor[2])) // up to 3 CCR sensors
		return;
	if (MATCH("po2.sample", double_to_o2pressure, &sample->setpoint))
		return;
	if (MATCH("heartbeat", get_uint8, &sample->heartbeat))
		return;
	if (MATCH("bearing", get_bearing, &sample->bearing))
		return;
	if (MATCH("setpoint.sample", double_to_o2pressure, &sample->setpoint))
		return;
	if (MATCH("ppo2.sample", double_to_o2pressure, &sample->o2sensor[next_o2_sensor])) {
		next_o2_sensor++;
		return;
	}
	if (MATCH("deco.sample", parse_libdc_deco, sample))
		return;
	if (MATCH("time.deco", sampletime, &sample->stoptime))
		return;
	if (MATCH("depth.deco", depth, &sample->stopdepth))
		return;

	switch (import_source) {
	case DIVINGLOG:
		if (divinglog_fill_sample(sample, name, buf))
			return;
		break;

	case UDDF:
		if (uddf_fill_sample(sample, name, buf))
			return;
		break;

	default:
		break;
	}

	nonmatch("sample", name, buf);
}

static void try_to_fill_userid(const char *name, char *buf)
{
	UNUSED(name);
	if (prefs.save_userid_local)
		set_userid(buf);
}

static const char *country, *city;

static void divinglog_place(char *place, uint32_t *uuid)
{
	char buffer[1024];

	snprintf(buffer, sizeof(buffer),
		 "%s%s%s%s%s",
		 place,
		 city ? ", " : "",
		 city ? city : "",
		 country ? ", " : "",
		 country ? country : "");
	*uuid = get_dive_site_uuid_by_name(buffer, NULL);
	if (*uuid == 0)
		*uuid = create_dive_site(buffer, cur_dive->when);

	// TODO: capture the country / city info in the taxonomy instead
	city = NULL;
	country = NULL;
}

static int divinglog_dive_match(struct dive *dive, const char *name, char *buf)
{
	return MATCH("divedate", divedate, &dive->when) ||
	       MATCH("entrytime", divetime, &dive->when) ||
	       MATCH("divetime", duration, &dive->dc.duration) ||
	       MATCH("depth", depth, &dive->dc.maxdepth) ||
	       MATCH("depthavg", depth, &dive->dc.meandepth) ||
	       MATCH("tanktype", utf8_string, &dive->cylinder[0].type.description) ||
	       MATCH("tanksize", cylindersize, &dive->cylinder[0].type.size) ||
	       MATCH("presw", pressure, &dive->cylinder[0].type.workingpressure) ||
	       MATCH("press", pressure, &dive->cylinder[0].start) ||
	       MATCH("prese", pressure, &dive->cylinder[0].end) ||
	       MATCH("comments", utf8_string, &dive->notes) ||
	       MATCH("names.buddy", utf8_string, &dive->buddy) ||
	       MATCH("name.country", utf8_string, &country) ||
	       MATCH("name.city", utf8_string, &city) ||
	       MATCH("name.place", divinglog_place, &dive->dive_site_uuid) ||
	       0;
}

/*
 * Uddf specifies ISO 8601 time format.
 *
 * There are many variations on that. This handles the useful cases.
 */
static void uddf_datetime(char *buffer, timestamp_t *when)
{
	char c;
	int y, m, d, hh, mm, ss;
	struct tm tm = { 0 };
	int i;

	i = sscanf(buffer, "%d-%d-%d%c%d:%d:%d", &y, &m, &d, &c, &hh, &mm, &ss);
	if (i == 7)
		goto success;
	ss = 0;
	if (i == 6)
		goto success;

	i = sscanf(buffer, "%04d%02d%02d%c%02d%02d%02d", &y, &m, &d, &c, &hh, &mm, &ss);
	if (i == 7)
		goto success;
	ss = 0;
	if (i == 6)
		goto success;
bad_date:
	printf("Bad date time %s\n", buffer);
	return;

success:
	if (c != 'T' && c != ' ')
		goto bad_date;
	tm.tm_year = y;
	tm.tm_mon = m - 1;
	tm.tm_mday = d;
	tm.tm_hour = hh;
	tm.tm_min = mm;
	tm.tm_sec = ss;
	*when = utc_mktime(&tm);
}

#define uddf_datedata(name, offset)                              \
	static void uddf_##name(char *buffer, timestamp_t *when) \
	{                                                        \
		cur_tm.tm_##name = atoi(buffer) + offset;        \
		*when = utc_mktime(&cur_tm);                     \
	}

uddf_datedata(year, 0)
uddf_datedata(mon, -1)
uddf_datedata(mday, 0)
uddf_datedata(hour, 0)
uddf_datedata(min, 0)

static int uddf_dive_match(struct dive *dive, const char *name, char *buf)
{
	return MATCH("datetime", uddf_datetime, &dive->when) ||
	       MATCH("diveduration", duration, &dive->dc.duration) ||
	       MATCH("greatestdepth", depth, &dive->dc.maxdepth) ||
	       MATCH("year.date", uddf_year, &dive->when) ||
	       MATCH("month.date", uddf_mon, &dive->when) ||
	       MATCH("day.date", uddf_mday, &dive->when) ||
	       MATCH("hour.time", uddf_hour, &dive->when) ||
	       MATCH("minute.time", uddf_min, &dive->when) ||
	       0;
}

/*
 * This parses "floating point" into micro-degrees.
 * We don't do exponentials etc, if somebody does
 * GPS locations in that format, they are insane.
 */
degrees_t parse_degrees(char *buf, char **end)
{
	int sign = 1, decimals = 6, value = 0;
	degrees_t ret;

	while (isspace(*buf))
		buf++;
	switch (*buf) {
	case '-':
		sign = -1;
	/* fallthrough */
	case '+':
		buf++;
	}
	while (isdigit(*buf)) {
		value = 10 * value + *buf - '0';
		buf++;
	}

	/* Get the first six decimals if they exist */
	if (*buf == '.')
		buf++;
	do {
		value *= 10;
		if (isdigit(*buf)) {
			value += *buf - '0';
			buf++;
		}
	} while (--decimals);

	/* Rounding */
	switch (*buf) {
	case '5' ... '9':
		value++;
	}
	while (isdigit(*buf))
		buf++;

	*end = buf;
	ret.udeg = value * sign;
	return ret;
}

static void gps_lat(char *buffer, struct dive *dive)
{
	char *end;
	degrees_t latitude = parse_degrees(buffer, &end);
	struct dive_site *ds = get_dive_site_for_dive(dive);
	if (!ds) {
		dive->dive_site_uuid = create_dive_site_with_gps(NULL, latitude, (degrees_t){0}, dive->when);
	} else {
		if (ds->latitude.udeg && ds->latitude.udeg != latitude.udeg)
			fprintf(stderr, "Oops, changing the latitude of existing dive site id %8x name %s; not good\n", ds->uuid, ds->name ?: "(unknown)");
		ds->latitude = latitude;
	}
}

static void gps_long(char *buffer, struct dive *dive)
{
	char *end;
	degrees_t longitude = parse_degrees(buffer, &end);
	struct dive_site *ds = get_dive_site_for_dive(dive);
	if (!ds) {
		dive->dive_site_uuid = create_dive_site_with_gps(NULL, (degrees_t){0}, longitude, dive->when);
	} else {
		if (ds->longitude.udeg && ds->longitude.udeg != longitude.udeg)
			fprintf(stderr, "Oops, changing the longitude of existing dive site id %8x name %s; not good\n", ds->uuid, ds->name ?: "(unknown)");
		ds->longitude = longitude;
	}

}

static void gps_location(char *buffer, struct dive_site *ds)
{
	char *end;

	ds->latitude = parse_degrees(buffer, &end);
	ds->longitude = parse_degrees(end, &end);
}

static void gps_in_dive(char *buffer, struct dive *dive)
{
	char *end;
	struct dive_site *ds = NULL;
	degrees_t latitude = parse_degrees(buffer, &end);
	degrees_t longitude = parse_degrees(end, &end);
	uint32_t uuid = dive->dive_site_uuid;
	if (uuid == 0) {
		// check if we have a dive site within 20 meters of that gps fix
		uuid = get_dive_site_uuid_by_gps_proximity(latitude, longitude, 20, &ds);

		if (ds) {
			// found a site nearby; in case it turns out this one had a different name let's
			// remember the original coordinates so we can create the correct dive site later
			cur_latitude = latitude;
			cur_longitude = longitude;
			dive->dive_site_uuid = uuid;
		} else {
			dive->dive_site_uuid = create_dive_site_with_gps("", latitude, longitude, dive->when);
			ds = get_dive_site_by_uuid(dive->dive_site_uuid);
		}
	} else {
		ds = get_dive_site_by_uuid(uuid);
		if (dive_site_has_gps_location(ds) &&
		    (latitude.udeg != 0 || longitude.udeg != 0) &&
		    (ds->latitude.udeg != latitude.udeg || ds->longitude.udeg != longitude.udeg)) {
			// Houston, we have a problem
			fprintf(stderr, "dive site uuid in dive, but gps location (%10.6f/%10.6f) different from dive location (%10.6f/%10.6f)\n",
				ds->latitude.udeg / 1000000.0, ds->longitude.udeg / 1000000.0,
				latitude.udeg / 1000000.0, longitude.udeg / 1000000.0);
			const char *coords = printGPSCoords(latitude.udeg, longitude.udeg);
			ds->notes = add_to_string(ds->notes, translate("gettextFromC", "multiple GPS locations for this dive site; also %s\n"), coords);
			free((void *)coords);
		} else {
			ds->latitude = latitude;
			ds->longitude = longitude;
		}
	}
}

static void gps_picture_location(char *buffer, struct picture *pic)
{
	char *end;

	pic->latitude = parse_degrees(buffer, &end);
	pic->longitude = parse_degrees(end, &end);
}

/* We're in the top-level dive xml. Try to convert whatever value to a dive value */
static void try_to_fill_dive(struct dive *dive, const char *name, char *buf)
{
	char *hash;
	start_match("dive", name, buf);

	switch (import_source) {
	case DIVINGLOG:
		if (divinglog_dive_match(dive, name, buf))
			return;
		break;

	case UDDF:
		if (uddf_dive_match(dive, name, buf))
			return;
		break;

	default:
		break;
	}
	if (MATCH("divesiteid", hex_value, &dive->dive_site_uuid))
		return;
	if (MATCH("number", get_index, &dive->number))
		return;
	if (MATCH("tags", divetags, &dive->tag_list))
		return;
	if (MATCH("tripflag", get_tripflag, &dive->tripflag))
		return;
	if (MATCH("date", divedate, &dive->when))
		return;
	if (MATCH("time", divetime, &dive->when))
		return;
	if (MATCH("datetime", divedatetime, &dive->when))
		return;
	/*
	 * Legacy format note: per-dive depths and duration get saved
	 * in the first dive computer entry
	 */
	if (match_dc_data_fields(&dive->dc, name, buf))
		return;

	if (MATCH("filename.picture", utf8_string, &cur_picture->filename))
		return;
	if (MATCH("offset.picture", offsettime, &cur_picture->offset))
		return;
	if (MATCH("gps.picture", gps_picture_location, cur_picture))
		return;
	if (MATCH("hash.picture", utf8_string, &hash)) {
		/* Legacy -> ignore. */
		free(hash);
		return;
	}
	if (MATCH("cylinderstartpressure", pressure, &dive->cylinder[0].start))
		return;
	if (MATCH("cylinderendpressure", pressure, &dive->cylinder[0].end))
		return;
	if (MATCH("gps", gps_in_dive, dive))
		return;
	if (MATCH("Place", gps_in_dive, dive))
		return;
	if (MATCH("latitude", gps_lat, dive))
		return;
	if (MATCH("sitelat", gps_lat, dive))
		return;
	if (MATCH("lat", gps_lat, dive))
		return;
	if (MATCH("longitude", gps_long, dive))
		return;
	if (MATCH("sitelon", gps_long, dive))
		return;
	if (MATCH("lon", gps_long, dive))
		return;
	if (MATCH("location", add_dive_site, dive))
		return;
	if (MATCH("name.dive", add_dive_site, dive))
		return;
	if (MATCH("suit", utf8_string, &dive->suit))
		return;
	if (MATCH("divesuit", utf8_string, &dive->suit))
		return;
	if (MATCH("notes", utf8_string, &dive->notes))
		return;
	if (MATCH("divemaster", utf8_string, &dive->divemaster))
		return;
	if (MATCH("buddy", utf8_string, &dive->buddy))
		return;
	if (MATCH("rating.dive", get_rating, &dive->rating))
		return;
	if (MATCH("visibility.dive", get_rating, &dive->visibility))
		return;
	if (cur_ws_index < MAX_WEIGHTSYSTEMS) {
		if (MATCH("description.weightsystem", utf8_string, &dive->weightsystem[cur_ws_index].description))
			return;
		if (MATCH("weight.weightsystem", weight, &dive->weightsystem[cur_ws_index].weight))
			return;
		if (MATCH("weight", weight, &dive->weightsystem[cur_ws_index].weight))
			return;
	}
	if (cur_cylinder_index < MAX_CYLINDERS) {
		if (MATCH("size.cylinder", cylindersize, &dive->cylinder[cur_cylinder_index].type.size))
			return;
		if (MATCH("workpressure.cylinder", pressure, &dive->cylinder[cur_cylinder_index].type.workingpressure))
			return;
		if (MATCH("description.cylinder", utf8_string, &dive->cylinder[cur_cylinder_index].type.description))
			return;
		if (MATCH("start.cylinder", pressure, &dive->cylinder[cur_cylinder_index].start))
			return;
		if (MATCH("end.cylinder", pressure, &dive->cylinder[cur_cylinder_index].end))
			return;
		if (MATCH("use.cylinder", cylinder_use, &dive->cylinder[cur_cylinder_index].cylinder_use))
			return;
		if (MATCH("depth.cylinder", depth, &dive->cylinder[cur_cylinder_index].depth))
			return;
		if (MATCH("o2", gasmix, &dive->cylinder[cur_cylinder_index].gasmix.o2))
			return;
		if (MATCH("o2percent", gasmix, &dive->cylinder[cur_cylinder_index].gasmix.o2))
			return;
		if (MATCH("n2", gasmix_nitrogen, &dive->cylinder[cur_cylinder_index].gasmix))
			return;
		if (MATCH("he", gasmix, &dive->cylinder[cur_cylinder_index].gasmix.he))
			return;
	}
	if (MATCH("air.divetemperature", temperature, &dive->airtemp))
		return;
	if (MATCH("water.divetemperature", temperature, &dive->watertemp))
		return;

	nonmatch("dive", name, buf);
}

/* We're in the top-level trip xml. Try to convert whatever value to a trip value */
static void try_to_fill_trip(dive_trip_t **dive_trip_p, const char *name, char *buf)
{
	start_match("trip", name, buf);

	dive_trip_t *dive_trip = *dive_trip_p;

	if (MATCH("date", divedate, &dive_trip->when))
		return;
	if (MATCH("time", divetime, &dive_trip->when))
		return;
	if (MATCH("location", utf8_string, &dive_trip->location))
		return;
	if (MATCH("notes", utf8_string, &dive_trip->notes))
		return;

	nonmatch("trip", name, buf);
}

/* We're processing a divesite entry - try to fill the components */
static void try_to_fill_dive_site(struct dive_site **ds_p, const char *name, char *buf)
{
	start_match("divesite", name, buf);

	struct dive_site *ds = *ds_p;
	if (ds->taxonomy.category == NULL)
		ds->taxonomy.category = alloc_taxonomy();

	if (MATCH("uuid", hex_value, &ds->uuid))
		return;
	if (MATCH("name", utf8_string, &ds->name))
		return;
	if (MATCH("description", utf8_string, &ds->description))
		return;
	if (MATCH("notes", utf8_string, &ds->notes))
		return;
	if (MATCH("gps", gps_location, ds))
		return;
	if (MATCH("cat.geo", get_index, (int *)&ds->taxonomy.category[ds->taxonomy.nr].category))
		return;
	if (MATCH("origin.geo", get_index, (int *)&ds->taxonomy.category[ds->taxonomy.nr].origin))
		return;
	if (MATCH("value.geo", utf8_string, &ds->taxonomy.category[ds->taxonomy.nr].value)) {
		if (ds->taxonomy.nr < TC_NR_CATEGORIES)
			ds->taxonomy.nr++;
		return;
	}

	nonmatch("divesite", name, buf);
}

static bool entry(const char *name, char *buf)
{
	if (!strncmp(name, "version.program", sizeof("version.program") - 1) ||
	    !strncmp(name, "version.divelog", sizeof("version.divelog") - 1)) {
		last_xml_version = atoi(buf);
		report_datafile_version(last_xml_version);
	}
	if (in_userid) {
		try_to_fill_userid(name, buf);
		return true;
	}
	if (in_settings) {
		try_to_fill_dc_settings(name, buf);
		try_to_match_autogroup(name, buf);
		return true;
	}
	if (cur_dive_site) {
		try_to_fill_dive_site(&cur_dive_site, name, buf);
		return true;
	}
	if (!cur_event.deleted) {
		try_to_fill_event(name, buf);
		return true;
	}
	if (cur_sample) {
		try_to_fill_sample(cur_sample, name, buf);
		return true;
	}
	if (cur_dc) {
		try_to_fill_dc(cur_dc, name, buf);
		return true;
	}
	if (cur_dive) {
		try_to_fill_dive(cur_dive, name, buf);
		return true;
	}
	if (cur_trip) {
		try_to_fill_trip(&cur_trip, name, buf);
		return true;
	}
	return true;
}

static const char *nodename(xmlNode *node, char *buf, int len)
{
	int levels = 2;
	char *p = buf;

	if (!node || (node->type != XML_CDATA_SECTION_NODE && !node->name)) {
		return "root";
	}

	if (node->type == XML_CDATA_SECTION_NODE || (node->parent && !strcmp((const char *)node->name, "text")))
		node = node->parent;

	/* Make sure it's always NUL-terminated */
	p[--len] = 0;

	for (;;) {
		const char *name = (const char *)node->name;
		char c;
		while ((c = *name++) != 0) {
			/* Cheaper 'tolower()' for ASCII */
			c = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
			*p++ = c;
			if (!--len)
				return buf;
		}
		*p = 0;
		node = node->parent;
		if (!node || !node->name)
			return buf;
		*p++ = '.';
		if (!--len)
			return buf;
		if (!--levels)
			return buf;
	}
}

#define MAXNAME 32

static bool visit_one_node(xmlNode *node)
{
	xmlChar *content;
	static char buffer[MAXNAME];
	const char *name;

	content = node->content;
	if (!content || xmlIsBlankNode(node))
		return true;

	name = nodename(node, buffer, sizeof(buffer));

	return entry(name, (char *)content);
}

static bool traverse(xmlNode *root);

static bool traverse_properties(xmlNode *node)
{
	xmlAttr *p;
	bool ret = true;

	for (p = node->properties; p; p = p->next)
		if ((ret = traverse(p->children)) == false)
			break;
	return ret;
}

static bool visit(xmlNode *n)
{
	return visit_one_node(n) && traverse_properties(n) && traverse(n->children);
}

static void DivingLog_importer(void)
{
	import_source = DIVINGLOG;

	/*
	 * Diving Log units are really strange.
	 *
	 * Temperatures are in C, except in samples,
	 * when they are in Fahrenheit. Depths are in
	 * meters, an dpressure is in PSI in the samples,
	 * but in bar when it comes to working pressure.
	 *
	 * Crazy f*%^ morons.
	 */
	xml_parsing_units = SI_units;
}

static void uddf_importer(void)
{
	import_source = UDDF;
	xml_parsing_units = SI_units;
	xml_parsing_units.pressure = PASCAL;
	xml_parsing_units.temperature = KELVIN;
}

static void subsurface_webservice(void)
{
	import_source = SSRF_WS;
}

/*
 * I'm sure this could be done as some fancy DTD rules.
 * It's just not worth the headache.
 */
static struct nesting {
	const char *name;
	void (*start)(void), (*end)(void);
} nesting[] = {
	  { "divecomputerid", dc_settings_start, dc_settings_end },
	  { "settings", settings_start, settings_end },
	  { "site", dive_site_start, dive_site_end },
	  { "dive", dive_start, dive_end },
	  { "Dive", dive_start, dive_end },
	  { "trip", trip_start, trip_end },
	  { "sample", sample_start, sample_end },
	  { "waypoint", sample_start, sample_end },
	  { "SAMPLE", sample_start, sample_end },
	  { "reading", sample_start, sample_end },
	  { "event", event_start, event_end },
	  { "mix", cylinder_start, cylinder_end },
	  { "gasmix", cylinder_start, cylinder_end },
	  { "cylinder", cylinder_start, cylinder_end },
	  { "weightsystem", ws_start, ws_end },
	  { "divecomputer", divecomputer_start, divecomputer_end },
	  { "P", sample_start, sample_end },
	  { "userid", userid_start, userid_stop},
	  { "picture", picture_start, picture_end },
	  { "extradata", extra_data_start, extra_data_end },

	  /* Import type recognition */
	  { "Divinglog", DivingLog_importer },
	  { "uddf", uddf_importer },
	  { "output", subsurface_webservice },
	  { NULL, }
  };

static bool traverse(xmlNode *root)
{
	xmlNode *n;
	bool ret = true;

	for (n = root; n; n = n->next) {
		struct nesting *rule = nesting;

		if (!n->name) {
			if ((ret = visit(n)) == false)
				break;
			continue;
		}

		do {
			if (!strcmp(rule->name, (const char *)n->name))
				break;
			rule++;
		} while (rule->name);

		if (rule->start)
			rule->start();
		if ((ret = visit(n)) == false)
			break;
		if (rule->end)
			rule->end();
	}
	return ret;
}

/* Per-file reset */
static void reset_all(void)
{
	/*
	 * We reset the units for each file. You'd think it was
	 * a per-dive property, but I'm not going to trust people
	 * to do per-dive setup. If the xml does have per-dive
	 * data within one file, we might have to reset it per
	 * dive for that format.
	 */
	xml_parsing_units = SI_units;
	import_source = UNKNOWN;
}

/* divelog.de sends us xml files that claim to be iso-8859-1
 * but once we decode the HTML encoded characters they turn
 * into UTF-8 instead. So skip the incorrect encoding
 * declaration and decode the HTML encoded characters */
static const char *preprocess_divelog_de(const char *buffer)
{
	char *ret = strstr(buffer, "<DIVELOGSDATA>");

	if (ret) {
		xmlParserCtxtPtr ctx;
		char buf[] = "";
		size_t i;

		for (i = 0; i < strlen(ret); ++i)
			if (!isascii(ret[i]))
				return buffer;

		ctx = xmlCreateMemoryParserCtxt(buf, sizeof(buf));
		ret = (char *)xmlStringLenDecodeEntities(ctx, (xmlChar *)ret, strlen(ret), XML_SUBSTITUTE_REF, 0, 0, 0);

		return ret;
	}
	return buffer;
}

int parse_xml_buffer(const char *url, const char *buffer, int size,
		      struct dive_table *table, const char **params)
{
	UNUSED(size);
	xmlDoc *doc;
	const char *res = preprocess_divelog_de(buffer);
	int ret = 0;

	target_table = table;
	doc = xmlReadMemory(res, strlen(res), url, NULL, 0);
	if (!doc)
		doc = xmlReadMemory(res, strlen(res), url, "latin1", 0);

	if (res != buffer)
		free((char *)res);

	if (!doc)
		return report_error(translate("gettextFromC", "Failed to parse '%s'"), url);

	prefs.save_userid_local = false;
	reset_all();
	dive_start();
	doc = test_xslt_transforms(doc, params);
	if (!traverse(xmlDocGetRootElement(doc))) {
		// we decided to give up on parsing... why?
		ret = -1;
	}
	dive_end();
	xmlFreeDoc(doc);
	return ret;
}

/*
 * Parse a unsigned 32-bit integer in little-endian mode,
 * that is seconds since Jan 1, 2000.
 */
static timestamp_t parse_dlf_timestamp(unsigned char *buffer)
{
	timestamp_t offset;

	offset = buffer[3];
	offset = (offset << 8) + buffer[2];
	offset = (offset << 8) + buffer[1];
	offset = (offset << 8) + buffer[0];

	// Jan 1, 2000 is 946684800 seconds after Jan 1, 1970, which is
	// the Unix epoch date that "timestamp_t" uses.
	return offset + 946684800;
}

int parse_dlf_buffer(unsigned char *buffer, size_t size)
{
	unsigned char *ptr = buffer;
	unsigned char event;
	bool found;
	unsigned int time = 0;
	int i;
	char serial[6];

	target_table = &dive_table;

	// Check for the correct file magic
	if (ptr[0] != 'D' || ptr[1] != 'i' || ptr[2] != 'v' || ptr[3] != 'E')
		return -1;

	dive_start();
	divecomputer_start();

	cur_dc->model = strdup("DLF import");
	// (ptr[7] << 8) + ptr[6] Is "Serial"
	snprintf(serial, sizeof(serial), "%d", (ptr[7] << 8) + ptr[6]);
	cur_dc->serial = strdup(serial);
	cur_dc->when = parse_dlf_timestamp(ptr + 8);
	cur_dive->when = cur_dc->when;

	cur_dc->duration.seconds = ((ptr[14] & 0xFE) << 16) + (ptr[13] << 8) + ptr[12];

	// ptr[14] >> 1 is scrubber used in %

	// 3 bit dive type
	switch((ptr[15] & 0x30) >> 3) {
	case 0: // unknown
	case 1:
		cur_dc->divemode = OC;
		break;
	case 2:
		cur_dc->divemode = CCR;
		break;
	case 3:
		cur_dc->divemode = CCR; // mCCR
		break;
	case 4:
		cur_dc->divemode = FREEDIVE;
		break;
	case 5:
		cur_dc->divemode = OC; // Gauge
		break;
	case 6:
		cur_dc->divemode = PSCR; // ASCR
		break;
	case 7:
		cur_dc->divemode = PSCR;
		break;
	}

	cur_dc->maxdepth.mm = ((ptr[21] << 8) + ptr[20]) * 10;
	cur_dc->surface_pressure.mbar = ((ptr[25] << 8) + ptr[24]) / 10;

	/* Done with parsing what we know about the dive header */
	ptr += 32;

	// We're going to interpret ppO2 saved as a sensor value in these modes.
	if (cur_dc->divemode == CCR || cur_dc->divemode == PSCR)
		cur_dc->no_o2sensors = 1;

	for (; ptr < buffer + size; ptr += 16) {
		time = ((ptr[0] >> 4) & 0x0f) +
			((ptr[1] << 4) & 0xff0) +
			((ptr[2] << 12) & 0x1f000);
		event = ptr[0] & 0x0f;
		switch (event) {
		case 0:
			/* Regular sample */
			sample_start();
			cur_sample->time.seconds = time;
			cur_sample->depth.mm = ((ptr[5] << 8) + ptr[4]) * 10;
			// Crazy precision on these stored values...
			// Only store value if we're in CCR/PSCR mode,
			// because we rather calculate ppo2 our selfs.
			if (cur_dc->divemode == CCR || cur_dc->divemode == PSCR)
				cur_sample->o2sensor[0].mbar = ((ptr[7] << 8) + ptr[6]) / 10;

			// In some test files, ndl / tts / temp is bogus if this bits are 1
			// flag bits in ptr[11] & 0xF0 is probably involved to,
			if ((ptr[2] >> 5) != 1) {
				// NDL in minutes, 10 bit
				cur_sample->ndl.seconds = (((ptr[9] & 0x03) << 8) + ptr[8]) * 60;
				// TTS in minutes, 10 bit
				cur_sample->tts.seconds = (((ptr[10] & 0x0F) << 6) + (ptr[9] >> 2)) * 60;
				// Temperature in 1/10 C, 10 bit signed
				cur_sample->temperature.mkelvin = ((ptr[11] & 0x20) ? -1 : 1)  * (((ptr[11] & 0x1F) << 4) + (ptr[10] >> 4)) * 100 + ZERO_C_IN_MKELVIN;
			}
			cur_sample->stopdepth.mm = ((ptr[13] << 8) + ptr[12]) * 10;
			if (cur_sample->stopdepth.mm)
				cur_sample->in_deco = true;
			//ptr[14] is helium content, always zero?
			//ptr[15] is setpoint, what the computer thinks you should aim for?
			sample_end();
			break;
		case 1: /* dive event */
		case 2: /* automatic parameter change */
		case 3: /* diver error */
		case 4: /* internal error */
		case 5: /* device activity log */
			//Event 18 is a button press. Lets ingore that event.
			if (ptr[4] == 18)
				continue;

			event_start();
			cur_event.time.seconds = time;
			switch (ptr[4]) {
			case 1:
				strcpy(cur_event.name, "Setpoint Manual");
				cur_event.value = ptr[6];
				sample_start();
				cur_sample->setpoint.mbar = ptr[6] * 10;
				sample_end();
				break;
			case 2:
				strcpy(cur_event.name, "Setpoint Auto");
				cur_event.value = ptr[6];
				sample_start();
				cur_sample->setpoint.mbar = ptr[6] * 10;
				sample_end();
				switch (ptr[7]) {
				case 0:
					strcat(cur_event.name, " Manual");
					break;
				case 1:
					strcat(cur_event.name, " Auto Start");
					break;
				case 2:
					strcat(cur_event.name, " Auto Hypox");
					break;
				case 3:
					strcat(cur_event.name, " Auto Timeout");
					break;
				case 4:
					strcat(cur_event.name, " Auto Ascent");
					break;
				case 5:
					strcat(cur_event.name, " Auto Stall");
					break;
				case 6:
					strcat(cur_event.name, " Auto SP Low");
					break;
				default:
					break;
				}
				break;
			case 3:
				// obsolete
				strcpy(cur_event.name, "OC");
				break;
			case 4:
				// obsolete
				strcpy(cur_event.name, "CCR");
				break;
			case 5:
				strcpy(cur_event.name, "gaschange");
				cur_event.type = SAMPLE_EVENT_GASCHANGE2;
				cur_event.value = ptr[7] << 8 ^ ptr[6];

				found = false;
				for (i = 0; i < cur_cylinder_index; ++i) {
					if (cur_dive->cylinder[i].gasmix.o2.permille == ptr[6] * 10 && cur_dive->cylinder[i].gasmix.he.permille == ptr[7] * 10) {
						found = true;
						break;
					}
				}
				if (!found) {
					cylinder_start();
					cur_dive->cylinder[cur_cylinder_index].gasmix.o2.permille = ptr[6] * 10;
					cur_dive->cylinder[cur_cylinder_index].gasmix.he.permille = ptr[7] * 10;
					cylinder_end();
					cur_event.gas.index = cur_cylinder_index;
				} else {
					cur_event.gas.index = i;
				}
				break;
			case 6:
				strcpy(cur_event.name, "Start");
				break;
			case 7:
				strcpy(cur_event.name, "Too Fast");
				break;
			case 8:
				strcpy(cur_event.name, "Above Ceiling");
				break;
			case 9:
				strcpy(cur_event.name, "Toxic");
				break;
			case 10:
				strcpy(cur_event.name, "Hypox");
				break;
			case 11:
				strcpy(cur_event.name, "Critical");
				break;
			case 12:
				strcpy(cur_event.name, "Sensor Disabled");
				break;
			case 13:
				strcpy(cur_event.name, "Sensor Enabled");
				break;
			case 14:
				strcpy(cur_event.name, "O2 Backup");
				break;
			case 15:
				strcpy(cur_event.name, "Peer Down");
				break;
			case 16:
				strcpy(cur_event.name, "HS Down");
				break;
			case 17:
				strcpy(cur_event.name, "Inconsistent");
				break;
			case 18:
				// key pressed - It should never get in here
				// as we ingored it at the parent 'case 5'.
				break;
			case 19:
				// obsolete
				strcpy(cur_event.name, "SCR");
				break;
			case 20:
				strcpy(cur_event.name, "Above Stop");
				break;
			case 21:
				strcpy(cur_event.name, "Safety Miss");
				break;
			case 22:
				strcpy(cur_event.name, "Fatal");
				break;
			case 23:
				strcpy(cur_event.name, "gaschange");
				cur_event.type = SAMPLE_EVENT_GASCHANGE2;
				cur_event.value = ptr[7] << 8 ^ ptr[6];
				event_end();
				break;
			case 24:
				strcpy(cur_event.name, "gaschange");
				cur_event.type = SAMPLE_EVENT_GASCHANGE2;
				cur_event.value = ptr[7] << 8 ^ ptr[6];
				event_end();
				// This is both a mode change and a gas change event
				// so we encode it as two separate events.
				event_start();
				strcpy(cur_event.name, "Change Mode");
				switch (ptr[8]) {
				case 1:
					strcat(cur_event.name, ": OC");
					break;
				case 2:
					strcat(cur_event.name, ": CCR");
					break;
				case 3:
					strcat(cur_event.name, ": mCCR");
					break;
				case 4:
					strcat(cur_event.name, ": Free");
					break;
				case 5:
					strcat(cur_event.name, ": Gauge");
					break;
				case 6:
					strcat(cur_event.name, ": ASCR");
					break;
				case 7:
					strcat(cur_event.name, ": PSCR");
					break;
				default:
					break;
				}
				event_end();
				break;
			case 25:
				strcpy(cur_event.name, "CCR O2 solenoid opened/closed");
				break;
			case 26:
				strcpy(cur_event.name, "User mark");
				break;
			case 27:
				snprintf(cur_event.name, MAX_EVENT_NAME, "%sGF Switch (%d/%d)", ptr[6] ? "Bailout, ": "", ptr[7], ptr[8]);
				break;
			case 28:
				strcpy(cur_event.name, "Peer Up");
				break;
			case 29:
				strcpy(cur_event.name, "HS Up");
				break;
			case 30:
				snprintf(cur_event.name, MAX_EVENT_NAME, "CNS %d%%", ptr[6]);
				break;
			default:
				// No values above 30 had any description
				break;
			}
			event_end();
			break;
		case 6:
			/* device configuration */
			break;
		case 7:
			/* measure record */
			switch (ptr[2] >> 5) {
			case 1:
				/* Measure Battery */
				//printf("B1: %dmV %d% B2: %dmV %d%\n", (ptr[5] << 8) + ptr[4], (ptr[7] << 8) + ptr[6], (ptr[9] << 8) + ptr[8], (ptr[11] << 8) + ptr[10]);
			case 3:
				/* Measure Oxygen */
				//printf("o2 cells(0.01 mV): %d %d %d %d\n", (ptr[5] << 8) + ptr[4], (ptr[7] << 8) + ptr[6], (ptr[9] << 8) + ptr[8], (ptr[11] << 8) + ptr[10]);
				break;
			case 4:
				/* Measure GPS */
				cur_latitude.udeg =  (int)((ptr[7]  << 24) + (ptr[6]  << 16) + (ptr[5] << 8) + (ptr[4] << 0));
				cur_longitude.udeg = (int)((ptr[11] << 24) + (ptr[10] << 16) + (ptr[9] << 8) + (ptr[8] << 0));
				cur_dive->dive_site_uuid = create_dive_site_with_gps(NULL, cur_latitude, cur_longitude, cur_dive->when);
				const char * coords = printGPSCoords(cur_latitude.udeg, cur_longitude.udeg);
				printf("gps: %s\n", coords);
				free((void *)coords);
				break;
			default:
				break;
			}
			break;
		default:
			/* Unknown... */
			break;
		}
	}
	divecomputer_end();
	dive_end();
	return 0;
}


void parse_xml_init(void)
{
	LIBXML_TEST_VERSION
}

void parse_xml_exit(void)
{
	xmlCleanupParser();
}

static struct xslt_files {
	const char *root;
	const char *file;
	const char *attribute;
} xslt_files[] = {
	  { "SUUNTO", "SuuntoSDM.xslt", NULL },
	  { "Dive", "SuuntoDM4.xslt", "xmlns" },
	  { "Dive", "shearwater.xslt", "version" },
	  { "JDiveLog", "jdivelog2subsurface.xslt", NULL },
	  { "dives", "MacDive.xslt", NULL },
	  { "DIVELOGSDATA", "divelogs.xslt", NULL },
	  { "uddf", "uddf.xslt", NULL },
	  { "UDDF", "uddf.xslt", NULL },
	  { "profile", "udcf.xslt", NULL },
	  { "Divinglog", "DivingLog.xslt", NULL },
	  { "csv", "csv2xml.xslt", NULL },
	  { "sensuscsv", "sensuscsv.xslt", NULL },
	  { "SubsurfaceCSV", "subsurfacecsv.xslt", NULL },
	  { "manualcsv", "manualcsv2xml.xslt", NULL },
	  { "logbook", "DiveLog.xslt", NULL },
	  { "AV1", "av1.xslt", NULL },
	  { NULL, }
  };

static xmlDoc *test_xslt_transforms(xmlDoc *doc, const char **params)
{
	struct xslt_files *info = xslt_files;
	xmlDoc *transformed;
	xsltStylesheetPtr xslt = NULL;
	xmlNode *root_element = xmlDocGetRootElement(doc);
	char *attribute;

	while (info->root) {
		if ((strcasecmp((const char *)root_element->name, info->root) == 0)) {
			if (info->attribute == NULL)
				break;
			else if (xmlGetProp(root_element, (const xmlChar *)info->attribute) != NULL)
				break;
		}
		info++;
	}

	if (info->root) {
		attribute = (char *)xmlGetProp(xmlFirstElementChild(root_element), (const xmlChar *)"name");
		if (attribute) {
			if (strcasecmp(attribute, "subsurface") == 0) {
				free((void *)attribute);
				return doc;
			}
			free((void *)attribute);
		}
		xmlSubstituteEntitiesDefault(1);
		xslt = get_stylesheet(info->file);
		if (xslt == NULL) {
			report_error(translate("gettextFromC", "Can't open stylesheet %s"), info->file);
			return doc;
		}
		transformed = xsltApplyStylesheet(xslt, doc, params);
		xmlFreeDoc(doc);
		xsltFreeStylesheet(xslt);

		return transformed;
	}
	return doc;
}
