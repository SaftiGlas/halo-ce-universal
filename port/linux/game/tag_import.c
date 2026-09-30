/*
TAG_IMPORT.C

Brings tags from another map's cache file into the map that is loading
(game.import and game.import_into in config.toml; the mod launcher's
"import" in mods/mods.json sets them; see mods/FORGE_PLAN.md, "Cross-map
tags and AI"). It does what the game does not: the game has one cache file
at a time, and tag_load() refuses.

The donor map's tag data is read (the DVD's maps are the 0x800 byte header
and one zlib stream, which is inflated in memory), and the tags asked for
and every tag they refer to are found by their tag references. What the map
already has, by group and name, is used as it is; the rest is added:

- the donor's whole tag data is copied into contiguous ("physical") memory,
  where Direct3D finds vertex and index data, and every pointer in the added
  tags moves by how far the copy is from where the donor's data was
  (pointers are the words that fall in the donor's address range: verified
  on rocks, where every one lands in the tag's own data, the names, the
  buffer tables or the index data; raw vertex data, which could hold such
  words, is never scanned)
- each reference in an added tag gets the index the tag has in this map
- the donor's vertex and index buffer tables are relocated and registered
  as the game does its own (tags_header_register_vertex_and_index_buffers)
- bitmaps: pixels are read from the cache file by offset; the pixels of an
  added bitmap are copied to a store, and cache_file_read reads offsets from
  HALO_IMPORT_PIXEL_BASE on out of it (halo_tag_import_read)
- the map's tag instance table is replaced by a longer one with the added
  tags at its end, so the tag iterator, tag_get and the forge menu see them

Called from scenario_tags_load and scenario_tags_unload (cache/cache_files.c)
and cache_file_read (cache/cache_files_windows.c). Local games only in
practice: the tags exist on this machine alone.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cache/cache_files.h"
#include "memory/zlib/zlib.h"

#include <xtl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the game's debugging allocator (cseries.h) refuses free(NULL) and knows only
its own blocks; this file's memory is the C library's */
#undef malloc
#undef free
#undef realloc

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	IMPORT_HEADER_SIZE = 0x800,
	IMPORT_TAG_HEADER_SIZE = 0x24,
	IMPORT_MAXIMUM_REQUESTS = 64,
	IMPORT_MAXIMUM_DONORS = 4,
	IMPORT_MAXIMUM_RANGES = 3 * 256,
	IMPORT_SECTOR_SIZE = 0x800,
	IMPORT_NAME_SIZE = 256,
	/* the first tag index's salt: index i is (0xE174 + i) << 16 | i */
	IMPORT_FIRST_SALT = 0xE174,
	IMPORT_INFLATE_CHUNK = 0x100000
};

/* offsets of pixel data that cache_file_read serves from the store */
#define HALO_IMPORT_PIXEL_BASE 0x40000000L

/* ---------- structures (cache/cache_files.c's, which are private to it) */

struct import_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

struct import_tag_header
{
	struct import_instance *tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	void *vertex_buffers;
	long index_buffer_count;
	void *index_buffers;
	unsigned long signature;
};

struct import_cache_header
{
	unsigned long signature;
	long version;
	long file_length;
	byte reservedC[4];
	long tag_data_offset;
	long tag_data_size;
};

struct import_request
{
	unsigned long group_tag;
	char donor[64];
	char name[IMPORT_NAME_SIZE];
};

/* the parts of a bitmap group the import changes (bitmaps/bitmap_group.h) */
struct import_bitmap_data
{
	unsigned long signature;
	short width;
	short height;
	short depth;
	short type;
	short format;
	unsigned short flags;
	long registration_point;
	short mipmap_count;
	short mipmap_pad;
	long pixels_offset;
	long pixels_size;
	long tag_index;
	long cache_block_index;
	void *hardware_format;
	void *base_address;
};

/* a stretch of a tag's data (donor addresses) that is raw data, not to be scanned */
struct import_range
{
	unsigned long start;
	unsigned long end;
};

