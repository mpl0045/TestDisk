/*

    File: ntfs_adv.c

    Copyright (C) 1998-2007 Christophe GRENIER <grenier@cgsecurity.org>
  
    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
  
    You should have received a copy of the GNU General Public License along
    with this program; if not, write the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
 
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <ctype.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include "types.h"
#include "common.h"
#include "intrf.h"
#include "intrfn.h"
#include "dirpart.h"
#include "ntfs.h"
#include "fnctdsk.h"
#include "lang.h"
#include "io_redir.h"
#include "log.h"

#define INTER_NTFS_X 0
#define INTER_NTFS_Y 23

#define MAX_INFO_MFT 10
#define NTFS_SECTOR_SIZE 0x200

typedef struct s_info_mft info_mft_t;
struct s_info_mft
{
  uint64_t sector;
  uint64_t mft_lcn;
  uint64_t mftmirr_lcn;
};

static int create_ntfs_boot_sector(disk_t *disk_car, partition_t *partition, const int interface, const unsigned int cluster_size, const uint64_t mft_lcn, const uint64_t mftmirr_lcn, const uint32_t mft_record_size, const uint32_t index_block_size, char**current_cmd);
static int ncurses_ntfs2_info(const struct ntfs_boot_sector *nh1, const struct ntfs_boot_sector *nh2);
static int ncurses_ntfs_info(const struct ntfs_boot_sector *ntfs_header);
static int testdisk_ffs(int x);
static int read_mft_info(disk_t *disk_car, partition_t *partition, const uint64_t mft_sector, const int verbose, unsigned int *sectors_per_cluster, uint64_t *mft_lcn, uint64_t *mftmirr_lcn, unsigned int *mft_record_size);

#ifdef HAVE_NCURSES
static void ntfs_dump_ncurses(disk_t *disk_car, const partition_t *partition, const unsigned char *orgboot, const unsigned char *newboot)
{
  WINDOW *window=newwin(0,0,0,0);	/* full screen */
  keypad(window, TRUE); /* Need it to get arrow key */
  aff_copy(window);
  wmove(window,4,0);
  wprintw(window,"%s",disk_car->description(disk_car));
  wmove(window,5,0);
  aff_part(window,AFF_PART_ORDER|AFF_PART_STATUS,disk_car,partition);
  mvwaddstr(window,6,0, "     Rebuild Boot sector           Boot sector");
  dump2(window, newboot, orgboot, NTFS_SECTOR_SIZE);
  delwin(window);
  (void) clearok(stdscr, TRUE);
#ifdef HAVE_TOUCHWIN
  touchwin(stdscr);
#endif
} 
#endif

static void ntfs_dump(disk_t *disk_car, const partition_t *partition, const unsigned char *orgboot, const unsigned char *newboot)
{
  log_info("     Rebuild Boot sector           Boot sector\n");
  dump2_log(newboot, orgboot, NTFS_SECTOR_SIZE);
#ifdef HAVE_NCURSES
  ntfs_dump_ncurses(disk_car, partition, orgboot, newboot);
#endif
}

