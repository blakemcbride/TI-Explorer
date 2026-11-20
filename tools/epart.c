/*
 * explorer 1 disk image manipulator
 * Brad Parker <brad@heeltoe.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>

#define E_BLKSIZE	1024
u_char block[E_BLKSIZE];
int fd;
char *filename;
int mark_expt_flag;
int show_flag;
int extract_flag;
int active_flag;
int deactive_flag;
int debug_flag;

typedef unsigned int uint32;

#if 0
/* virtual format */
struct {
	uint32 rev;
	uint32 flags;
	char dev[32];
	int bytes_per_block;
	int bytes_per_sector;
	int sectors_per_track;
	int reads;
	int cyls;
	int reserved_sectors;
};
#endif

#define E_PART_MAX	32
struct {
	char name[5];
	int start_block;
	int length_blocks;
	u_char attributes[4];
	char comment[64];
} part_table[E_PART_MAX];
int part_table_size;

/* raw, actual-bytes format */
struct raw_volume_label_s {
	u_char vl_labl[4];
	u_char vl_rev[4];
	u_char vl_res1[8];

	u_char vl_flagword;
	u_char vl_res2[3];
	u_char vl_devname[12];

	u_char vl_bytes_per_block[2];
#if 1
	u_char vl_bytes_per_sector[2];
	u_char vl_res3[2];
	u_char vl_sectors_per_track;
	u_char vl_heads;
	u_char vl_cyls[2];
	u_char vl_reserved_sectors[2];
	u_char vl_res4[4];
#endif
#if 0
	u_char vl_bytes_per_phys_block[2];
	u_char vl_res5[2];
	u_char vl_tracks[2];
	u_char vl_blocks[4];
	u_char vl_res6[4];
#endif
	u_char vl_volname[16];
	u_char vl_res7[16];

	u_char vl_ptable_name[4];
	u_char vl_ptable_start_block[4];
	u_char vl_ptable_length_blocks[4];
	u_char vl_res8[4];
	u_char vl_save_name[4];
	u_char vl_save_start_block[4];
	u_char vl_save_length_blocks[4];
	u_char vl_res9[112];
	u_char vl_comment[768];
};

struct part_table_s {
	u_char pt_name[4];
	u_char pt_rev[4];
	u_char pt_num_entries[4];
	u_char pt_size_entry[4];
	u_char pt_offset[4];
	u_char pt_res[44];
};

struct part_table_entry_s {
	u_char pte_name[4];
	u_char pte_start_block[4];
	u_char pte_length_blocks[4];
	u_char pte_attributes[4];
	u_char pte_comment[48];
};

char *part_gen_func_type[] = {
	/* 00 */ "load band",
	/* 01 */ "microload band",
	/* 02 */ "page band",
	/* 03 */ "file band",
	/* 04 */ "meter band",
	/* 05 */ "test zone band",
	/* 06 */ "format parameter band",
	/* 07 */ "volume label",
	/* 08 */ "save band",
	/* 09 */ "partition band",
	/* 0A */ "configuration band",
	/* 0B */ "user defined type #1",
	/* 0C */ "user defined type #2",
	/* 0D */ "user defined type #3",
	/* 0E */ "user defined type #4",
	/* 0F */ "user defined type #5",
	/* 10 */ "user defined type #6",
	/* 11 */ "user defined type #7",
	/* 12 */ "user defined type #8",
	/* 13 */ "user defined type #9",
	/* 14 */ "user defined type #10",
	"reserved"
};


/*
Volume Label
Partition Table
Save Partition
Test Zone
Format Information partition
*/

int
open_file(char *filename, int need_rw)
{
	if (debug_flag) printf("open '%s'\n", filename);
	fd = open(filename, need_rw ? O_RDWR : O_RDONLY);
	if (fd < 0) {
		perror(filename);
		return -1;
	}

	return 0;
}

int
read_block(int blknum)
{
	int ret;
	off_t offset = blknum * 1024;

	ret = lseek(fd, offset, SEEK_SET);
	if (ret < 0)
		return 1;
	
	ret = read(fd, block, E_BLKSIZE);
	if (ret != E_BLKSIZE)
		return -1;

	return 0;
}

int
write_block(int blknum)
{
	int ret;
	off_t offset = blknum * 1024;

	ret = lseek(fd, offset, SEEK_SET);
	if (ret < 0)
		return 1;
	
	ret = write(fd, block, E_BLKSIZE);
	if (ret != E_BLKSIZE)
		return -1;

	return 0;
}

