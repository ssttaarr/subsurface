// SPDX-License-Identifier: GPL-2.0
#include "metadata.h"
#include "exif.h"
#include "qthelper.h"
#include <QString>
#include <QFile>
#include <QDateTime>

// Weirdly, android builds fail owing to undefined UINT64_MAX
#ifndef UINT64_MAX
#define UINT64_MAX (~0ULL)
#endif

// The following functions fetch an arbitrary-length _unsigned_ integer from either
// a file or a memory location in big-endian or little-endian mode. The size of the
// integer is passed via a template argument [e.g. getBE<uint16_t>(...)].
// The functions doing file access return a default value on IO error or end-of-file.
// Warning: This code works properly only for unsigned integers. The template parameter
// is not checked and passing a signed integer will silently fail!
template <typename T>
static inline T getBE(const char *buf_in)
{
	constexpr size_t size = sizeof(T);
	// Interpret raw bytes as unsigned char to avoid sign extension for
	// characters in the 0x80...0xff range.
	auto buf = (unsigned const char *)buf_in;
	T ret = 0;
	for (size_t i = 0; i < size; ++i)
		ret = (ret << 8) | buf[i];
	return ret;
}

template <typename T>
static inline T getBE(QFile &f, T def=0)
{
	constexpr size_t size = sizeof(T);
	char buf[size];
	if (f.read(buf, size) != size)
		return def;
	return getBE<T>(buf);
}

template <typename T>
static inline T getLE(const char *buf_in)
{
	constexpr size_t size = sizeof(T);
	// Interpret raw bytes as unsigned char to avoid sign extension for
	// characters in the 0x80...0xff range.
	auto buf = (unsigned const char *)buf_in;
	T ret = 0;
	for (size_t i = 0; i < size; ++i)
		ret |= static_cast<T>(buf[i]) << (i * 8);
	return ret;
}

template <typename T>
static inline T getLE(QFile &f, T def=0)
{
	constexpr size_t size = sizeof(T);
	char buf[size];
	if (f.read(buf, size) != size)
		return def;
	return getLE<T>(buf);
}

static bool parseExif(QFile &f, struct metadata *metadata)
{
	f.seek(0);
	if (getBE<uint16_t>(f) != 0xffd8)
		return false;
	for (;;) {
		switch (getBE<uint16_t>(f)) {
		case 0xffc0:
		case 0xffc2:
		case 0xffc4:
		case 0xffd0 ... 0xffd7:
		case 0xffdb:
		case 0xffdd:
		case 0xffe0:
		case 0xffe2 ... 0xffef:
		case 0xfffe: {
			uint16_t len = getBE<uint16_t>(f);
			if (len < 2)
				return false;
			f.seek(f.pos() + len - 2); // TODO: switch to QFile::skip()
			break;
		}
		case 0xffe1: {
			uint16_t len = getBE<uint16_t>(f);
			if (len < 2)
				return false;
			len -= 2;
			QByteArray data = f.read(len);
			if (data.size() != len)
				return false;
			easyexif::EXIFInfo exif;
			if (exif.parseFromEXIFSegment(reinterpret_cast<const unsigned char *>(data.constData()), len) != PARSE_EXIF_SUCCESS)
				return false;
			metadata->longitude.udeg = lrint(1000000.0 * exif.GeoLocation.Longitude);
			metadata->latitude.udeg = lrint(1000000.0 * exif.GeoLocation.Latitude);
			metadata->timestamp = exif.epoch();
			return true;
		}
		case 0xffda:
		case 0xffd9:
			// We expect EXIF data before any scan data
			return false;
		default:
			return false;
		}
	}
}

