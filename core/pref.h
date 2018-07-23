// SPDX-License-Identifier: GPL-2.0
#ifndef PREF_H
#define PREF_H

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

#include "units.h"
#include "taxonomy.h"

typedef struct
{
	bool po2;
	bool pn2;
	bool phe;
	double po2_threshold_min;
	double po2_threshold_max;
	double pn2_threshold;
	double phe_threshold;
} partial_pressure_graphs_t;

typedef struct {
	const char *access_token;
	const char *user_id;
	const char *album_id;
} facebook_prefs_t;

typedef struct {
	enum taxonomy_category category[3];
} geocoding_prefs_t;

typedef struct {
	const char *language;
	const char *lang_locale;
	bool use_system_language;
} locale_prefs_t;

enum deco_mode {
	BUEHLMANN,
	RECREATIONAL,
	VPMB
};

typedef struct {
	bool dont_check_for_updates;
	bool dont_check_exists;
	const char *last_version_used;
	const char *next_check;
} update_manager_prefs_t;

typedef struct {
	const char *vendor;
	const char *product;
	const char *device;
	const char *device_name;
	int download_mode;
} dive_computer_prefs_t;

// ********** PREFERENCES **********
// This struct is kept global for all of ssrf
// most of the fields are loaded from git as
// part of the dives, but some fields are loaded
// from local storage (QSettings)
// The struct is divided in groups (sorted)
// and elements within the group is sorted
//
// When adding items to this list, please keep
// the list sorted (easier to find something)
struct preferences {
	// ********** Animations **********
	int animation_speed;

	// ********** CloudStorage **********
	const char *cloud_base_url;
	const char *cloud_git_url;
	const char *cloud_storage_email;
	const char *cloud_storage_email_encoded;
	const char *cloud_storage_newpassword;
	const char *cloud_storage_password;
	const char *cloud_storage_pin;
	short       cloud_timeout;
	short       cloud_verification_status;
	bool        git_local_only;
	bool        save_password_local;
	bool        save_userid_local;
	const char *userid;

	// ********** DiveComputer **********
	dive_computer_prefs_t dive_computer;

	// ********** Display **********
	bool        display_invalid_dives;
	const char *divelist_font;
	double      font_size;
	bool        show_developer;
 	const char *theme;

	// ********** Facebook **********
	facebook_prefs_t facebook;

	// ********** General **********
	bool        auto_recalculate_thumbnails;
	int         defaultsetpoint; // default setpoint in mbar
	const char *default_cylinder;
	const char *default_filename;
	short       default_file_behavior;
	int         o2consumption; // ml per min
	int         pscr_ratio; // dump ratio times 1000
	bool        use_default_file;

	// ********** Geocoding **********
	geocoding_prefs_t geocoding;

	// ********** Language **********
	const char *    date_format;
	bool            date_format_override;
	const char *    date_format_short;
	locale_prefs_t  locale; //: TODO: move the rest of locale based info here.
	const char *    time_format;
	bool            time_format_override;

	// ********** LocationService **********
	int time_threshold;
	int distance_threshold;

	// ********** Network **********
	bool        proxy_auth;
	const char *proxy_host;
	int         proxy_port;
	int         proxy_type;
	const char *proxy_user;
	const char *proxy_pass;

	// ********** Planner **********
	int             ascratelast6m;
	int             ascratestops;
	int             ascrate50;
	int             ascrate75; // All rates in mm / sec
	depth_t         bestmixend;
	int             bottompo2;
	int             bottomsac;
	int             decopo2;
	int             decosac;
	int             descrate;
	bool            display_duration;
	bool            display_runtime;
	bool            display_transitions;
	bool            display_variations;
	bool            doo2breaks;
	bool            drop_stone_mode;
	bool            last_stop;   // At 6m?
	int             min_switch_duration; // seconds
	enum deco_mode  planner_deco_mode;
	int             problemsolvingtime;
	int             reserve_gas;
	int             sacfactor;
	bool            safetystop;
	bool            switch_at_req_stop;
	bool            verbatim_plan;

	// ********** TecDetails **********
	bool                        calcalltissues;
	bool                        calcceiling;
	bool                        calcceiling3m;
	bool                        calcndltts;
	bool                        dcceiling;
	enum deco_mode              display_deco_mode;
	bool                        display_unused_tanks;
	bool                        ead;
	short                       gfhigh;
	short                       gflow;
	bool                        gf_low_at_maxdepth;
	bool                        hrgraph;
	bool                        mod;
	double                      modpO2;
	bool                        percentagegraph;
	partial_pressure_graphs_t   pp_graphs;
	bool                        redceiling;
	bool                        rulergraph;
	bool                        show_average_depth;
	bool                        show_ccr_sensors;
	bool                        show_ccr_setpoint;
	bool                        show_icd;
	bool                        show_pictures_in_profile;
	bool                        show_sac;
	bool                        show_scr_ocpo2;
	bool                        tankbar;
	short                       vpmb_conservatism;
	bool                        zoomed_plot;

	// ********** Units **********
	bool            coordinates_traditional;
	short           unit_system;
	struct units    units;

	// ********** UpdateManager **********
	update_manager_prefs_t update_manager;
};

enum unit_system_values {
	METRIC,
	IMPERIAL,
	PERSONALIZE
};

enum def_file_behavior {
	UNDEFINED_DEFAULT_FILE,
	LOCAL_DEFAULT_FILE,
	NO_DEFAULT_FILE,
	CLOUD_DEFAULT_FILE
};

extern struct preferences prefs, default_prefs, git_prefs;

extern const char *system_divelist_default_font;
extern double system_divelist_default_font_size;

extern const char *system_default_directory(void);
extern const char *system_default_filename();
extern bool subsurface_ignore_font(const char *font);
extern void subsurface_OS_pref_setup();
extern void copy_prefs(struct preferences *src, struct preferences *dest);

#ifdef __cplusplus
}
#endif

#endif // PREF_H