static void menu_write_ntfs_boot_sector_cli(disk_t *disk_car, partition_t *partition, const unsigned char *orgboot, const unsigned char *newboot, const int error, char **current_cmd)
{
  const struct ntfs_boot_sector *org_ntfs_header=(const struct ntfs_boot_sector *)orgboot;
  const struct ntfs_boot_sector *ntfs_header=(const struct ntfs_boot_sector *)newboot;
  int no_confirm=0;
  while(1)
  {
    if(memcmp(newboot,orgboot,NTFS_SECTOR_SIZE)!=0)
    {
      log_ntfs2_info(ntfs_header, org_ntfs_header);
      if(error)
	log_error("Warning: Extrapolated boot sector have incorrect values.\n");
    }
    else
    {
      log_ntfs_info(ntfs_header);
    }
    while(*current_cmd[0]==',')
      (*current_cmd)++;
    if(strncmp(*current_cmd,"list",4)==0)
    {
      (*current_cmd)+=4;
      io_redir_add_redir(disk_car,partition->part_offset,NTFS_SECTOR_SIZE,0,newboot);
      dir_partition(disk_car, partition, 0, current_cmd);
      io_redir_del_redir(disk_car,partition->part_offset);
    }
    else if(strncmp(*current_cmd,"dump",4)==0)
    {
      (*current_cmd)+=4;
      ntfs_dump(disk_car, partition, orgboot, newboot);
    }
    else if(strncmp(*current_cmd,"noconfirm,",10)==0)
    {
      (*current_cmd)+=10;
      no_confirm=1;
    }
    else if(strncmp(*current_cmd,"write",5)==0)
    {
      (*current_cmd)+=5;
      if(no_confirm!=0 || ask_confirmation("Write new NTFS boot sector, confirm ? (Y/N)")!=0)
      {
	log_info("Write new boot!\n");
	/* Write boot sector and backup boot sector */
	if(disk_car->write(disk_car,NTFS_SECTOR_SIZE, newboot, partition->part_offset))
	{
	  display_message("Write error: Can't write new NTFS boot sector\n");
	}
	if(disk_car->write(disk_car,NTFS_SECTOR_SIZE, newboot, partition->part_offset+partition->part_size-disk_car->sector_size)!=0)
	{
	  display_message("Write error: Can't write new NTFS backup boot sector\n");
	}
        disk_car->sync(disk_car);
      }
      return ;
    }
    else
    {
      log_info("Don't write new NTFS boot sector and backup boot sector!\n");
      return;
    }
  }
}

#ifdef HAVE_NCURSES
static void menu_write_ntfs_boot_sector_ncurses(disk_t *disk_car, partition_t *partition, const unsigned char *orgboot, const unsigned char *newboot, const int error, char **current_cmd)
{
  const struct ntfs_boot_sector *org_ntfs_header=(const struct ntfs_boot_sector *)orgboot;
  const struct ntfs_boot_sector *ntfs_header=(const struct ntfs_boot_sector *)newboot;
  struct MenuItem menuSaveBoot[]=
  {
    { 'D', "Dump", "Dump sector" },
    { 'L', "List", "List directories and files" },
    { 'W', "Write","Write boot"},
    { 'Q',"Quit","Quit this section"},
    { 0, NULL, NULL }
  };
  const char *options="DLQ";
  int command;
  while(1)
  {
    aff_copy(stdscr);
    wmove(stdscr,4,0);
    wprintw(stdscr,"%s",disk_car->description(disk_car));
    mvwaddstr(stdscr,5,0,msg_PART_HEADER_LONG);
    wmove(stdscr,6,0);
    aff_part(stdscr,AFF_PART_ORDER|AFF_PART_STATUS,disk_car,partition);
    wmove(stdscr,8,0);
    if(memcmp(newboot,orgboot,NTFS_SECTOR_SIZE))
    {
      options="DLWQ";
      ncurses_ntfs2_info(ntfs_header, org_ntfs_header);
      wprintw(stdscr,"Extrapolated boot sector and current boot sector are different.\n");
      log_ntfs2_info(ntfs_header, org_ntfs_header);
      if(error)
	log_error("Warning: Extrapolated boot sector have incorrect values.\n");
    }
    else
    {
      log_ntfs_info(ntfs_header);
      ncurses_ntfs_info(ntfs_header);
      wprintw(stdscr,"Extrapolated boot sector and current boot sector are identical.\n");
    }
    command=wmenuSelect(stdscr,INTER_DUMP_Y, INTER_DUMP_X, menuSaveBoot,8,options,MENU_HORIZ | MENU_BUTTON, 1);
    switch(command)
    {
      case 'w':
      case 'W':
	if(strchr(options,'W')!=NULL && ask_confirmation("Write new NTFS boot sector, confirm ? (Y/N)")!=0)
	{
	  log_info("Write new boot!\n");
	  /* Write boot sector and backup boot sector */
	  if(disk_car->write(disk_car,NTFS_SECTOR_SIZE, newboot, partition->part_offset))
	  {
	    display_message("Write error: Can't write new NTFS boot sector\n");
	  }
	  if(disk_car->write(disk_car,NTFS_SECTOR_SIZE, newboot, partition->part_offset+partition->part_size-disk_car->sector_size)!=0)
	  {
	    display_message("Write error: Can't write new NTFS backup boot sector\n");
	  }
          disk_car->sync(disk_car);
	}
	return;
      case 'd':
      case 'D':
	if(strchr(options,'D')!=NULL)
	{
	  WINDOW *window=newwin(0,0,0,0);	/* full screen */
	  keypad(window, TRUE); /* Need it to get arrow key */
	  aff_copy(window);
	  wmove(window,4,0);
	  wprintw(window,"%s",disk_car->description(disk_car));
	  wmove(window,5,0);
	  aff_part(window,AFF_PART_ORDER|AFF_PART_STATUS,disk_car,partition);
	  log_info("     Rebuild Boot sector           Boot sector\n");
	  mvwaddstr(window,6,0, "     Rebuild Boot sector           Boot sector");
	  dump2(window, newboot, orgboot, NTFS_SECTOR_SIZE);
	  dump2_log(newboot, orgboot, NTFS_SECTOR_SIZE);
	  delwin(window);
	  (void) clearok(stdscr, TRUE);
#ifdef HAVE_TOUCHWIN
	  touchwin(stdscr);
#endif
	}
	break;
      case 'l':
      case 'L':
	io_redir_add_redir(disk_car,partition->part_offset,NTFS_SECTOR_SIZE,0,newboot);
	dir_partition(disk_car, partition, 0, current_cmd);
	io_redir_del_redir(disk_car,partition->part_offset);
	break;
      case 'q':
      case 'Q':
	return;
    }
  }
}
#endif