static bool parseMP4(QFile &f, metadata *metadata)
{
	f.seek(0);

	// MP4s and related formats are hierarchical, being made up of "atoms", which can
	// contain other atoms (an interesting interpretation of the term atom).
	// To parse the file, the remaining to-be-parsed bytes of the upper atoms in
	// the parse-tree are tracked in a stack-like structure. This is not strictly
	// necessary, since the level at which an atom is found is insubstantial.
	// Nevertheless, it is an effective and simple way of sanity-checking the file and the
	// parsing routine.
	std::vector<uint64_t> atom_stack;
	atom_stack.reserve(10);

	// For the outmost level, set the atom-size the the maximum value representable in
	// 64-bits, which effectively means parse to the end of file.
	atom_stack.push_back(UINT64_MAX);

	// The first atom of an MP4 or related video is supposed to be of the "ftyp" kind.
	// If such an atom is found as first atom, this function will return true, indicating
	// that the file is a video.
	bool found_ftyp = false;

	while (!f.atEnd() && !atom_stack.empty()) {
		// Parse atom header. The header can have two forms (each character stands for a byte):
		//	lllltttt
		// or
		// 	0001ttttllllllll
		// where "l" stands for length in big-endian mode and "t" for type of the atom.
		// The length includes the 8- or 16-bytes header.
		uint64_t atom_size = getBE<uint32_t>(f, 2);
		int atom_header_size = 8;
		if (atom_size > 1 && atom_size < 8)
			break;
		char type[4];
		if (f.read(type, 4) != 4)
			break;
		if (atom_size == 1) {
			atom_size = getBE<uint64_t>(f);
			atom_header_size = 16;
			if (atom_size < 16)
			       break;
		}
		if (atom_size == 0)
			atom_size = atom_stack.back();
		if (atom_size > atom_stack.back())
			break;
		atom_stack.back() -= atom_size;
		atom_size -= atom_header_size;

		// The first atom must be "ftyp"
		if (!found_ftyp) {
			found_ftyp = !memcmp(type, "ftyp", 4);
			if (!found_ftyp)
				break;
		}

		if (!memcmp(type, "moov", 4) ||
		    !memcmp(type, "trak", 4) ||
		    !memcmp(type, "mdia", 4)) {
			// Recurse into "moov", "trak" and "mdia" atoms
			atom_stack.push_back(atom_size);
			continue;
		} else if (!memcmp(type, "mdhd", 4) && atom_size >= 24 && atom_size < 4096) {
			// Parse "mdhd" (media header).
			// Sanity check: size between 24 and 4096
			std::vector<char> data(atom_size);
			if (f.read(&data[0], atom_size) != static_cast<int>(atom_size))
				break;
			uint64_t timestamp = 0;
			uint32_t timescale = 0;
			uint64_t duration = 0;
			// First byte is version. We know version 0 and 1
			switch (data[0]) {
			case 0:
				timestamp = getBE<uint32_t>(&data[4]);
				timescale = getBE<uint32_t>(&data[12]);
				duration = getBE<uint32_t>(&data[16]);
				break;
			case 1:
				timestamp = getBE<uint64_t>(&data[4]);
				timescale = getBE<uint32_t>(&data[20]);
				duration = getBE<uint64_t>(&data[24]);
				break;
			default:
				// For unknown versions: ignore -> maybe we find a parseable "mdhd" atom later in this file
				break;
			}
			if (timescale > 0)
				metadata->duration.seconds = lrint((double)duration / timescale);
			// Timestamp is given as seconds since midnight 1904/1/1. To be convertible to the UNIX epoch
			// it must be larger than 2082844800.
			if (timestamp >= 2082844800) {
				metadata->timestamp = timestamp - 2082844800;
				// Currently, we only know how to extract timestamps, so we might just quit parsing here.
				break;
			}
		} else {
			// Jump over unknown atom
			if (!f.seek(f.pos() + atom_size)) // TODO: switch to QFile::skip()
				break;
		}

		// If end of atom is reached, return to outer atom
		while (!atom_stack.empty() && atom_stack.back() == 0)
			atom_stack.pop_back();
	}

	return found_ftyp;
}

static QStringList weekdays = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
static QStringList months = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };

static bool parseDate(const QString &s_in, timestamp_t &timestamp)
{
	// As a first attempt we're very crude: replace all '/' and '-' by ':'
	// and try to see if this is of the form "yyyy:mm:dd hh:mm:ss".
	// Since AVIs have no unified way of saving dates, we will have
	// to find out empirically what different software produces.
	// Note that we don't want to parse dates without time. That would
	// be too imprecise and in such a case we'd rather go after the
	// file modification date.
	QString s = s_in;
	s.replace('/', ':');
	s.replace('-', ':');
	QDateTime datetime = QDateTime::fromString(s, "yyyy:M:d h:m:s");
	if (datetime.isValid()) {
		// Not knowing any better, we suppose that time is give in UTC
		datetime.setTimeSpec(Qt::UTC);
		timestamp = datetime.toMSecsSinceEpoch() / 1000;
		return true;
	}

	// I've also seen "Weekday Mon  Day hh:mm:ss yyyy"(!)
	QStringList items = s.split(' ', QString::SkipEmptyParts);
	if (items.size() < 4)
		return false;

	// Skip weekday if any is given
	for (const QString &day: weekdays) {
		if (items[0].startsWith(day, Qt::CaseInsensitive)) {
			items.removeFirst();
			break;
		}
	}
	if (items.size() < 4)
		return false;
	int month;
	for (month = 0; month < 12; ++month)
		if (items[0].startsWith(months[month], Qt::CaseInsensitive))
			break;
	if (month >= 12)
		return false;
	bool ok;
	int day = items[1].toInt(&ok, 10);
	if (!ok)
		return false;
	QTime time = QTime::fromString(items[2], "h:m:s");
	if (!time.isValid())
		return false;
	int year = items[3].toInt(&ok, 10);
	if (!ok)
		return false;
	QDate date(year, month + 1, day);
	if (!date.isValid())
		return false;

	// Not knowing any better, we suppose that time is give in UTC
	datetime = QDateTime(date, time, Qt::UTC);
	if (datetime.isValid()) {
		timestamp = datetime.toMSecsSinceEpoch() / 1000;
		return true;
	}

	return false;
}

