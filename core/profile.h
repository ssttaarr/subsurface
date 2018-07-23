// SPDX-License-Identifier: GPL-2.0
#ifndef PROFILE_H
#define PROFILE_H

#include "dive.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	STABLE,
	SLOW,
	MODERATE,
	FAST,
	CRAZY
} velocity_t;

struct membuffer;
struct divecomputer;
struct plot_info;
struct plot_data {
	unsigned int in_deco : 1;
	int sec;
	/*
	 * pressure[x][0] is sensor pressure for cylinder x
	 * pressure[x][1] is interpolated pressure
	 */
	int pressure[MAX_CYLINDERS][2];
	int temperature;
	/* Depth info */
	int depth;
	int ceiling;
	int ceilings[16];
	int percentages[16];
	int ndl;
	int tts;
	int rbt;
	int stoptime;
	int stopdepth;
	int cns;
	int smoothed;
	int sac;
	int running_sum;
	struct gas_pressures pressures;
	pressure_t o2pressure;  // for rebreathers, this is consensus measured po2, or setpoint otherwise. 0 for OC.
	pressure_t o2sensor[3]; //for rebreathers with up to 3 PO2 sensors
	pressure_t o2setpoint;
	pressure_t scr_OC_pO2;
	double mod, ead, end, eadd;
	velocity_t velocity;
	int speed;
	// stats over 9 minute window:
	int min, max;	// indices into pi->entry[]
	/* values calculated by us */
	unsigned int in_deco_calc : 1;
	int ndl_calc;
	int tts_calc;
	int stoptime_calc;
	int stopdepth_calc;
	int pressure_time;
	int heartbeat;
	int bearing;
	double ambpressure;
	double gfline;
	double density;
	bool icd_warning;
};

struct ev_select {
	char *ev_name;
	bool plot_ev;
};

struct plot_info calculate_max_limits_new(struct dive *dive, struct divecomputer *given_dc);
void compare_samples(struct plot_data *e1, struct plot_data *e2, char *buf, int bufsize, int sum);
struct plot_data *populate_plot_entries(struct dive *dive, struct divecomputer *dc, struct plot_info *pi);
struct plot_info *analyze_plot_info(struct plot_info *pi);
void create_plot_info_new(struct dive *dive, struct divecomputer *dc, struct plot_info *pi, bool fast, struct deco_state *planner_ds);
void calculate_deco_information(struct deco_state *ds, struct deco_state *planner_de, struct dive *dive, struct divecomputer *dc, struct plot_info *pi, bool print_mode);
struct plot_data *get_plot_details_new(struct plot_info *pi, int time, struct membuffer *);

/*
 * When showing dive profiles, we scale things to the
 * current dive. However, we don't scale past less than
 * 30 minutes or 90 ft, just so that small dives show
 * up as such unless zoom is enabled.
 * We also need to add 180 seconds at the end so the min/max
 * plots correctly
 */
int get_maxtime(struct plot_info *pi);

/* get the maximum depth to which we want to plot
 * take into account the additional verical space needed to plot
 * partial pressure graphs */
int get_maxdepth(struct plot_info *pi);

#define SENSOR_PR 0
#define INTERPOLATED_PR 1
#define SENSOR_PRESSURE(_entry,_idx) (_entry)->pressure[_idx][SENSOR_PR]
#define INTERPOLATED_PRESSURE(_entry,_idx) (_entry)->pressure[_idx][INTERPOLATED_PR]
#define GET_PRESSURE(_entry,_idx) (SENSOR_PRESSURE(_entry,_idx) ? SENSOR_PRESSURE(_entry,_idx) : INTERPOLATED_PRESSURE(_entry,_idx))
#define SAC_WINDOW 45 /* sliding window in seconds for current SAC calculation */

#ifdef __cplusplus
}
#endif
#endif // PROFILE_H
