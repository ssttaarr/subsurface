// SPDX-License-Identifier: GPL-2.0
/* statistics.c
 *
 * core logic for the Info & Stats page -
 * char *get_minutes(int seconds);
 * void process_all_dives(struct dive *dive, struct dive **prev_dive);
 */
#include "gettext.h"
#include <string.h>
#include <ctype.h>

#include "dive.h"
#include "display.h"
#include "divelist.h"
#include "statistics.h"

static stats_t stats;
stats_t stats_selection;
stats_t *stats_monthly = NULL;
stats_t *stats_yearly = NULL;
stats_t *stats_by_trip = NULL;
stats_t *stats_by_type = NULL;

static void process_temperatures(struct dive *dp, stats_t *stats)
{
	temperature_t min_temp, mean_temp, max_temp = {.mkelvin = 0};

	max_temp.mkelvin = dp->maxtemp.mkelvin;
	if (max_temp.mkelvin && (!stats->max_temp.mkelvin || max_temp.mkelvin > stats->max_temp.mkelvin))
		stats->max_temp.mkelvin = max_temp.mkelvin;

	min_temp.mkelvin = dp->mintemp.mkelvin;
	if (min_temp.mkelvin && (!stats->min_temp.mkelvin || min_temp.mkelvin < stats->min_temp.mkelvin))
		stats->min_temp.mkelvin = min_temp.mkelvin;

	if (min_temp.mkelvin || max_temp.mkelvin) {
		mean_temp.mkelvin = min_temp.mkelvin;
		if (mean_temp.mkelvin)
			mean_temp.mkelvin = (mean_temp.mkelvin + max_temp.mkelvin) / 2;
		else
			mean_temp.mkelvin = max_temp.mkelvin;
		stats->combined_temp.mkelvin += mean_temp.mkelvin;
		stats->combined_count++;
	}
}

static void process_dive(struct dive *dive, stats_t *stats)
{
	int old_tadt, sac_time = 0;
	int32_t duration = dive->duration.seconds;

	old_tadt = stats->total_average_depth_time.seconds;
	stats->total_time.seconds += duration;
	if (duration > stats->longest_time.seconds)
		stats->longest_time.seconds = duration;
	if (stats->shortest_time.seconds == 0 || duration < stats->shortest_time.seconds)
		stats->shortest_time.seconds = duration;
	if (dive->maxdepth.mm > stats->max_depth.mm)
		stats->max_depth.mm = dive->maxdepth.mm;
	if (stats->min_depth.mm == 0 || dive->maxdepth.mm < stats->min_depth.mm)
		stats->min_depth.mm = dive->maxdepth.mm;

	process_temperatures(dive, stats);

	/* Maybe we should drop zero-duration dives */
	if (!duration)
		return;
	if (dive->meandepth.mm) {
		stats->total_average_depth_time.seconds += duration;
		stats->avg_depth.mm = lrint((1.0 * old_tadt * stats->avg_depth.mm +
					duration * dive->meandepth.mm) /
					stats->total_average_depth_time.seconds);
	}
	if (dive->sac > 100) { /* less than .1 l/min is bogus, even with a pSCR */
		sac_time = stats->total_sac_time.seconds + duration;
		stats->avg_sac.mliter = lrint((1.0 * stats->total_sac_time.seconds * stats->avg_sac.mliter +
					 duration * dive->sac) /
					 sac_time);
		if (dive->sac > stats->max_sac.mliter)
			stats->max_sac.mliter = dive->sac;
		if (stats->min_sac.mliter == 0 || dive->sac < stats->min_sac.mliter)
			stats->min_sac.mliter = dive->sac;
		stats->total_sac_time.seconds = sac_time;
	}
}

char *get_minutes(int seconds)
{
	static char buf[80];
	snprintf(buf, sizeof(buf), "%d:%.2d", FRACTION(seconds, 60));
	return buf;
}