static bool parseAVI(QFile &f, metadata *metadata)
{
	f.seek(0);

	// Like MP4s, AVIs are hierarchical, being made up of  "chunks" and "lists",
	// whereby the latter can contain more "chunks" and "lists".
	// All elements are padded to an even-byte value. I.e. if the length of en element
	// is odd, then a padding byte is introduced.
	// To parse the file, the remaining to-be-parsed bytes of the upper lists in
	// the parse-tree are tracked in a stack-like structure. This is not strictly
	// necessary, since the level at which a chunk is found is insubstantial.
	// Nevertheless, it is an effective and simple way of sanity-checking the file and the
	// parsing routine.
	std::vector<uint64_t> list_stack;
	list_stack.reserve(10);

	// For the outmost level, set the chunk-size the the maximum value representable in
	// 64-bits, which effectively means parse to the end of file.
	list_stack.push_back(UINT64_MAX);

	// The first element of an AVI is supposed to be a "RIFF" list.
	// If such a list is found as first element, this function will return true, indicating
	// that the file is a video.
	bool found_riff = false;

	// Find creation date and duration. If we found both, we may quit.
	bool found_date = false;
	bool found_duration = false;
	while (!f.atEnd() && !list_stack.empty() && (!found_date || !found_duration)) {
		// Parse chunk/list header. If the first four bytes are "RIFF" or "LIST", then this
		// is a list. Otherwise, it is an chunk.
		char type[4];
		if (f.read(type, 4) != 4)
			break;

		// The first element must be RIFF
		if (!found_riff) {
			found_riff = !memcmp(type, "RIFF", 4);
			if (!found_riff)
				break;
		}

		uint32_t len = getLE<uint32_t>(f);
		// Elements are always padded to word (16-bit) boundaries
		uint32_t len_in_file = len + (len & 1);
		if (len_in_file + 8 > list_stack.back())
			break;
		list_stack.back() -= len_in_file + 8;

		// Check if this is a list
		if (!memcmp(type, "RIFF", 4) || !memcmp(type, "LIST", 4)) {
			// This is a list
			// The format is as follows:
			//	4 bytes "RIFF" or "LIST"
			//	4 bytes length (not including this and the previous entry)
			//	4 bytes type
			//	n bytes data
			// length includes the 4 bytes type
			if (len < 4)
				break;
			char list_type[4];
			if (f.read(list_type, 4) != 4)
				break;

			if (!memcmp(list_type, "AVI ", 4) || !memcmp(list_type, "hdrl", 4) ||
			    !memcmp(list_type, "strl", 4) || !memcmp(list_type, "INFO", 4)) {
				// Recurse into "AVI ", "hdrl", "strl" and "INFO" lists
				list_stack.push_back(len_in_file - 4);
				continue;
			} else {
				// Skip other lists
				if (!f.seek(f.pos() + len_in_file - 4)) // TODO: switch to QFile::skip()
					break;
			}
		} else if (!memcmp(type, "strh", 4) && !found_duration) {
			// The stream header contains the duration information. We will just assume that
			// the stream header is the correct one.
			// Before reading, sanity-check the length.
			if (len < 48 || len > 4096)
				break;
			std::vector<char> data(len_in_file);
			if (f.read(data.data(), len_in_file) != len_in_file)
				break;
			double scale = getLE<uint32_t>(&data[20]);
			double rate = getLE<uint32_t>(&data[24]);
			double start = getLE<uint32_t>(&data[28]);
			double length = getLE<uint32_t>(&data[32]);
			double duration = (start + length) * scale / rate;
			metadata->duration.seconds = lrint(duration);
			found_duration = true;
		} else if (!memcmp(type, "IDIT", 4) || !memcmp(type, "ICRD", 4)) {
			// "IDIT" of "ICRD" chunks may contain the creation date/time of the file
			// First, sanity-check the length.
			if (len > 4096)
				break;
			std::vector<char> data(len_in_file);
			if (f.read(data.data(), len_in_file) != len_in_file)
				break;
			QString idit = QString::fromUtf8(data.data(), len);
			// In my test file, the string contained a '\0' terminator. Remove it.
			idit.remove(QChar(0));
			found_date = parseDate(idit, metadata->timestamp);
		} else {
			if (!f.seek(f.pos() + len_in_file)) // TODO: switch to QFile::skip()
				break;
		}

		// If end of current list is reached, return to outer list
		while (!list_stack.empty() && list_stack.back() == 0)
			list_stack.pop_back();
	}
	return found_riff;
}