struct import_donor
{
	FILE *file;
	struct import_cache_header header;
	/* the inflated file after its header, for a compressed map */
	byte *inflated;
	/* the tag data, copied where Direct3D can reach it */
	byte *blob;
	unsigned long blob_size;
	unsigned long base;
	long delta;
	struct import_tag_header *tag_header;
	struct import_instance *instances;
	long tag_count;
	/* per tag: the end of its data, and its index in the map (NONE: not in the closure) */
	unsigned long *extent_end;
	long *map_index;
};

/* ---------- globals */

static struct
{
	void *blobs[IMPORT_MAXIMUM_DONORS];
	short blob_count;
	struct import_instance *instances;
	byte *pixels;
	long pixel_size;
	long pixel_capacity;
} tag_import_globals;

/* ---------- private code */

static unsigned long import_group_tag(
	char const *text)
{
	return ((unsigned long)(byte)text[0] << 24) | ((unsigned long)(byte)text[1] << 16) |
		((unsigned long)(byte)text[2] << 8) | (unsigned long)(byte)text[3];
}

static long import_salted_index(
	long index)
{
	return (long)((((unsigned long)IMPORT_FIRST_SALT + (unsigned long)index) << 16) | (unsigned long)index);
}

/* "a30:scen:scenery\rocks\rock|..." into requests; how many */
static short import_parse_requests(
	char const *text,
	struct import_request *requests)
{
	short count = 0;

	while (*text && count < IMPORT_MAXIMUM_REQUESTS)
	{
		char const *end = strchr(text, '|');
		size_t length = end ? (size_t)(end - text) : strlen(text);
		char entry[IMPORT_NAME_SIZE + 80];
		char *first;
		char *second;

		if (length < sizeof(entry))
		{
			memcpy(entry, text, length);
			entry[length] = 0;
			first = strchr(entry, ':');
			second = first ? strchr(first + 1, ':') : NULL;
			if (first && second && second - first == 5 && second[1] && first - entry < (long)sizeof(requests[0].donor))
			{
				*first = 0;
				*second = 0;
				memset(&requests[count], 0, sizeof(requests[count]));
				strcpy(requests[count].donor, entry);
				requests[count].group_tag = import_group_tag(first + 1);
				strncpy(requests[count].name, second + 1, sizeof(requests[count].name) - 1);
				count++;
			}
			else
			{
				platform_log("import: \"%s\" is not donor:group:tag name", entry);
			}
		}
		text = end ? end + 1 : text + length;
	}

	return count;
}

static boolean import_map_listed(
	char const *list,
	char const *map_name)
{
	size_t length = strlen(map_name);

	if (!*list)
		return _stricmp(map_name, "ui") != 0;
	while (*list)
	{
		char const *end = strchr(list, ',');
		size_t item = end ? (size_t)(end - list) : strlen(list);

		if (item == length && !_strnicmp(list, map_name, length))
			return TRUE;
		list = end ? end + 1 : list + item;
	}

	return FALSE;
}

/* reads size bytes of the donor's file at offset (after the header for a compressed map) */
static boolean import_donor_read(
	struct import_donor *donor,
	long offset,
	long size,
	void *buffer)
{
	if (donor->inflated)
	{
		if (offset < IMPORT_HEADER_SIZE || (unsigned long)(offset - IMPORT_HEADER_SIZE) + size > (unsigned long)donor->header.file_length - IMPORT_HEADER_SIZE)
			return FALSE;
		memcpy(buffer, donor->inflated + (offset - IMPORT_HEADER_SIZE), size);
		return TRUE;
	}

	return fseek(donor->file, offset, SEEK_SET) == 0 && fread(buffer, 1, size, donor->file) == (size_t)size;
}