static void menu_write_ntfs_boot_sector(disk_t *disk_car, partition_t *partition, const unsigned char *orgboot, const unsigned char *newboot, const int error, char **current_cmd)
{
  if(*current_cmd!=NULL)
  {
    menu_write_ntfs_boot_sector_cli(disk_car, partition, orgboot, newboot, error, current_cmd);
    return;
  }
#ifdef HAVE_NCURSES
  menu_write_ntfs_boot_sector_ncurses(disk_car, partition, orgboot, newboot, error, current_cmd);
#endif
}

static int create_ntfs_boot_sector(disk_t *disk_car, partition_t *partition, const int interface, const unsigned int cluster_size, const uint64_t mft_lcn, const uint64_t mftmirr_lcn, const uint32_t mft_record_size, const uint32_t index_block_size, char**current_cmd)
{
  unsigned char orgboot[NTFS_SECTOR_SIZE];
  unsigned char newboot[NTFS_SECTOR_SIZE];
  struct ntfs_boot_sector *org_ntfs_header=(struct ntfs_boot_sector *)&orgboot;
  struct ntfs_boot_sector *ntfs_header=(struct ntfs_boot_sector *)&newboot;
  int error=0;
  if(disk_car->read(disk_car,NTFS_SECTOR_SIZE, &orgboot, partition->part_offset)!=0)
  {
    log_error("create_ntfs_boot_sector: Can't read boot sector.\n");
    memset(&orgboot,0,NTFS_SECTOR_SIZE);
  }
  if(cluster_size==0)
  {
    error=1;
  }
  if(error)
  {
    display_message("NTFS Bad extrapolation.\n");
    return 1;
  }
  memcpy(&newboot,&orgboot,NTFS_SECTOR_SIZE);
  memcpy(ntfs_header->system_id,"NTFS    ",8);
  ntfs_header->sector_size[0]=disk_car->sector_size & 0xFF;
  ntfs_header->sector_size[1]=disk_car->sector_size>>8;
  ntfs_header->sectors_per_cluster=cluster_size/disk_car->sector_size;
  ntfs_header->reserved=le16(0);
  ntfs_header->fats=0;
  ntfs_header->dir_entries[0]=0;
  ntfs_header->dir_entries[1]=0;
  ntfs_header->sectors[0]=0;
  ntfs_header->sectors[1]=0;
  ntfs_header->media=0xF8;
  ntfs_header->fat_length=le16(0);
  ntfs_header->secs_track=le16(disk_car->CHS.sector);
  ntfs_header->heads=le16(disk_car->CHS.head+1);
  /* absolute sector address from the beginning of the disk (!= FAT) */
  ntfs_header->hidden=le32(partition->part_offset/disk_car->sector_size);
  ntfs_header->total_sect=le32(0);
  ntfs_header->sectors_nbr=le64(partition->part_size/disk_car->sector_size-1);
  ntfs_header->mft_lcn=le64(mft_lcn);
  ntfs_header->mftmirr_lcn=le64(mftmirr_lcn);
  ntfs_header->clusters_per_mft_record=(mft_record_size >= cluster_size?mft_record_size / cluster_size:
      -(testdisk_ffs(mft_record_size) - 1));
  ntfs_header->clusters_per_index_record =(index_block_size >= cluster_size?index_block_size / cluster_size:
      -(testdisk_ffs(index_block_size) - 1));
  ntfs_header->reserved0[0]=0;
  ntfs_header->reserved0[1]=0;
  ntfs_header->reserved0[2]=0;
  ntfs_header->reserved1[0]=0;
  ntfs_header->reserved1[1]=0;
  ntfs_header->reserved1[2]=0;
  /*
  {
    uint32_t *u;
    uint32_t checksum;
    for (checksum = 0,u=(uint32_t*)ntfs_header; u < (uint32_t*)(&ntfs_header->checksum); u++)
      checksum += NTFS_GETU32(u);
    ntfs_header->checksum=le32(checksum);
  }
  */
  ntfs_header->checksum=le32(0);
  ntfs_header->marker=le16(0xAA55);
  if(memcmp(newboot,orgboot,NTFS_SECTOR_SIZE))
  {
    log_warning("             New / Current boot sector\n");
    log_ntfs2_info(ntfs_header,org_ntfs_header);
    log_warning("Extrapolated boot sector and current boot sector are different.\n");
  }
  else
  {
    log_info("Extrapolated boot sector and current boot sector are identical.\n");
  }
  /* */
  if(interface)
    menu_write_ntfs_boot_sector(disk_car, partition, orgboot, newboot, error, current_cmd);
  else
    log_info("Don't write new NTFS boot sector and backup boot sector!\n");
  return 1;
}