static bool parseASF(QFile &f, metadata *metadata)
{
	f.seek(0);

	// Parse the header of the header object:
	//	id				(16 bytes)
	//	size				 (8 bytes)
	//	number of header objects	 (4 bytes)
	//	reserved			 (2 bytes)
	//	------------------------------------------
	//	total				(30 bytes)
	char header[30];
	if (f.read(header, 30) != 30)
		return false;

	// Check if this is indeed an ASF header.
	if (memcmp(&header[0], "\x30\x26\xb2\x75\x8e\x66\xcf\x11\xa6\xd9\x00\xaa\x00\x62\xce\x6c", 16) != 0)
		return false;

	uint64_t header_len = getLE<uint64_t>(&header[16]);
	uint32_t num = getLE<uint32_t>(&header[24]);

	// Sanity check
	if (header_len <= 30 || num > 10000)
		return false;
	header_len -= 30;

	// Read through all the header objects
	for (uint32_t i = 0; i < num && header_len > 24; ++i) {
		// Each objects starts with the same header:
		//	id			(16 bytes)
		//	size			 (8 bytes)
		char data[24];
		if (f.read(data, 24) != 24)
			return false;

		uint64_t object_len = getLE<uint64_t>(&data[16]);
		// Sanity check
		if (object_len < 24 || object_len > header_len)
			return false;

		header_len -= object_len;
		object_len -= 24;
		if (!memcmp(data, "\xa1\xdc\xab\x8c\x47\xa9\xcf\x11\x8e\xe4\x0\xc0\xc\x20\x53\x65", 16) != 0) {
			// This is a file properties object. The interesting data are:
			//	quadword (64 bit) at byte 24: creation date in 100-nanoseconds since Jan. 1, 1601.
			//	quadword (64 bit) at byte 40: duration in 100-nanoseconds.
			//	quadword (64 bit) at byte 56: offset in msec (to be subtracted from duration)
			// But first a sanity check:
			if (object_len < 80 || object_len > 4096)
				break;

			std::vector<char> v(object_len);
			if (f.read(v.data(), object_len) != (int)object_len)
				break;

			uint64_t creation_date = getLE<uint64_t>(&v[24]);
			// OK - first convert to seconds
			creation_date /= 10000000;
			// Check if this is during the UNIX epoch and convert into epoch
			if (creation_date <= 11644473600)
				metadata->timestamp = 0;		// Can't determine creation date, sorry!
			else
				metadata->timestamp = creation_date - 11644473600;

			uint64_t duration = getLE<uint64_t>(&v[40]);
			uint64_t offset = getLE<uint64_t>(&v[56]);
			metadata->duration.seconds = lrint(duration / 10000000.0 - offset / 1000.0);

			// We found everything that we wanted -> return success
			return true;
		} else {
			// Skip over unknown object
			if (!f.seek(f.pos() + object_len)) // TODO: switch to QFile::skip()
				break;
		}
	}

	// We didn't find a file properties object. According to the ASF specification, this is
	// *not* a valid ASF-file. Return failure accordingly.
	return false;
}

extern "C" mediatype_t get_metadata(const char *filename_in, metadata *data)
{
	data->timestamp = 0;
	data->duration.seconds = 0;
	data->latitude.udeg = 0;
	data->longitude.udeg = 0;

	QString filename = localFilePath(QString(filename_in));
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly))
		return MEDIATYPE_IO_ERROR;

	mediatype_t res = MEDIATYPE_UNKNOWN;
	if (parseExif(f, data))
		res = MEDIATYPE_PICTURE;
	else if(parseMP4(f, data))
		res = MEDIATYPE_VIDEO;
	else if(parseAVI(f, data))
		res = MEDIATYPE_VIDEO;
	else if(parseASF(f, data))
		res = MEDIATYPE_VIDEO;

	// If we couldn't get a creation date from the file (for example AVI files don't
	// have a standard way of storing this datum), use the file creation date of the file.
	// TODO: QFileInfo::created is deprecated in newer Qt versions.
	if (data->timestamp == 0)
		data->timestamp = QFileInfo(filename).created().toMSecsSinceEpoch() / 1000;
	return res;
}

extern "C" timestamp_t picture_get_timestamp(const char *filename)
{
	struct metadata data;
	get_metadata(filename, &data);
	return data.timestamp;
}