void process_all_dives(struct dive *dive, struct dive **prev_dive)
{
	int idx;
	struct dive *dp;
	struct tm tm;
	int current_year = 0;
	int current_month = 0;
	int year_iter = 0;
	int month_iter = 0;
	int prev_month = 0, prev_year = 0;
	int trip_iter = 0;
	dive_trip_t *trip_ptr = 0;
	unsigned int size, tsize;

	*prev_dive = NULL;
	memset(&stats, 0, sizeof(stats));
	if (dive_table.nr > 0) {
		stats.shortest_time.seconds = dive_table.dives[0]->duration.seconds;
		stats.min_depth.mm = dive_table.dives[0]->maxdepth.mm;
		stats.selection_size = dive_table.nr;
	}

	/* allocate sufficient space to hold the worst
	 * case (one dive per year or all dives during
	 * one month) for yearly and monthly statistics*/

	free(stats_yearly);
	free(stats_monthly);
	free(stats_by_trip);
	free(stats_by_type);

	size = sizeof(stats_t) * (dive_table.nr + 1);
	tsize = sizeof(stats_t) * (NUM_DIVEMODE + 1);
	stats_yearly = malloc(size);
	stats_monthly = malloc(size);
	stats_by_trip = malloc(size);
	stats_by_type = malloc(tsize);
	if (!stats_yearly || !stats_monthly || !stats_by_trip || !stats_by_type)
		return;
	memset(stats_yearly, 0, size);
	memset(stats_monthly, 0, size);
	memset(stats_by_trip, 0, size);
	memset(stats_by_type, 0, tsize);
	stats_yearly[0].is_year = true;

	/* Setting the is_trip to true to show the location as first
	 * field in the statistics window */
	stats_by_type[0].location = strdup(translate("gettextFromC", "All (by type stats)"));
	stats_by_type[0].is_trip = true;
	stats_by_type[1].location = strdup(translate("gettextFromC", divemode_text_ui[OC]));
	stats_by_type[1].is_trip = true;
	stats_by_type[2].location = strdup(translate("gettextFromC", divemode_text_ui[CCR]));
	stats_by_type[2].is_trip = true;
	stats_by_type[3].location = strdup(translate("gettextFromC", divemode_text_ui[PSCR]));
	stats_by_type[3].is_trip = true;
	stats_by_type[4].location = strdup(translate("gettextFromC", divemode_text_ui[FREEDIVE]));
	stats_by_type[4].is_trip = true;

	/* this relies on the fact that the dives in the dive_table
	 * are in chronological order */
	for_each_dive (idx, dp) {
		if (dive && dp->when == dive->when) {
			/* that's the one we are showing */
			if (idx > 0)
				*prev_dive = dive_table.dives[idx - 1];
		}
		process_dive(dp, &stats);

		/* yearly statistics */
		utc_mkdate(dp->when, &tm);
		if (current_year == 0)
			current_year = tm.tm_year;

		if (current_year != tm.tm_year) {
			current_year = tm.tm_year;
			process_dive(dp, &(stats_yearly[++year_iter]));
			stats_yearly[year_iter].is_year = true;
		} else {
			process_dive(dp, &(stats_yearly[year_iter]));
		}
		stats_yearly[year_iter].selection_size++;
		stats_yearly[year_iter].period = current_year;

		/* stats_by_type[0] is all the dives combined */
		stats_by_type[0].selection_size++;
		process_dive(dp, &(stats_by_type[0]));

		process_dive(dp, &(stats_by_type[dp->dc.divemode + 1]));
		stats_by_type[dp->dc.divemode + 1].selection_size++;

		if (dp->divetrip != NULL) {
			if (trip_ptr != dp->divetrip) {
				trip_ptr = dp->divetrip;
				trip_iter++;
			}

			/* stats_by_trip[0] is all the dives combined */
			stats_by_trip[0].selection_size++;
			process_dive(dp, &(stats_by_trip[0]));
			stats_by_trip[0].is_trip = true;
			stats_by_trip[0].location = strdup(translate("gettextFromC", "All (by trip stats)"));

			process_dive(dp, &(stats_by_trip[trip_iter]));
			stats_by_trip[trip_iter].selection_size++;
			stats_by_trip[trip_iter].is_trip = true;
			stats_by_trip[trip_iter].location = dp->divetrip->location;
		}

		/* monthly statistics */
		if (current_month == 0) {
			current_month = tm.tm_mon + 1;
		} else {
			if (current_month != tm.tm_mon + 1)
				current_month = tm.tm_mon + 1;
			if (prev_month != current_month || prev_year != current_year)
				month_iter++;
		}
		process_dive(dp, &(stats_monthly[month_iter]));
		stats_monthly[month_iter].selection_size++;
		stats_monthly[month_iter].period = current_month;
		prev_month = current_month;
		prev_year = current_year;
	}
}