static int read_mft_info(disk_t *disk_car, partition_t *partition, const uint64_t mft_sector, const int verbose, unsigned int *sectors_per_cluster, uint64_t *mft_lcn, uint64_t *mftmirr_lcn, unsigned int *mft_record_size)
{
  char buffer[8*DEFAULT_SECTOR_SIZE];
  const char *attr=buffer;
  if(disk_car->read(disk_car,sizeof(buffer), &buffer, partition->part_offset+(uint64_t)mft_sector*disk_car->sector_size)!=0)
  {
    display_message("NTFS: Can't read mft_sector\n");
    return 1;
  }
  *mft_lcn=ntfs_get_attr(attr,0x80,partition,buffer+8*DEFAULT_SECTOR_SIZE,verbose,0,NULL);
  *mft_record_size=NTFS_GETU32(attr + 0x1C);
  if(*mft_record_size==0)
  {
    if(verbose>0)
      log_warning("read_mft_info failed: mft_record_size=0\n");
    return 2;
  }
  attr+= NTFS_GETU32(attr + 0x1C);
  *mftmirr_lcn=ntfs_get_attr(attr,0x80,partition,buffer+8*DEFAULT_SECTOR_SIZE,verbose,0,NULL);
  /* Try to divide by the biggest number first */
  if(*mft_lcn<*mftmirr_lcn)
  {
    if(*mftmirr_lcn>0 && mft_sector%(*mftmirr_lcn)==0)
    {
      *sectors_per_cluster=mft_sector/(*mftmirr_lcn);
      switch(*sectors_per_cluster)
      {
	case 1: case 2: case 4: case 8: case 16: case 32: case 64: case 128:
	  return 0;
	default:
	  break;
      }
    }
    if(*mft_lcn>0 && mft_sector%(*mft_lcn)==0)
    {
      *sectors_per_cluster=mft_sector/(*mft_lcn);
      switch(*sectors_per_cluster)
      {
	case 1: case 2: case 4: case 8: case 16: case 32: case 64: case 128:
	  return 0;
	default:
	  break;
      }
    }
  }
  else
  {
    if(*mft_lcn>0 && mft_sector%(*mft_lcn)==0)
    {
      *sectors_per_cluster=mft_sector/(*mft_lcn);
      switch(*sectors_per_cluster)
      {
	case 1: case 2: case 4: case 8: case 16: case 32: case 64: case 128:
	  return 0;
	default:
	  break;
      }
    }
    if(*mftmirr_lcn>0 && mft_sector%(*mftmirr_lcn)==0)
    {
      *sectors_per_cluster=mft_sector/(*mftmirr_lcn);
      switch(*sectors_per_cluster)
      {
	case 1: case 2: case 4: case 8: case 16: case 32: case 64: case 128:
	  return 0;
	default:
	  break;
      }
    }
  }
  if(verbose>0)
  {
    log_warning("read_mft_info failed\n");
    log_warning("ntfs_find_mft: sectors_per_cluster invalid\n");
    log_warning("ntfs_find_mft: mft_lcn             %lu\n",(long unsigned int)*mft_lcn);
    log_warning("ntfs_find_mft: mftmirr_lcn         %lu\n",(long unsigned int)*mftmirr_lcn);
    log_warning("ntfs_find_mft: mft_record_size     %u\n",*mft_record_size);
    log_warning("\n");
  }
  *sectors_per_cluster=0;
  return 3;
}