/* opens the donor map; when it is the DVD's compressed file, inflates the rest of it into memory */
static boolean import_donor_open(
	struct import_donor *donor,
	char const *map_name)
{
	char path[256];
	long file_size;

	memset(donor, 0, sizeof(*donor));
	_snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), map_name);
	donor->file = fopen(path, "rb");
	if (!donor->file)
	{
		platform_log("import: cannot open %s", path);
		return FALSE;
	}
	if (fread(&donor->header, 1, sizeof(donor->header), donor->file) != sizeof(donor->header) ||
		donor->header.signature != 0x68656164 || donor->header.version != 5 ||
		fseek(donor->file, 0, SEEK_END) != 0)
	{
		platform_log("import: %s is not a cache file", path);
		return FALSE;
	}
	file_size = ftell(donor->file);
	if (file_size < donor->header.file_length)
	{
		/* one zlib stream after the header */
		z_stream stream;
		unsigned long total = (unsigned long)donor->header.file_length - IMPORT_HEADER_SIZE;
		byte *input = (byte *)malloc(IMPORT_INFLATE_CHUNK);
		int status = Z_OK;

		donor->inflated = (byte *)malloc(total);
		if (!donor->inflated || !input)
		{
			platform_log("import: no memory to inflate %s (%lu bytes)", path, total);
			free(input);
			return FALSE;
		}
		memset(&stream, 0, sizeof(stream));
		inflateInit(&stream);
		stream.next_out = donor->inflated;
		stream.avail_out = total;
		fseek(donor->file, IMPORT_HEADER_SIZE, SEEK_SET);
		while (status == Z_OK && stream.avail_out > 0)
		{
			stream.avail_in = (unsigned int)fread(input, 1, IMPORT_INFLATE_CHUNK, donor->file);
			stream.next_in = input;
			if (stream.avail_in == 0)
				break;
			status = inflate(&stream, Z_NO_FLUSH);
			if (status != Z_OK && status != Z_STREAM_END)
				break;
		}
		inflateEnd(&stream);
		free(input);
		if (stream.avail_out != 0)
		{
			platform_log("import: %s did not inflate (%d, %lu of %lu bytes)", path, status, total - stream.avail_out, total);
			return FALSE;
		}
	}

	return TRUE;
}

static void import_donor_close(
	struct import_donor *donor)
{
	if (donor->file)
		fclose(donor->file);
	free(donor->inflated);
	free(donor->extent_end);
	free(donor->map_index);

	return;
}

static char const *import_donor_string(
	struct import_donor const *donor,
	char const *address)
{
	unsigned long value = (unsigned long)address;

	return value >= donor->base && value < donor->base + donor->blob_size
		? (char const *)donor->blob + (value - donor->base)
		: "";
}

/* the donor tag a reference at these four words names, or NONE. Words before relocation. */
static long import_reference_target(
	struct import_donor const *donor,
	unsigned long const *words)
{
	unsigned long name_address = words[1];
	long index = (long)(words[3] & 0xFFFF);

	if (words[2] >= 256 || name_address < donor->base || name_address >= donor->base + donor->blob_size ||
		index >= donor->tag_count || (words[3] >> 16) != (unsigned long)IMPORT_FIRST_SALT + (unsigned long)index ||
		donor->instances[index].group_tag != (long)words[0])
	{
		return NONE;
	}

	return !strcmp(import_donor_string(donor, (char const *)name_address),
		import_donor_string(donor, donor->instances[index].name)) ? index : NONE;
}

/* the extent of each tag's data: up to the next tag's (the last, to the end of the tag data) */
static boolean import_donor_extents(
	struct import_donor *donor)
{
	unsigned long *sorted = (unsigned long *)malloc(sizeof(unsigned long) * 2 * donor->tag_count);
	long index;
	long count = 0;

