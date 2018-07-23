#ifndef METADATA_H
#define METADATA_H

#include "units.h"

struct metadata {
	timestamp_t	timestamp;
	duration_t	duration;
	degrees_t	latitude;
	degrees_t	longitude;
};

enum mediatype_t {
	MEDIATYPE_UNKNOWN,		// Couldn't (yet) identify file
	MEDIATYPE_IO_ERROR,		// Couldn't read file
	MEDIATYPE_PICTURE,
	MEDIATYPE_VIDEO,
	MEDIATYPE_STILL_LOADING,	// Still processing in the background
};

#ifdef __cplusplus
extern "C" {
#endif

enum mediatype_t get_metadata(const char *filename, struct metadata *data);
timestamp_t picture_get_timestamp(const char *filename);

#ifdef __cplusplus
}
#endif

#endif // METADATA_H