int rebuild_NTFS_BS(disk_t *disk_car, partition_t *partition, const int verbose, const int dump_ind,const int interface, const unsigned int expert, char **current_cmd)
{
  uint64_t sector;
  char buffer[8*DEFAULT_SECTOR_SIZE];
  int ind_stop=0;
  unsigned int sectors_per_cluster=0;
  uint64_t mft_lcn;
  uint64_t mftmirr_lcn;
  unsigned int mft_record_size=1024;
  info_mft_t info_mft[MAX_INFO_MFT];
  unsigned int nbr_mft=0;
  log_info("rebuild_NTFS_BS\n");
#ifdef HAVE_NCURSES
  if(interface)
  {
    aff_copy(stdscr);
    wmove(stdscr,4,0);
    wprintw(stdscr,"%s",disk_car->description(disk_car));
    mvwaddstr(stdscr,5,0,msg_PART_HEADER_LONG);
    wmove(stdscr,6,0);
    aff_part(stdscr,AFF_PART_ORDER|AFF_PART_STATUS,disk_car,partition);
    wmove(stdscr,22,0);
    wattrset(stdscr, A_REVERSE);
    waddstr(stdscr,"  Stop  ");
    wattroff(stdscr, A_REVERSE);
  }
#endif
  /* try to find MFT Backup first */
  for(sector=(partition->part_size/disk_car->sector_size/2-20>0?partition->part_size/disk_car->sector_size/2-20:1);(sector<partition->part_size/disk_car->sector_size)&&(sector<=partition->part_size/disk_car->sector_size/2+20)&&(ind_stop==0);sector++)
  {
    if(disk_car->read(disk_car,2*DEFAULT_SECTOR_SIZE, &buffer, partition->part_offset+sector*(uint64_t)disk_car->sector_size)==0)
    {
      if(memcmp(buffer,"FILE",4)==0 && (NTFS_GETU16(buffer+ 0x14)%8==0) && (NTFS_GETU16(buffer+ 0x14)>=42)
	  &&(NTFS_GETU16(buffer+22)==1))	/* MFT_RECORD_IN_USE */
      {
	int res;
	res=ntfs_get_attr(buffer,0x30,partition,buffer+2*DEFAULT_SECTOR_SIZE,verbose,0,"$MFT");
	if(res==1)
	{
	  int tmp;
	  log_info("mft at %lu, seq=%u, main=%u res=%d\n",(long unsigned)sector,NTFS_GETU8(buffer+0x10),(unsigned int)NTFS_GETU32(buffer+0x20),res);
	  tmp=read_mft_info(disk_car, partition, sector, verbose, &sectors_per_cluster, &mft_lcn, &mftmirr_lcn, &mft_record_size);
	  if(tmp==0)
	  {
	    log_info("ntfs_find_mft: mft_lcn             %lu\n",(long unsigned int)mft_lcn);
	    log_info("ntfs_find_mft: mftmirr_lcn         %lu\n",(long unsigned int)mftmirr_lcn);
	    if(expert==0 || ask_confirmation("Use MFT from %lu, confirm ? (Y/N)",(long unsigned int)mft_lcn)!=0)
	      ind_stop=1;
	  }
	  else if(tmp==3)
	  {
	    if(nbr_mft<MAX_INFO_MFT)
	    {
	      info_mft[nbr_mft].sector=sector;
	      info_mft[nbr_mft].mft_lcn=mft_lcn;
	      info_mft[nbr_mft].mftmirr_lcn=mftmirr_lcn;
	      nbr_mft++;
	    }
	  }
	}
      }
    }
  }
  for(sector=1;(sector<partition->part_size/disk_car->sector_size)&&(ind_stop==0);sector++)
  {
#ifdef HAVE_NCURSES
    if((interface!=0) &&(sector&0xffff)==0)
    {
      wmove(stdscr,9,0);
      wclrtoeol(stdscr);
      wprintw(stdscr,"Search mft %10lu/%lu", (long unsigned)sector,
	  (long unsigned)(partition->part_size/disk_car->sector_size));
      wrefresh(stdscr);
      if(check_enter_key_or_s(stdscr))
      {
	log_info("Search mft stopped: %10lu/%lu\n", (long unsigned)sector,
	    (long unsigned)(partition->part_size/disk_car->sector_size));
	ind_stop=1;
      }
    }
#endif
    if(disk_car->read(disk_car,2*DEFAULT_SECTOR_SIZE, &buffer, partition->part_offset+sector*(uint64_t)disk_car->sector_size)==0)
    {
      if(memcmp(buffer,"FILE",4)==0 && (NTFS_GETU16(buffer+ 0x14)%8==0) && (NTFS_GETU16(buffer+ 0x14)>=42))
      {
	int res;
	res=ntfs_get_attr(buffer,0x30,partition,buffer+2*DEFAULT_SECTOR_SIZE,verbose,0,"$MFT");
	if(res==1)
	{
	  int tmp;
	  log_info("mft at %lu, seq=%u, main=%u res=%d\n",(long unsigned)sector,NTFS_GETU8(buffer+0x10),(unsigned int)NTFS_GETU32(buffer+0x20),res);
	  tmp=read_mft_info(disk_car, partition, sector, verbose, &sectors_per_cluster, &mft_lcn, &mftmirr_lcn, &mft_record_size);
	  if(tmp==0)
	  {
	    log_info("ntfs_find_mft: mft_lcn             %lu\n",(long unsigned int)mft_lcn);
	    log_info("ntfs_find_mft: mftmirr_lcn         %lu\n",(long unsigned int)mftmirr_lcn);
	    if(expert==0 || ask_confirmation("Use MFT from %lu, confirm ? (Y/N)",(long unsigned int)mft_lcn)!=0)
	      ind_stop=1;
	  }
	  else if(tmp==3)
	  {
	    if(nbr_mft<MAX_INFO_MFT)
	    {
	      info_mft[nbr_mft].sector=sector;
	      info_mft[nbr_mft].mft_lcn=mft_lcn;
	      info_mft[nbr_mft].mftmirr_lcn=mftmirr_lcn;
	      nbr_mft++;
	    }
	  }
	}
      }
    }
  }
  /* Find partition location using MFT information */
  {
    unsigned int i,j;
    int find_partition=0;
    uint64_t tmp=partition->part_offset;
    for(i=0;i<nbr_mft;i++)
    {
      for(j=i+1;j<nbr_mft;j++)
      {
	unsigned int sec_per_cluster=0;
	if(info_mft[i].mft_lcn > info_mft[j].mftmirr_lcn)
	{
	  if((info_mft[j].sector - info_mft[i].sector)%(info_mft[i].mft_lcn - info_mft[j].mftmirr_lcn)==0)
	    sec_per_cluster=(info_mft[j].sector - info_mft[i].sector)/(info_mft[i].mft_lcn - info_mft[j].mftmirr_lcn);
	}
	else if(info_mft[i].mft_lcn < info_mft[j].mftmirr_lcn)
	{
	  if((info_mft[j].sector - info_mft[i].sector)%(info_mft[j].mftmirr_lcn - info_mft[i].mft_lcn)==0)
	    sec_per_cluster=(info_mft[j].sector - info_mft[i].sector)/(info_mft[j].mftmirr_lcn - info_mft[i].mft_lcn);
	}
	if(sec_per_cluster!=0)
	{
	  partition->part_offset=partition->part_offset + (info_mft[i].sector -
	    info_mft[i].mft_lcn * sec_per_cluster) * disk_car->sector_size;
	  if(find_partition==0)
	    log_info("Potential partition:\n");
	  log_partition(disk_car, partition);
	  find_partition=1;
	}
	else
	{
	  if(info_mft[i].mftmirr_lcn > info_mft[j].mft_lcn)
	  {
	    if((info_mft[j].sector - info_mft[i].sector)/(info_mft[i].mftmirr_lcn - info_mft[j].mft_lcn)==0)
	      sec_per_cluster=(info_mft[j].sector - info_mft[i].sector)/(info_mft[i].mftmirr_lcn - info_mft[j].mft_lcn);
	  }
	  else if(info_mft[i].mftmirr_lcn < info_mft[j].mft_lcn)
	  {
	    if((info_mft[j].sector - info_mft[i].sector)/(info_mft[j].mft_lcn - info_mft[i].mftmirr_lcn)==0)
	      sec_per_cluster=(info_mft[j].sector - info_mft[i].sector)/(info_mft[j].mft_lcn - info_mft[i].mftmirr_lcn);
	  }
	  if(sec_per_cluster!=0)
	  {
	    partition->part_offset=partition->part_offset + (info_mft[i].sector -
		info_mft[i].mftmirr_lcn * sec_per_cluster) * disk_car->sector_size;
	    if(find_partition==0)
	      log_info("Potential partition:\n");
	    log_partition(disk_car, partition);
	    find_partition=1;
	  }
	}
      }
    }
    partition->part_offset=tmp;
  }
#ifdef HAVE_NCURSES
  if(interface>0 && expert>0)
  {
    wmove(stdscr, INTER_NTFS_Y, INTER_NTFS_X);
    sectors_per_cluster=ask_number(sectors_per_cluster,0,512,"Sectors per cluster ");
    wmove(stdscr, INTER_NTFS_Y, INTER_NTFS_X);
    mft_lcn=ask_number(mft_lcn,0,0,"MFT LCN ");
    wmove(stdscr, INTER_NTFS_Y, INTER_NTFS_X);
    mftmirr_lcn=ask_number(mftmirr_lcn,0,0,"MFTMIRR LCN ");
    wmove(stdscr, INTER_NTFS_Y, INTER_NTFS_X);
    mft_record_size=ask_number(mft_record_size,0,4096," mft record size ");
  }
#endif
  /* TODO read_mft_info(partition,sector,*sectors_per_cluster,*mft_lcn,*mftmirr_lcn,*mft_record_size); */
  if(sectors_per_cluster>0 && mft_record_size>0)
  {
    unsigned int index_block_size=4096;
    /* Read "root directory" in MFT */
    if(disk_car->read(disk_car,mft_record_size, &buffer, partition->part_offset+(uint64_t)mft_lcn*sectors_per_cluster*disk_car->sector_size+5*(uint64_t)mft_record_size)!=0)
    {
      display_message("NTFS Can't read \"root directory\" in MFT\n");
      return 1;
    }
    index_block_size=ntfs_get_attr(buffer,0x90,partition,buffer+mft_record_size,verbose,0,NULL);
    if(index_block_size%512!=0)
      index_block_size=4096;
    log_info("ntfs_find_mft: sectors_per_cluster %u\n",sectors_per_cluster);
    log_info("ntfs_find_mft: mft_lcn             %lu\n",(long unsigned int)mft_lcn);
    log_info("ntfs_find_mft: mftmirr_lcn         %lu\n",(long unsigned int)mftmirr_lcn);
    log_info("ntfs_find_mft: mft_record_size     %u\n",mft_record_size);
    log_info("ntfs_find_mft: index_block_size    %u\n",index_block_size);
    create_ntfs_boot_sector(disk_car,partition, interface, sectors_per_cluster*disk_car->sector_size, mft_lcn, mftmirr_lcn, mft_record_size, index_block_size,current_cmd);
    /* TODO: ask if the user want to continue the search of MFT */
  }
  else
  {
    log_error("Failed to rebuild NTFS boot sector.\n");
  }
  return 0;
}