	donor->extent_end = (unsigned long *)calloc(donor->tag_count, sizeof(unsigned long));
	donor->map_index = (long *)malloc(sizeof(long) * donor->tag_count);
	if (!sorted || !donor->extent_end || !donor->map_index)
	{
		free(sorted);
		return FALSE;
	}
	for (index = 0; index < donor->tag_count; index++)
	{
		unsigned long address = (unsigned long)donor->instances[index].base_address;

		donor->map_index[index] = NONE;
		if (address >= donor->base && address < donor->base + donor->blob_size)
		{
			sorted[count * 2] = address;
			sorted[count * 2 + 1] = (unsigned long)index;
			count++;
		}
	}
	{
		/* sort (address, index) pairs by address */
		long i;
		long j;

		for (i = 1; i < count; i++)
		{
			unsigned long address = sorted[i * 2];
			unsigned long tag = sorted[i * 2 + 1];

			for (j = i - 1; j >= 0 && sorted[j * 2] > address; j--)
			{
				sorted[(j + 1) * 2] = sorted[j * 2];
				sorted[(j + 1) * 2 + 1] = sorted[j * 2 + 1];
			}
			sorted[(j + 1) * 2] = address;
			sorted[(j + 1) * 2 + 1] = tag;
		}
	}
	for (index = 0; index < count; index++)
	{
		donor->extent_end[sorted[index * 2 + 1]] =
			index + 1 < count ? sorted[(index + 1) * 2] : donor->base + donor->blob_size;
	}
	free(sorted);

	return TRUE;
}

/* marks the donor tag and every tag it refers to, in mark[] */
static void import_mark_closure(
	struct import_donor const *donor,
	long tag_index,
	byte *mark)
{
	long *pending = (long *)malloc(sizeof(long) * donor->tag_count);
	long pending_count = 0;

	if (!pending)
		return;
	pending[pending_count++] = tag_index;
	mark[tag_index] = TRUE;
	while (pending_count > 0)
	{
		long current = pending[--pending_count];
		unsigned long address = (unsigned long)donor->instances[current].base_address;
		unsigned long end = donor->extent_end[current];
		unsigned long const *words;
		unsigned long offset;

		if (!end)
			continue;
		words = (unsigned long const *)(donor->blob + (address - donor->base));
		for (offset = 0; offset + 16 <= end - address; offset += 4)
		{
			long target = import_reference_target(donor, words + offset / 4);

			if (target != NONE && !mark[target])
			{
				mark[target] = TRUE;
				pending[pending_count++] = target;
			}
		}
	}
	free(pending);

	return;
}

/* an animation graph's raw data: the frame info, default data and frame data of each
animation (block at 116; 180 bytes each, their three tag_datas at 72, 140 and 160,
models/model_animation_definitions.h). Packed rotations and translations in them can
look like pointers, so the pointer scan leaves them alone; ranges are in the donor's
addresses. How many ranges. */
static long import_animation_ranges(
	struct import_donor const *donor,
	unsigned long address,
	struct import_range *ranges,
	long maximum)
{
	static short const data_offsets[3] = { 72, 140, 160 };
	unsigned long const *block = (unsigned long const *)(donor->blob + (address - donor->base) + 116);
	long count = (long)block[0];
	long index;
	long result = 0;

	if (count <= 0 || block[1] < donor->base || block[1] >= donor->base + donor->blob_size)
		return 0;
	for (index = 0; index < count && result + 3 <= maximum; index++)
	{
		unsigned long const *element = (unsigned long const *)(donor->blob + (block[1] - donor->base) + index * 180);
		short which;

		for (which = 0; which < 3; which++)
		{
			unsigned long size = element[data_offsets[which] / 4];
			unsigned long data = element[data_offsets[which] / 4 + 3];

			if (size && data >= donor->base && data + size <= donor->base + donor->blob_size)
			{
				ranges[result].start = data;
				ranges[result].end = data + size;
				result++;
			}
		}
	}

	return result;
}

/* copies size bytes of the donor's file at offset into the store (padded to a sector,
as cache_file_read reads them); the offset cache_file_read reads them at, or NONE */
static long import_store_add(
	struct import_donor *donor,
	long file_offset,
	long size)
{
	long padded = (size + IMPORT_SECTOR_SIZE - 1) & ~(IMPORT_SECTOR_SIZE - 1);
	long result;

	if (tag_import_globals.pixel_size + padded > tag_import_globals.pixel_capacity)
	{
		long capacity = MAX(tag_import_globals.pixel_capacity * 2, tag_import_globals.pixel_size + padded + 0x100000);
		byte *store = (byte *)realloc(tag_import_globals.pixels, capacity);

		if (!store)
		{
			platform_log("import: no memory for the store");
			return NONE;
		}
		tag_import_globals.pixels = store;
		tag_import_globals.pixel_capacity = capacity;
	}
	memset(tag_import_globals.pixels + tag_import_globals.pixel_size, 0, padded);
	if (!import_donor_read(donor, file_offset, size, tag_import_globals.pixels + tag_import_globals.pixel_size))
		platform_log("import: cannot read %ld bytes at %ld of the donor", size, file_offset);
	result = HALO_IMPORT_PIXEL_BASE + tag_import_globals.pixel_size;
	tag_import_globals.pixel_size += padded;

	return result;
}