/* make sure we skip the selected summary entries */
void process_selected_dives(void)
{
	struct dive *dive;
	unsigned int i, nr;

	memset(&stats_selection, 0, sizeof(stats_selection));

	nr = 0;
	for_each_dive(i, dive) {
		if (dive->selected) {
			process_dive(dive, &stats_selection);
			nr++;
		}
	}
	stats_selection.selection_size = nr;
}

#define SOME_GAS 5000 // 5bar drop in cylinder pressure makes cylinder used

bool has_gaschange_event(struct dive *dive, struct divecomputer *dc, int idx)
{
	bool first_gas_explicit = false;
	struct event *event = get_next_event(dc->events, "gaschange");
	while (event) {
		if (dc->sample && (event->time.seconds == 0 ||
				   (dc->samples && dc->sample[0].time.seconds == event->time.seconds)))
			first_gas_explicit = true;
		if (get_cylinder_index(dive, event) == idx)
			return true;
		event = get_next_event(event->next, "gaschange");
	}
	if (dc->divemode == CCR) {
		if (idx == get_cylinder_idx_by_use(dive, DILUENT))
			return true;
		if (idx == get_cylinder_idx_by_use(dive, OXYGEN))
			return true;
	}
	return !first_gas_explicit && idx == 0;
}

bool is_cylinder_used(struct dive *dive, int idx)
{
	struct divecomputer *dc;
	if (cylinder_none(&dive->cylinder[idx]))
		return false;

	if ((dive->cylinder[idx].start.mbar - dive->cylinder[idx].end.mbar) > SOME_GAS)
		return true;

	if ((dive->cylinder[idx].sample_start.mbar - dive->cylinder[idx].sample_end.mbar) > SOME_GAS)
		return true;

	for_each_dc(dive, dc) {
		if (has_gaschange_event(dive, dc, idx))
			return true;
	}
	return false;
}

bool is_cylinder_prot(struct dive *dive, int idx)
{
	struct divecomputer *dc;
	if (cylinder_none(&dive->cylinder[idx]))
		return false;

	for_each_dc(dive, dc) {
		if (has_gaschange_event(dive, dc, idx))
			return true;
	}
	return false;
}

void get_gas_used(struct dive *dive, volume_t gases[MAX_CYLINDERS])
{
	int idx;

	for (idx = 0; idx < MAX_CYLINDERS; idx++) {
		cylinder_t *cyl = &dive->cylinder[idx];
		pressure_t start, end;

		start = cyl->start.mbar ? cyl->start : cyl->sample_start;
		end = cyl->end.mbar ? cyl->end : cyl->sample_end;
		if (end.mbar && start.mbar > end.mbar)
			gases[idx].mliter = gas_volume(cyl, start) - gas_volume(cyl, end);
	}
}

/* Quite crude reverse-blender-function, but it produces a approx result */
static void get_gas_parts(struct gasmix mix, volume_t vol, int o2_in_topup, volume_t *o2, volume_t *he)
{
	volume_t air = {};

	if (gasmix_is_air(&mix)) {
		o2->mliter = 0;
		he->mliter = 0;
		return;
	}

	air.mliter = lrint(((double)vol.mliter * (1000 - get_he(&mix) - get_o2(&mix))) / (1000 - o2_in_topup));
	he->mliter = lrint(((double)vol.mliter * get_he(&mix)) / 1000.0);
	o2->mliter += vol.mliter - he->mliter - air.mliter;
}

void selected_dives_gas_parts(volume_t *o2_tot, volume_t *he_tot)
{
	int i, j;
	struct dive *d;
	for_each_dive (i, d) {
		if (!d->selected)
			continue;
		volume_t diveGases[MAX_CYLINDERS] = {};
		get_gas_used(d, diveGases);
		for (j = 0; j < MAX_CYLINDERS; j++) {
			if (diveGases[j].mliter) {
				volume_t o2 = {}, he = {};
				get_gas_parts(d->cylinder[j].gasmix, diveGases[j], O2_IN_AIR, &o2, &he);
				o2_tot->mliter += o2.mliter;
				he_tot->mliter += he.mliter;
			}
		}
	}
}