unsigned int
int4(u_char *p)
{
	return (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | (p[0]);
}

unsigned int
int2(u_char *p)
{
	return (p[1] << 8) | (p[0]);
}

int ptblknum;

int
read_part_table(char *filename)
{
	struct raw_volume_label_s *vl;
	struct part_table_s *pt;
	struct part_table_entry_s *pe;
	int i, count, size_in_words, index;
	int rev, bytes_per_block;

	if (read_block(0))
		return -1;

	vl = (struct raw_volume_label_s *)block;
	if (vl->vl_labl[0] == 'D' && vl->vl_labl[1] == 'A' &&
	    vl->vl_labl[2] == 'T' && vl->vl_labl[3] == 'A')
	{
		printf("%s: DATA disk, no LABL partition\n", filename);
		return -1;
	}

	if (vl->vl_labl[0] != 'L' || vl->vl_labl[1] != 'A' ||
	    vl->vl_labl[2] != 'B' || vl->vl_labl[3] != 'L')
	{
		printf("%s: missing LABL signature\n", filename);

		if (debug_flag) printf("found %c%c%c%c\n",
				       vl->vl_labl[0], vl->vl_labl[1], 
				       vl->vl_labl[2], vl->vl_labl[3]);
		return -1;
	}

	rev = int4(vl->vl_rev);
	printf("rev: %08x\n", rev);
	printf("devname: '%s'\n", vl->vl_devname);

	printf("volname: '%s'\n", vl->vl_volname);

	printf("ptable_name: '%c%c%c%c'\n",
	       vl->vl_ptable_name[0],
	       vl->vl_ptable_name[1],
	       vl->vl_ptable_name[2],
	       vl->vl_ptable_name[3]);

	ptblknum = int4(vl->vl_ptable_start_block);
	printf("ptblknum: %d\n", ptblknum);

	bytes_per_block = int2(vl->vl_bytes_per_block);
	printf("bytes/block: %d\n", bytes_per_block);

	printf("comment: '%s'\n", vl->vl_comment);
	
	if (read_block(ptblknum))
		return -1;

	pt = (struct part_table_s *)block;
	printf("partition table: '%c%c%c%c'\n",
	       pt->pt_name[0], pt->pt_name[1], 
	       pt->pt_name[2], pt->pt_name[3]);

	if (pt->pt_name[0] != 'P' || pt->pt_name[1] != 'R' ||
	    pt->pt_name[2] != 'T' || pt->pt_name[3] != 'N')
	{
		return -1;
	}

	count = int4(pt->pt_num_entries);
	size_in_words = int4(pt->pt_size_entry);
	index = 0;

	printf("ptable count: %d\n", count);
	printf("size (words): %d\n", size_in_words);

	part_table_size = count;

	pe = (struct part_table_entry_s *)(pt+1);
	for (i = 0; i < count; i++) {
		int user_type, property;

		memcpy(part_table[index].name,
		       pe->pte_name, 4);
		part_table[index].name[4] = 0;

		part_table[index].start_block = int4(pe->pte_start_block);
		part_table[index].length_blocks = int4(pe->pte_length_blocks);
		memcpy(part_table[index].attributes,
		       pe->pte_attributes, 4);

		memset(part_table[index].comment, 0,
		       sizeof(part_table[index].comment));

		memcpy(part_table[index].comment, 
		       pe->pte_comment, sizeof(pe->pte_comment));

		printf("%-4s %6d/%6d %02x %02x %02x %02x '%s' ",
		       part_table[index].name,
		       part_table[index].start_block,
		       part_table[index].length_blocks,
		       part_table[index].attributes[0],
		       part_table[index].attributes[1],
		       part_table[index].attributes[2],
		       part_table[index].attributes[3],
		       part_table[index].comment);

		if (part_table[index].attributes[0] < 15) {
			int gen_func = part_table[index].attributes[0];
			printf("%s ", part_gen_func_type[gen_func]);
		}

		user_type = int2(&part_table[index].attributes[1]);
		if (user_type)
			printf("user %04x ", user_type);

		property = part_table[index].attributes[3];
#if 0
		if (property & 0x80) printf("expandable ");
		if (property & 0x40) printf("contractable ");
		if (property & 0x20) printf("delete-protected ");
		if (property & 0x10) printf("logical-partition ");
		if (property & 0x08) printf("copy-protected ");
		if (property & 0x04) printf("default-indicator ");
		if (property & 0x02) printf("diagnostic-use ");
		if (property & 0x01) printf("reserved ");
#else
		if (property) {
		printf("[");
		if (property & 0x80) printf("E");
		if (property & 0x40) printf("C");
		if (property & 0x20) printf("D");
		if (property & 0x10) printf("L");
		if (property & 0x08) printf("P");
		if (property & 0x04) printf("*");
		if (property & 0x02) printf("#");
		if (property & 0x01) printf("R");
		printf("]");
		}
#endif
		printf("\n");
		index++;
		pe = (struct part_table_entry_s *)
			(((uint32 *)pe) + size_in_words);
	}

	return 0;
}

int
show_part_table(void)
{
	return 0;
}

int
extract_part(char *partition, char *ext_filename)
{
	int i, index, blocks, fd2, bpblk, ret;
	off_t offset;

	index = -1;
	for (i = 0; i < part_table_size; i++) {
		if (strcmp(partition, part_table[i].name) == 0) {
			index = i;
			break;
		}
	}

	if (index < 0) {
		printf("can't find partition '%s'\n", partition);
		return -1;
	}

	bpblk = 1024;

	offset = part_table[i].start_block * bpblk;
	lseek(fd, offset, SEEK_SET);

	blocks = part_table[i].length_blocks;

	unlink(ext_filename);
	fd2 = open(ext_filename, O_WRONLY | O_CREAT, 0666);
	if (fd2 == 0) {
		perror(ext_filename);
		return -1;
	}

	for (i = 0; i < blocks; i++) {
		ret = read(fd, block, bpblk);
		if (ret != bpblk) {
			printf("short read?\n");
			break;
		}

		write(fd2, block, bpblk);
	}

	close(fd2);

	return 0;
}

int
active_part(char *partition, int onoff)
{
	struct part_table_s *pt;
	struct part_table_entry_s *pe;
	int i, count, size_in_words;

	if (read_block(ptblknum))
		return -1;

	printf("\nset/clean active\n");

	pt = (struct part_table_s *)block;
	pe = (struct part_table_entry_s *)(pt+1);

	count = int4(pt->pt_num_entries);
	size_in_words = int4(pt->pt_size_entry);

	for (i = 0; i < count; i++) {
		if (strcmp(part_table[i].name, partition) == 0) {

			if (onoff) {
				pe->pte_attributes[3] |= 0x04;
				printf("setting %s\n", part_table[i].name);
			} else {
				pe->pte_attributes[3] &= ~0x04;
				printf("clearing %s\n", part_table[i].name);
			}
		}
			
		pe = (struct part_table_entry_s *)
			(((uint32 *)pe) + size_in_words);
	}

	if (write_block(ptblknum))
		return -1;

	return 0;
}

int
mark_expt(void)
{
	struct part_table_s *pt;
	struct part_table_entry_s *pe;
	int i, count, size_in_words;

	if (read_block(ptblknum))
		return -1;

	printf("\ntrying to mark expts\n");

	pt = (struct part_table_s *)block;
	pe = (struct part_table_entry_s *)(pt+1);

	count = int4(pt->pt_num_entries);
	size_in_words = int4(pt->pt_size_entry);

	for (i = 0; i < count; i++) {
		if (strcmp(part_table[i].name, "EXPT") == 0) {

			if ((pe->pte_attributes[3] & 0x02) == 0) {
				pe->pte_attributes[3] |= 0x02;
				printf("marking %s\n", part_table[i].name);
			}
		}
			
		pe = (struct part_table_entry_s *)
			(((uint32 *)pe) + size_in_words);
	}

	if (write_block(ptblknum))
		return -1;

	return 0;
}

void
usage(void)
{
	fprintf(stderr, "usage:\n");
	fprintf(stderr, "-d		debug\n");
	fprintf(stderr, "-e <filename>	extract partition\n");
	fprintf(stderr, "-A <partname>  set active flag\n");
	fprintf(stderr, "-D <partname>  clear active flag\n");
	fprintf(stderr, "-m <partname>	mark expt parition\n");
	exit(1);
}


extern char *optarg;
extern int optind;

int
main(int argc, char *argv[])
{
	int c, need_write;
	char *partition;

	filename = "c0-d0-hacked.dsk";

	show_flag = 1;

	while ((c = getopt(argc, argv, "de:mA:D:")) != -1) {
		switch (c) {
		case 'd':
			debug_flag++;
			break;
		case 'e':
			extract_flag++;
			partition = strdup(optarg);
			break;
		case 'A':
			active_flag++;
			partition = strdup(optarg);
			break;
		case 'D':
			deactive_flag++;
			partition = strdup(optarg);
			break;
		case 'm':
			mark_expt_flag++;
			break;
		default:
			usage();
		}
	}

	if (argc > optind)
		filename = argv[optind];

	need_write = mark_expt_flag || active_flag || deactive_flag;

	if (open_file(filename, need_write) ||
	    read_part_table(filename))
	{
		exit(1);
	}

	if (show_flag) {
		show_part_table();
	}

	if (mark_expt_flag) {
		mark_expt();
	}

	if (deactive_flag) {
		active_part(partition, 0);
	}

	if (active_flag) {
		active_part(partition, 1);
	}

	if (extract_flag) {
		extract_part(partition, "ext.lod");
	}

	exit(0);
}