/* the pixels of an added bitmap tag (its data already moved) go to the store;
its bitmaps read them from there */
static void import_bitmap_group(
	struct import_donor *donor,
	byte *group,
	long new_tag_index)
{
	/* a bitmap group's bitmaps block is at 96 (bitmaps/bitmap_group.h) */
	unsigned long const *block = (unsigned long const *)(group + 96);
	long count = (long)block[0];
	struct import_bitmap_data *bitmaps = (struct import_bitmap_data *)block[1];
	long index;

	for (index = 0; index < count; index++)
	{
		struct import_bitmap_data *bitmap = &bitmaps[index];
		long offset = import_store_add(donor, bitmap->pixels_offset, bitmap->pixels_size);

		if (offset != NONE)
			bitmap->pixels_offset = offset;
		bitmap->tag_index = new_tag_index;
	}

	return;
}

/* the samples of an added sound tag (its data already moved) go to the store, and the
sound cache reads them from there. A sound is pitch ranges (block at 152; 72 bytes each,
their permutations block at 60), each permutation (124 bytes) has a tag_data of samples
at 64 (size, pad, file offset at 72) and the sound cache's fields at 44 (block index),
48 (address), 52 (the sound's tag index) and 60 (the same, sound/sound_definitions.h,
cache/xbox_sound_cache.c) */
static void import_sound(
	struct import_donor *donor,
	byte *sound,
	long new_tag_index)
{
	unsigned long const *ranges = (unsigned long const *)(sound + 152);
	long range_count = (long)ranges[0];
	long range_index;

	for (range_index = 0; range_index < range_count; range_index++)
	{
		unsigned long const *permutations = (unsigned long const *)((byte *)ranges[1] + range_index * 72 + 60);
		long permutation_count = (long)permutations[0];
		long permutation_index;

		for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
		{
			long *permutation = (long *)((byte *)permutations[1] + permutation_index * 124);
			long offset = import_store_add(donor, permutation[18], permutation[16]);

			if (offset != NONE)
				permutation[18] = offset;
			permutation[11] = NONE;
			permutation[12] = 0;
			permutation[13] = new_tag_index;
			permutation[15] = new_tag_index;
		}
	}

	return;
}