static int testdisk_ffs(int x)
{
  int r = 1;

  if (!x)
    return 0;
  if (!(x & 0xffff)) {
    x >>= 16;
    r += 16;
  }
  if (!(x & 0xff)) {
    x >>= 8;
    r += 8;
  }
  if (!(x & 0xf)) {
    x >>= 4;
    r += 4;
  }
  if (!(x & 3)) {
    x >>= 2;
    r += 2;
  }
  if (!(x & 1)) {
    x >>= 1;
    r += 1;
  }
  return r;
}

#ifdef HAVE_NCURSES
static int ncurses_ntfs_info(const struct ntfs_boot_sector *ntfs_header)
{
  wprintw(stdscr,"filesystem size           %llu\n", (long long unsigned)(le64(ntfs_header->sectors_nbr)+1));
  wprintw(stdscr,"sectors_per_cluster       %u\n",ntfs_header->sectors_per_cluster);
  wprintw(stdscr,"mft_lcn                   %lu\n",(long unsigned int)le64(ntfs_header->mft_lcn));
  wprintw(stdscr,"mftmirr_lcn               %lu\n",(long unsigned int)le64(ntfs_header->mftmirr_lcn));
  wprintw(stdscr,"clusters_per_mft_record   %d\n",ntfs_header->clusters_per_mft_record);
  wprintw(stdscr,"clusters_per_index_record %d\n",ntfs_header->clusters_per_index_record);
  return 0;
}