/* brings the requested tags of one donor map into the map */
static void import_from_donor(
	struct import_tag_header *header,
	struct import_instance **instances,
	char const *map_name,
	char const *donor_name,
	struct import_request const *requests,
	short request_count)
{
	struct import_donor donor;
	byte *mark = NULL;
	long index;
	long added = 0;
	long map_count = header->tag_count;
	struct import_instance *table;
	short request_index;

	if (!import_donor_open(&donor, donor_name))
	{
		import_donor_close(&donor);
		return;
	}

	/* the donor's tag data, copied where Direct3D can reach it */
	donor.blob_size = (unsigned long)donor.header.tag_data_size;
	donor.blob = (byte *)XPhysicalAlloc(donor.blob_size, (unsigned long)-1, 0, PAGE_READWRITE);
	if (!donor.blob || tag_import_globals.blob_count >= IMPORT_MAXIMUM_DONORS ||
		!import_donor_read(&donor, donor.header.tag_data_offset, donor.header.tag_data_size, donor.blob))
	{
		platform_log("import: cannot read the tags of %s", donor_name);
		if (donor.blob)
			XPhysicalFree(donor.blob);
		import_donor_close(&donor);
		return;
	}
	tag_import_globals.blobs[tag_import_globals.blob_count++] = donor.blob;
	donor.tag_header = (struct import_tag_header *)donor.blob;
	if (donor.tag_header->signature != 0x74616773)
	{
		platform_log("import: %s has no tag header", donor_name);
		import_donor_close(&donor);
		return;
	}
	donor.base = (unsigned long)donor.tag_header->tag_instances - IMPORT_TAG_HEADER_SIZE;
	donor.delta = (long)((unsigned long)donor.blob - donor.base);
	donor.tag_count = donor.tag_header->tag_count;
	donor.instances = (struct import_instance *)(donor.blob + ((unsigned long)donor.tag_header->tag_instances - donor.base));
	mark = (byte *)calloc(donor.tag_count, 1);
	if (!mark || !import_donor_extents(&donor))
	{
		free(mark);
		import_donor_close(&donor);
		return;
	}

	/* what was asked for, and everything it refers to */
	for (request_index = 0; request_index < request_count; request_index++)
	{
		long found = NONE;

		for (index = 0; index < donor.tag_count; index++)
		{
			if (donor.instances[index].group_tag == (long)requests[request_index].group_tag &&
				!_stricmp(import_donor_string(&donor, donor.instances[index].name), requests[request_index].name))
			{
				found = index;
				break;
			}
		}
		if (found == NONE)
			platform_log("import: %s has no tag %s", donor_name, requests[request_index].name);
		else
			import_mark_closure(&donor, found, mark);
	}

	/* which the map has already (by group and name), and which are new */
	for (index = 0; index < donor.tag_count; index++)
	{
		long map_index;

		if (!mark[index])
			continue;
		for (map_index = 0; map_index < map_count; map_index++)
		{
			if ((*instances)[map_index].group_tag == donor.instances[index].group_tag &&
				!_stricmp((*instances)[map_index].name, import_donor_string(&donor, donor.instances[index].name)))
			{
				donor.map_index[index] = map_index;
				break;
			}
		}
		if (donor.map_index[index] == NONE)
			donor.map_index[index] = map_count + added++;
	}

	/* a longer instance table, the added tags at its end */
	table = (struct import_instance *)calloc(map_count + added, sizeof(struct import_instance));
	if (!table)
	{
		free(mark);
		import_donor_close(&donor);
		return;
	}
	memcpy(table, *instances, sizeof(struct import_instance) * map_count);
	for (index = 0; index < donor.tag_count; index++)
	{
		struct import_instance *instance;
		unsigned long address;
		unsigned long end;
		unsigned long *words;
		unsigned long offset;
		struct import_range ranges[IMPORT_MAXIMUM_RANGES];
		long range_count = 0;
		long new_index = donor.map_index[index];

		if (!mark[index] || new_index < map_count)
			continue;

		instance = &table[new_index];
		*instance = donor.instances[index];
		instance->tag_index = import_salted_index(new_index);
		instance->name = (char *)((unsigned long)donor.instances[index].name + donor.delta);
		address = (unsigned long)donor.instances[index].base_address;
		instance->base_address = (void *)(address + donor.delta);

		/* every pointer moves, every reference names the tag in this map */
		end = donor.extent_end[index];
		words = (unsigned long *)(donor.blob + (address - donor.base));
		if (instance->group_tag == 'antr')
			range_count = import_animation_ranges(&donor, address, ranges, IMPORT_MAXIMUM_RANGES);
		for (offset = 0; offset + 4 <= end - address; offset += 4)
		{
			unsigned long *word = &words[offset / 4];

			if (range_count > 0)
			{
				long range_index;

				for (range_index = 0; range_index < range_count; range_index++)
				{
					if (address + offset >= ranges[range_index].start && address + offset < ranges[range_index].end)
						break;
				}
				if (range_index < range_count)
					continue;
			}
			if (offset + 16 <= end - address)
			{
				long target = import_reference_target(&donor, word);

				if (target != NONE)
				{
					word[1] += donor.delta;
					word[3] = (unsigned long)import_salted_index(donor.map_index[target]);
					offset += 12;
					continue;
				}
			}
			if (*word >= donor.base && *word < donor.base + donor.blob_size)
				*word += donor.delta;
		}
		if (instance->group_tag == 'bitm')
			import_bitmap_group(&donor, (byte *)words, instance->tag_index);
		else if (instance->group_tag == 'snd!')
			import_sound(&donor, (byte *)words, instance->tag_index);
	}

	/* the donor's vertex and index buffers, relocated and registered as the game does its own */
	{
		unsigned long *vertex_buffers = (unsigned long *)((unsigned long)donor.tag_header->vertex_buffers + donor.delta);
		unsigned long *index_buffers = (unsigned long *)((unsigned long)donor.tag_header->index_buffers + donor.delta);

		for (index = 0; index < donor.tag_header->vertex_buffer_count; index++)
		{
			vertex_buffers[index * 3 + 1] += donor.delta;
			vertex_buffers[index * 3] = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
			IDirect3DVertexBuffer8_Register((D3DVertexBuffer *)&vertex_buffers[index * 3], NULL);
		}
		for (index = 0; index < donor.tag_header->index_buffer_count; index++)
		{
			index_buffers[index * 3 + 1] += donor.delta;
			index_buffers[index * 3] = D3DCOMMON_TYPE_INDEXBUFFER | 1;
		}
	}

	if (added > 0)
	{
		free(tag_import_globals.instances);
		tag_import_globals.instances = table;
		*instances = table;
		header->tag_instances = table;
		header->tag_count = map_count + added;
	}
	else
	{
		free(table);
	}
	{
		long needed = 0;

		for (index = 0; index < donor.tag_count; index++)
			needed += mark[index];
		platform_log("import: %ld tags from %s added to %s (%ld are needed, the map has the rest)",
			added, donor_name, map_name, needed);
	}
	free(mark);
	import_donor_close(&donor);

	return;
}

/* ---------- public code */

void halo_tag_import(
	void *tag_header,
	void **instances,
	char const *map_name)
{
	char const *setting = config_string("game.import");
	struct import_request requests[IMPORT_MAXIMUM_REQUESTS];
	short request_count;
	short donor_index;
	char done[IMPORT_MAXIMUM_DONORS][64];
	short done_count = 0;

	if (!*setting || !import_map_listed(config_string("game.import_into"), map_name))
		return;
	request_count = import_parse_requests(setting, requests);
	for (donor_index = 0; donor_index < request_count; donor_index++)
	{
		struct import_request donor_requests[IMPORT_MAXIMUM_REQUESTS];
		short count = 0;
		short other;
		boolean seen = FALSE;

		for (other = 0; other < done_count; other++)
			seen = seen || !_stricmp(done[other], requests[donor_index].donor);
		if (seen || done_count >= IMPORT_MAXIMUM_DONORS || !_stricmp(requests[donor_index].donor, map_name))
			continue;
		strcpy(done[done_count++], requests[donor_index].donor);
		for (other = donor_index; other < request_count; other++)
		{
			if (!_stricmp(requests[other].donor, requests[donor_index].donor))
				donor_requests[count++] = requests[other];
		}
		import_from_donor((struct import_tag_header *)tag_header, (struct import_instance **)instances,
			map_name, requests[donor_index].donor, donor_requests, count);
	}

	return;
}

/* what the added tags took, given back with the map */
void halo_tag_import_release(
	void)
{
	short index;

	for (index = 0; index < tag_import_globals.blob_count; index++)
		XPhysicalFree(tag_import_globals.blobs[index]);
	free(tag_import_globals.instances);
	free(tag_import_globals.pixels);
	memset(&tag_import_globals, 0, sizeof(tag_import_globals));

	return;
}

/* cache_file_read for an offset of an added bitmap's pixels: TRUE when it was read */
boolean halo_tag_import_read(
	long offset,
	long size,
	void *buffer)
{
	long start = offset - HALO_IMPORT_PIXEL_BASE;

	if (offset < HALO_IMPORT_PIXEL_BASE)
		return FALSE;
	if (start >= 0 && tag_import_globals.pixels && start + size <= tag_import_globals.pixel_size)
		memcpy(buffer, tag_import_globals.pixels + start, size);
	else
		memset(buffer, 0, size);

	return TRUE;
}