static int ncurses_ntfs2_info(const struct ntfs_boot_sector *nh1, const struct ntfs_boot_sector *nh2)
{
  wprintw(stdscr,"filesystem size           %llu %llu\n",
      (long long unsigned)(le64(nh1->sectors_nbr)+1),
      (long long unsigned)(le64(nh2->sectors_nbr)+1));
  wprintw(stdscr,"sectors_per_cluster       %u %u\n",nh1->sectors_per_cluster,nh2->sectors_per_cluster);
  wprintw(stdscr,"mft_lcn                   %lu %lu\n",
      (long unsigned int)le64(nh1->mft_lcn),
      (long unsigned int)le64(nh2->mft_lcn));
  wprintw(stdscr,"mftmirr_lcn               %lu %lu\n",
      (long unsigned int)le64(nh1->mftmirr_lcn),
      (long unsigned int)le64(nh2->mftmirr_lcn));
  wprintw(stdscr,"clusters_per_mft_record   %d %d\n",nh1->clusters_per_mft_record,nh2->clusters_per_mft_record);
  wprintw(stdscr,"clusters_per_index_record %d %d\n",nh1->clusters_per_index_record,nh2->clusters_per_index_record);
  return 0;
}
#endif

int log_ntfs2_info(const struct ntfs_boot_sector *nh1, const struct ntfs_boot_sector *nh2)
{
  log_info("filesystem size           %llu %llu\n",
      (long long unsigned)(le64(nh1->sectors_nbr)+1),
      (long long unsigned)(le64(nh2->sectors_nbr)+1));
  log_info("sectors_per_cluster       %u %u\n",nh1->sectors_per_cluster,nh2->sectors_per_cluster);
  log_info("mft_lcn                   %lu %lu\n",(long unsigned int)le64(nh1->mft_lcn),(long unsigned int)le64(nh2->mft_lcn));
  log_info("mftmirr_lcn               %lu %lu\n",(long unsigned int)le64(nh1->mftmirr_lcn),(long unsigned int)le64(nh2->mftmirr_lcn));
  log_info("clusters_per_mft_record   %d %d\n",nh1->clusters_per_mft_record,nh2->clusters_per_mft_record);
  log_info("clusters_per_index_record %d %d\n",nh1->clusters_per_index_record,nh2->clusters_per_index_record);
  return 0;
}


