/*
    Copyright (C) 2024  Evgenii Zharkov

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdarg.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>	/* unlink, ftruncate */
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <ctype.h>      /* tolower */
#ifdef HAVE_LOCALE_H
#include <locale.h>	/* setlocale */
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <errno.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <QApplication>
#include <QLayoutItem>
#include <QLabel>
#include <QLayout>
#include <QTableView>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QDialogButtonBox>
#include <QSortFilterProxyModel>
#include <QGroupBox>
#include <QFileDialog>
#include <QComboBox>
#include <QTimer>
#include <QMessageBox>
#include <QTextDocument>
#include <QTreeWidget>
#include <QDesktopServices>
#include <QThread>
#include "types.h"
#include "common.h"
#include "hdcache.h"
#include "hdaccess.h"
#include "fnctdsk.h"
#include "filegen.h"
#include "sessionp.h"
#include "intrf.h"
#include "partauto.h"
#include "phcfg.h"
#include "log.h"
#include "log_part.h"
#include "sdrecovery.h"
#include "dir.h"
#include "ext2_dir.h"
#include "rfs_dir.h"
#include "ntfs.h"
#include "ntfs_dir.h"
#include "fat.h"
#include "guid_cmp.h"
#include "fat_dir.h"
#include "exfat_dir.h"
#include "io_redir.h"

extern const arch_fnct_t arch_none;
extern file_enable_t array_file_enable[];

char * statuses[9] = { "Restored", "File stat failed", "File open failed", "File read failed", "File create failed", "Restore failed: No space available", "Restore failed: Close failed", "Restore failed: No memory available", "Something went wrong"};

bool stop_the_recovery=false;
  
SdRecovery::SdRecovery(QWidget *my_parent) : QWidget(my_parent)
{
  const int verbose=1;
  const int testdisk_mode=TESTDISK_O_RDONLY|TESTDISK_O_READAHEAD_32K;
  list_disk_t *element_disk;

  list_disk=NULL;
  selected_disk=NULL;
  list_part=NULL;
  selected_partition=NULL;
  list=NULL;
  restore_dir=NULL;

  setWindowIcon( QPixmap( ":res/sdrecovery_64x64.png" ) );
  this->setWindowTitle(tr("SdRecovery"));
  QVBoxLayout *mainLayout = new QVBoxLayout();
  this->setLayout(mainLayout);

  list_disk=hd_parse(NULL, verbose, testdisk_mode);

  hd_update_all_geometry(list_disk, verbose);
  for(element_disk=list_disk;element_disk!=NULL;element_disk=element_disk->next)
    element_disk->disk=new_diskcache(element_disk->disk,testdisk_mode);
  if(list_disk==NULL)
  {
    no_disk_warning();
    exit(1);
  }
  else
    select_disk(list_disk->disk);
  setupUI();
}

void free_file_list(struct file_list *list)
{
  struct file_list *p, *pnext;
  
  if (!list)
  	return;
  
  for (p = list->fnext; p; p = pnext) {
        if (p->data) {
        	if (p->data->dir == true) {
       			free_file_list(p->dnext);
       			p->dnext = NULL;	
       		}
       		
       		free(p->data->name);
       		if (p->data->full_path)
       			free(p->data->full_path);
		free(p->data->size);
		free(p->data->datestr);
			
  		delete p->data;
  		p->data = NULL;
        }
	
	pnext = p->fnext;
	delete p;
  }
  
  delete list;
}

SdRecovery::~SdRecovery()
{
  log_close();

  if (list) {
  	free_file_list(list); list = NULL;
  }
  
  if (restore_dir) free(restore_dir);

  part_free_list(list_part);
  delete_list_disk(list_disk);
}

void SdRecovery::setExistingDirectory()
{
  QString directory = QFileDialog::getExistingDirectory(this,
      tr("Please select a destination to save the recovered files to."),
      directoryLabel->text(),
      QFileDialog::ShowDirsOnly);
  if (!directory.isEmpty())
  {
    directoryLabel->setText(directory);
    buttons_updateUI();
  }
}

void SdRecovery::partition_selected()
{
  if(PartListWidget->selectedItems().count()<=0)
    return;
    
  if (FsTreeWidget) {
  	clear_QTree();
  	FsTreeWidget->clear();
  	FsTreeWidget->setEnabled(false);
  }
  
  if (list) {
  	free_file_list(list); list = NULL;
  }
  
  filesTotalLabel->setText("");
  partTotalLabel->setText("");
  badFileNrLabel->setText("");
  
  list_part_t *tmp;
  const QString& s = PartListWidget->selectedItems()[0]->text();
  if(s.compare("")==0)
  {
    const QString& s2 = PartListWidget->selectedItems()[2]->text();
    for(tmp=list_part; tmp!=NULL; tmp=tmp->next)
    {
      partition_t *part=tmp->part;
      if(part->order==NO_ORDER && s2.compare(arch_none.get_partition_typename(part))==0)
      {
	selected_partition=part;
	buttons_updateUI();
	return ;
      }
    }
    if(list_part!=NULL)
    {
      selected_partition=list_part->part;
      buttons_updateUI();
      return ;
    }
    return ;
  }
  for(tmp=list_part; tmp!=NULL; tmp=tmp->next)
  {
    partition_t *part=tmp->part;
    if(QString::number(part->order).compare(s)==0)
    {
      selected_partition=part;
      buttons_updateUI();
      return ;
    }
  }
}

void SdRecovery::PartListWidget_updateUI()
{
  list_part_t *element;
  PartListWidget->setRowCount(0);
  PartListWidget->setSortingEnabled(false);
  
  for(element=list_part; element!=NULL; element=element->next)
  {
    const partition_t *partition=element->part;
    if(partition->status!=STATUS_EXT_IN_EXT)
    {
      const arch_fnct_t *arch=partition->arch;
      const int currentRow = PartListWidget->rowCount();
      PartListWidget->setRowCount(currentRow + 1);
      if(partition->order==NO_ORDER)
      {
	QTableWidgetItem *item = new QTableWidgetItem();
	item->setData(0, "");
	PartListWidget->setItem(currentRow, 0, item);
      }
      else
      {
	QTableWidgetItem *item = new QTableWidgetItem();
	item->setData(0, partition->order);
	PartListWidget->setItem(currentRow, 0, item);
      }
      if(partition->upart_type>0)
      {
	QTableWidgetItem *item=new QTableWidgetItem(QString(arch_none.get_partition_typename(partition)));
	item->setToolTip(QString(partition->info));
	PartListWidget->setItem(currentRow, 1, item);
      }
      else
      {
	PartListWidget->setItem(currentRow, 1, new QTableWidgetItem(""));
      }
      {
	char sizeinfo[32];
	QTableWidgetItem *item;
	size_to_unit(partition->part_size, &sizeinfo[0]);
	item=new QTableWidgetItem(QString(sizeinfo));
	item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	PartListWidget->setItem(currentRow, 2, item);
	/* Select the partition if it's already known */
	if(selected_partition == partition)
	  PartListWidget->setCurrentItem(item);
      }
      {
	QString partname="";
	if(partition->partname[0]!='\0')
	{
	  partname.sprintf("[%s]", partition->partname);
	}
	if(partition->fsname[0]!='\0')
	{
	  QString fsname;
	  fsname.sprintf(" [%s]", partition->fsname);
	  partname.append(fsname);
	}
	PartListWidget->setItem(currentRow, 3, new QTableWidgetItem(partname));
      }
    }
  }
  //PartListWidget->setSortingEnabled(true);
  PartListWidget->sortByColumn(0, Qt::AscendingOrder);
  PartListWidget->resizeColumnsToContents();
}

void SdRecovery::select_disk(disk_t *disk)
{
  if(disk==NULL)
    return ;
  selected_disk=disk;
  selected_partition=NULL;
  autodetect_arch(selected_disk, &arch_none);
  log_info("%s\n", selected_disk->description_short(selected_disk));
  part_free_list(list_part);
  list_part=init_list_part(selected_disk, NULL);
  /* If only whole disk is listed, select it */
  /* If there is the whole disk and only one partition, select the partition */
  if(list_part!=NULL)
  {
    if(list_part->next==NULL)
      selected_partition=list_part->part;
    else if(list_part->next->next==NULL)
      selected_partition=list_part->next->part;
  }
}

void SdRecovery::disk_changed(int index)
{
  int i;
  list_disk_t *element_disk;
  if (FsTreeWidget) {
  	clear_QTree();
  	FsTreeWidget->clear();
  	FsTreeWidget->setEnabled(false);	
  }
   	
  if (list) {
  	free_file_list(list); list = NULL;
  }
  
  filesTotalLabel->setText("");
  partTotalLabel->setText("");
  badFileNrLabel->setText("");
  	
  for(element_disk=list_disk, i=0;
      element_disk!=NULL;
      element_disk=element_disk->next, i++)
  {
    if(i==index)
    {
      select_disk(element_disk->disk);
      PartListWidget_updateUI();
      return;
    }
  }
}

QWidget *SdRecovery::copyright(QWidget * qwparent)
{
  QWidget *C_widget = new QWidget(qwparent);
  QLabel *t_logo=new QLabel(C_widget);
  QPixmap pixmap_img = QPixmap(":res/sdrecovery_64x64.png");
  t_logo->setPixmap(pixmap_img);

  QSizePolicy c_sizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  t_logo->setSizePolicy(c_sizePolicy);

  QLabel *t_copy=new QLabel(C_widget);

  t_copy->setText("Sd Recovery Utility, " + QString(TESTDISKDATE) + "<br>\n");
  t_copy->setTextFormat(Qt::RichText);
  t_copy->setTextInteractionFlags(Qt::TextBrowserInteraction);
  t_copy->setOpenExternalLinks(true);

  QHBoxLayout *C_layout = new QHBoxLayout(C_widget);
  C_layout->addStretch(1);
  C_layout->addWidget(t_logo);
  C_layout->addWidget(t_copy);
  C_layout->addStretch(1);
  C_widget->setLayout(C_layout);
  return C_widget;
}

int SdRecovery::no_disk_warning()
{
  QString msg;
  msg=tr("No hard disk found");
#if defined(__CYGWIN__) || defined(__MINGW32__)
  msg=tr("No hard disk found\n"
    "You need to be administrator to run this program.\n"
    "Under Windows, please select this program, right-click and choose \"Run as administrator\".");
#elif defined(DJGPP)
#else
#ifdef HAVE_GETEUID
  if(geteuid()!=0)
  {
    msg=tr("No hard disk found\n"
      "You need to be root to use SdRecovery.");
  }
#endif
#endif
  return QMessageBox::warning(this,tr("No Disk!"), msg, QMessageBox::Ok);
}

void SdRecovery::buttons_updateUI()
{
  if(selected_disk==NULL || selected_partition==NULL)
  {
    button_restore->setEnabled(false);
    return ;
  }
  
  button_scan->setEnabled(selected_partition!=NULL);
  button_restore->setEnabled(!directoryLabel->text().isEmpty());
}

void SdRecovery::HDDlistWidget_updateUI()
{
  list_disk_t *element_disk;
  int i;
  HDDlistWidget->clear();
  for(element_disk=list_disk, i=0;
      element_disk!=NULL;
      element_disk=element_disk->next, i++)
  {
    disk_t *disk=element_disk->disk;
    QString description=disk->description_short(disk);
    if(disk->serial_no!=NULL)
      description += ", S/N:" + QString(disk->serial_no);
    HDDlistWidget->addItem(
	QIcon::fromTheme("drive-harddisk", QIcon(":res/gnome/drive-harddisk.png")),
	description);
    if(disk==selected_disk)
      HDDlistWidget->setCurrentIndex(i);
  }
}

extern const arch_fnct_t arch_none;
extern const arch_fnct_t arch_i386;
extern const arch_fnct_t arch_mac;
extern const arch_fnct_t arch_sun;
extern const arch_fnct_t arch_gpt;

int is_part_linux(const partition_t *partition)
{
  if(partition->arch==&arch_i386 && partition->part_type_i386==P_LINUX)
      return 1;
  if(partition->arch==&arch_sun  && partition->part_type_sun==PSUN_LINUX)
      return 1;
  if(partition->arch==&arch_mac  && partition->part_type_mac==PMAC_LINUX)
      return 1;
  if(partition->arch==&arch_gpt &&
      (
       guid_cmp(partition->part_type_gpt,GPT_ENT_TYPE_LINUX_DATA)==0 ||
       guid_cmp(partition->part_type_gpt,GPT_ENT_TYPE_LINUX_HOME)==0 ||
       guid_cmp(partition->part_type_gpt,GPT_ENT_TYPE_LINUX_SRV)==0
      ))
      return 1;
  return 0;
}

unsigned int dir_nbr=0;
#define MAX_DIR_NBR 256
unsigned long int inode_known[MAX_DIR_NBR];
static int is_inode_valid(const file_info_t *current_file)
{
  const unsigned long int new_inode=current_file->st_ino;
  unsigned int i;
  if(new_inode<2)
    return 0;
  if(strcmp(current_file->name, "..")==0)
    return 0;
  /*@ loop assigns i; */
  for(i=0; i<dir_nbr; i++)
    if(new_inode==inode_known[i]) /* Avoid loop */
      return 0;
  return 1;
}

static dir_partition_t dir_partition_init(disk_t *disk, const partition_t *partition, const int verbose, const int expert, dir_data_t *dir_data)
{
  dir_partition_t res=DIR_PART_ENOIMP;
  if(is_part_fat(partition))
    res=dir_partition_fat_init(disk, partition, dir_data, verbose);
  else if(is_part_ntfs(partition))
  {
    res=dir_partition_ntfs_init(disk, partition, dir_data, verbose, expert);
    if(res!=DIR_PART_OK)
      res=dir_partition_exfat_init(disk, partition, dir_data, verbose);
  }
  else if(is_part_linux(partition))
  {
    res=dir_partition_ext2_init(disk, partition, dir_data, verbose);
    if(res!=DIR_PART_OK)
      res=dir_partition_reiser_init(disk, partition, dir_data, verbose);
  }
  if(res==DIR_PART_OK)
    return DIR_PART_OK;
  switch(partition->upart_type)
  {
    case UP_FAT12:
    case UP_FAT16:
    case UP_FAT32:
      return dir_partition_fat_init(disk, partition, dir_data, verbose);
    case UP_EXT4:
    case UP_EXT3:
    case UP_EXT2:
      return dir_partition_ext2_init(disk, partition, dir_data, verbose);
    case UP_RFS:
    case UP_RFS2:
    case UP_RFS3:
      return dir_partition_reiser_init(disk, partition, dir_data, verbose);
    case UP_NTFS:
      return dir_partition_ntfs_init(disk, partition, dir_data, verbose, expert);
    case UP_EXFAT:
      return dir_partition_exfat_init(disk, partition, dir_data, verbose);
    default:
      return res;
  }
}

static int file_valid(const partition_t *partition, const file_info_t *current_file)
{
        //if (!current_file->st_size) 
        //      return 0;

        if (current_file->st_size > partition->part_size)
               return 0;
        if (!current_file->st_ino)
                return 0;
        if (current_file->name[0]=='\0')
                return 0;

//      if (LINUX_S_ISDIR(current_file->st_mode)!=0 && current_file->st_size != 0x40000)
//              return 0;

        //0x40000 - exFAT
        //0x1000 - ext4
        //0x0 - NTFS
        return 1;
}

void add_file_to_list(struct file_list *list, struct file_list *item)
{
	struct file_list *p;
	
	if (!list) return; 
	
	if (!list->fnext) {
		list->fnext = item;
		return;
	}
	
	for (p = list->fnext; p->fnext; p=p->fnext);
	
	if (p) {
		p->fnext = item;
	}
	return;
}

unsigned int bad_file_nbr = 0;
unsigned int file_nbr = 0;
bool read_finished = false;
bool stop_the_read = false;
unsigned long long part_total_size = 0;


static int dir_whole_partition_read(struct file_list *list, disk_t *disk, const partition_t *partition, dir_data_t *dir_data, const unsigned long int inode)
{
  struct td_list_head *file_walker = NULL;

  const unsigned int current_directory_namelength=strlen(dir_data->current_directory);
  char datestr[80];
  char size [80];
  int res = 0;
  
  file_info_t dir_list;
  TD_INIT_LIST_HEAD(&dir_list.list);
  if(dir_nbr==MAX_DIR_NBR)
    return 1;   /* subdirectories depth is too high => Back */
  dir_data->get_dir(disk, partition, dir_data, inode, &dir_list);
  /* Not perfect for FAT32 root cluster */
  inode_known[dir_nbr++]=inode;
  td_list_for_each(file_walker, &dir_list.list)
  {
    if (stop_the_read==true) {
        read_finished = true;
    	res = -1;
    	goto exit;
    }
    
    const file_info_t *current_file=td_list_entry_const(file_walker, const file_info_t, list);
       
    if(strlen(dir_data->current_directory) + 1 + strlen(current_file->name) <
	sizeof(dir_data->current_directory)-1)
    {
            if(strcmp(dir_data->current_directory,"/"))
	       strcat(dir_data->current_directory,"/");
            strcat(dir_data->current_directory,current_file->name);
    
	    if(LINUX_S_ISDIR(current_file->st_mode)!=0)
	    {
	    	if (is_inode_valid(current_file)>0 && file_valid(partition,current_file)) {
			file_list *l = new file_list;
			if (!l) {
				read_finished = true;
				res = -1;
    				goto exit;
			}
			memset(l, 0, sizeof(struct file_list));
			
			l->dnext = new file_list;
			if (!l->dnext) {
				delete l;
				read_finished = true;
				res = -1;
    				goto exit;
			}
			memset(l->dnext, 0, sizeof(struct file_list));
			
			file_data *p = new file_data;
			if (!p) {
				delete l; delete l->dnext;
				read_finished = true;
				res = -1;
    				goto exit;
			}
			memset(p, 0, sizeof(struct file_data));
			
			p->name = strdup(current_file->name);
	       	 	snprintf(size, sizeof(size), "%lu", current_file->st_size,size);
			p->size = strdup(size);
			p->st_size = current_file->st_size;
	    		set_datestr((char *)&datestr, sizeof(datestr), current_file->td_mtime);
			p->datestr = strdup(datestr);
			p->inode = current_file->st_ino;
			p->parent_inode = inode;
			p->checked = true;
			p->dir = true;
			l->data = p;
			add_file_to_list(list, l);
	      		if (dir_whole_partition_read(l->dnext, disk, partition, dir_data, current_file->st_ino) == -1) {
	      			read_finished = true;
				res = -1;
    				goto exit;
	      		}
	     	} else {
	     		bad_file_nbr++;
	     	}
	    } else {
	    	set_datestr((char *)&datestr, sizeof(datestr), current_file->td_mtime);
	    	if (file_valid(partition, current_file)) {
			file_list *l = new file_list;
			if (!l) {
				read_finished = true;
				res = -1;
    				goto exit;
			}
			memset(l, 0, sizeof(struct file_list));
			
			file_data *p = new file_data;
			if (!p) {
				delete l;
				read_finished = true;
				res = -1;
    				goto exit;
			}
			memset(p, 0, sizeof(struct file_data));
			
			p->name = strdup(current_file->name);
			snprintf(size, sizeof(size), "%lu", current_file->st_size,size);
			p->size = strdup(size);
			p->st_size = current_file->st_size;
			set_datestr((char *)&datestr, sizeof(datestr), current_file->td_mtime);
			p->datestr = strdup(datestr);
			p->inode = current_file->st_ino;
			p->parent_inode = inode;
			p->checked = true;
			l->data = p;
			
			add_file_to_list(list, l);
			part_total_size+=current_file->st_size;
			file_nbr++;
	    	} else {
	    		bad_file_nbr++;
	    	}
	    }
     /* restore current_directory name */
     dir_data->current_directory[current_directory_namelength]='\0';
    } else {
      bad_file_nbr++;
    }
  }
  
exit:
  delete_list_file(&dir_list);
  dir_nbr--;
  return res;
}

struct file_list *find_list_file_checked(struct file_list *list, const file_info_t *current_file, const unsigned long int inode)
{
	struct file_list *p = NULL;

	for (p = list; p; p=p->fnext) {
		if (p->data->inode == current_file->st_ino && p->data->parent_inode == inode) {
			if (!strncmp(p->data->name, current_file->name, strlen(p->data->name))) {
				if (p->data->checked == true) {
					return p;
				}
			}
		}
	}
	return NULL;
}

unsigned int selected_file_nbr = 0;
bool recovery_finished = false;
unsigned long long size_copied_files = 0;
unsigned int f_data_count=0;
file_data **f_data = NULL;

static int dir_whole_partition_copy_aux(struct file_list *list, disk_t *disk, const partition_t *partition, dir_data_t *dir_data, const unsigned long int inode)
{
  struct td_list_head *file_walker = NULL;
  const unsigned int current_directory_namelength=strlen(dir_data->current_directory);
  char datestr[80];
  char size [80];
  int res = 0;

  file_info_t dir_list;
  TD_INIT_LIST_HEAD(&dir_list.list);
  if(dir_nbr==MAX_DIR_NBR)
    return 1;	/* subdirectories depth is too high => Back */
  dir_data->get_dir(disk, partition, dir_data, inode, &dir_list);
  /* Not perfect for FAT32 root cluster */
  inode_known[dir_nbr++]=inode;
  td_list_for_each(file_walker, &dir_list.list)
  {
    if (stop_the_recovery==true) {
    	recovery_finished = true;
    	res = -1;
    	goto exit;
    }
    const file_info_t *current_file=td_list_entry_const(file_walker, const file_info_t, list);
    if(strlen(dir_data->current_directory) + 1 + strlen(current_file->name) <
	sizeof(dir_data->current_directory)-1)
    {
      if(strcmp(dir_data->current_directory,"/"))
	  strcat(dir_data->current_directory,"/");
      strcat(dir_data->current_directory,current_file->name);
	
      if(LINUX_S_ISDIR(current_file->st_mode)!=0)
      {
	if(is_inode_valid(current_file)>0 && file_valid(partition,current_file))
	{
		struct file_list *dir = find_list_file_checked(list->fnext, current_file, inode);
		if (dir) {
	  		if (dir_whole_partition_copy_aux(dir->dnext, disk, partition, dir_data, current_file->st_ino) == -1) {
		  		recovery_finished = true;
				res = -1;
    				goto exit;
	  		}
	  	}
	}
      }
      else if(LINUX_S_ISREG(current_file->st_mode)!=0 && file_valid(partition,current_file))
      {
      	struct file_list *file = find_list_file_checked(list->fnext, current_file, inode);
      	if (file) {
      		if (f_data_count >= selected_file_nbr) {
      			recovery_finished = true;
			res = -1;
    			goto exit;
      		}
      		
      		file->data->full_path = strdup(dir_data->current_directory);
      					
		int ret = dir_data->copy_file(disk, partition, dir_data, current_file);
		switch (ret) {
		case CP_OK:
			file->data->status = statuses[0];
			break;
		case CP_STAT_FAILED:
			file->data->status = statuses[1];
			break;
		case CP_OPEN_FAILED:
			file->data->status = statuses[2];
			break;
		case CP_READ_FAILED:
			file->data->status = statuses[3];
			break;
		case CP_CREATE_FAILED:
			file->data->status = statuses[4];
			break;
		case CP_NOSPACE:
			file->data->status = statuses[5];
			break;
		case CP_CLOSE_FAILED:
			file->data->status = statuses[6];
			break;
		case CP_NOMEM:
			file->data->status = statuses[7];
			break;
		default:
			file->data->status = statuses[8];
			break;
		}
		
		size_copied_files+=current_file->st_size;
		f_data[f_data_count] = file->data;
		f_data_count++;
		
		if (ret != CP_OK) {
			recovery_finished = true;
			res = -1;
    			goto exit;
		}
       }
     }
     /* restore current_directory name */
     dir_data->current_directory[current_directory_namelength]='\0';
    }
  }
  
exit:
  delete_list_file(&dir_list);
  dir_nbr--;
  return res;
}

static dir_partition_t dir_partition_read(struct file_list *list, disk_t *disk, const partition_t *partition)
{
  dir_data_t dir_data;
  dir_partition_t res;
  dir_data.local_dir=NULL;
  res=dir_partition_init(disk, partition, 0, 0, &dir_data);
  dir_data.display=NULL;

  switch(res)
  {
    case DIR_PART_ENOIMP:
      log_info("DIR_PART_ENOIMP\n");
      break;
    case DIR_PART_ENOSYS:
      log_info("DIR_PART_ENOSYS\n");
      break;
    case DIR_PART_EIO:
      log_info("DIR_PART_EIO\n");
      break;
    case DIR_PART_OK:
      dir_nbr=0;
      memset(inode_known, 0, sizeof(unsigned long int)*MAX_DIR_NBR);
      dir_whole_partition_read(list, disk,partition,&dir_data,dir_data.current_inode);
      dir_data.close(&dir_data);
      break;
    default:
      break;
  }
  if (dir_data.local_dir)
  	free(dir_data.local_dir);
  return res;
}

static dir_partition_t dir_partition_copy(struct file_list *list, disk_t *disk, const partition_t *partition, char *dst_directory)
{
  dir_data_t dir_data;
  dir_partition_t res;
  dir_data.local_dir=NULL;
  res=dir_partition_init(disk, partition, 0, 0, &dir_data);
  dir_data.display=NULL;
  switch(res)
  {
    case DIR_PART_ENOIMP:
      break;
    case DIR_PART_ENOSYS:
      break;
    case DIR_PART_EIO:
      break;
    case DIR_PART_OK:
      dir_nbr=0;
      memset(inode_known, 0, sizeof(unsigned long int)*MAX_DIR_NBR);
      dir_data.local_dir=dst_directory;
      dir_whole_partition_copy_aux(list, disk, partition, &dir_data, dir_data.current_inode);
      dir_data.close(&dir_data);
      break;
    default:
      break;
  }
  return res;
}

struct file_list *find_list_file(struct file_list *list, struct file_data *data)
{
	struct file_list *p = NULL;

	for (p = list; p; p=p->fnext) {
		if (p->data->inode == data->inode && p->data->parent_inode == data->parent_inode) {
			if (!strncmp(p->data->name, data->name, strlen(p->data->name))) {
				return p;
			}
		}
	}

	return NULL;
}

void create_treeView(QTreeWidgetItem *item, struct file_list *list)
{
	struct file_list *p;

	if (!list)
		return;

	for (p = list->fnext; p; p=p->fnext) {
		QTreeWidgetItem *new_item = new QTreeWidgetItem(item);

		new_item->setText(0, p->data->name);
		new_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
		new_item->setCheckState(0, Qt::Checked);
		new_item->setText(1, p->data->size);
		if (p->data->dir == true) {
			new_item->setIcon(0, QIcon(":res/gnome/folder.png")); 
			new_item->setText(2, "Directory");
			if (p->dnext && p->dnext->fnext)
				QTreeWidgetItem *dummy = new QTreeWidgetItem(new_item);
			
		} else {
			new_item->setIcon(0, QIcon(":res/gnome/document.png"));
			new_item->setText(2, "File"); 
		}
		new_item->setText(3, p->data->datestr);
	}
}

dir_partition_t part_read_status;
int reading_progress = 0;
void SdRecovery::SdRecovery_read_updateUI()
{
   char buf[100];
	if (read_finished==true) {
		timer->stop();
		button_scan_stop->setEnabled(false);
		
		if (part_read_status == DIR_PART_OK) {
			create_treeView(FsTreeWidget->invisibleRootItem(), list);
			for (int i = 0; i < FsTreeWidget->columnCount(); i++) {
				FsTreeWidget->resizeColumnToContents(i);
			}
			FsTreeWidget->setEnabled(true);
			button_scan_stop->setText("Stop");

			snprintf(buf, sizeof(buf), "Files: %lu", file_nbr);
			filesTotalLabel->setText(buf);
			
			snprintf(buf, sizeof(buf), "Total size: %lu Mb", part_total_size/1024/1024);
			partTotalLabel->setText(buf);
			
			snprintf(buf, sizeof(buf), "Files ignored: %lu", bad_file_nbr);
			badFileNrLabel->setText(buf);
		} else {
			switch(part_read_status) {
			case DIR_PART_ENOIMP:
			   filesTotalLabel->setText("File-system support is not implemented.");
			   break;
			case DIR_PART_ENOSYS:
			case DIR_PART_EIO:
			   filesTotalLabel->setText("Failed to read file-system on selected partition.");
			   break;
			default:
			   filesTotalLabel->setText("Something went wrong during file-system read.");
			   break;
			}
			
			if (list) { delete list; list = NULL; }
		}
		button_scan->setText("Scan");
		HDDlistWidget->setEnabled(true);
		PartListWidget->setEnabled(true);
	} else {
		switch(++reading_progress) {
		case 4:
			button_scan->setText("Scanning ->");
			break;
		case 8:
			button_scan->setText("Scanning -->");
			break;
		case 12:
			button_scan->setText("Scanning --->");
			reading_progress = 0;
			break;
		default:
			break;
		}
		
		snprintf(buf, sizeof(buf), "Files: %lu", file_nbr);
		filesTotalLabel->setText(buf);
		
		snprintf(buf, sizeof(buf), "Total size: %lu Mb", part_total_size/1024/1024);
		partTotalLabel->setText(buf);
		
		snprintf(buf, sizeof(buf), "Files ignored: %lu", bad_file_nbr);
		badFileNrLabel->setText(buf);
	}
}

unsigned long long files_total_size = 0;
void SdRecovery::SdRecovery_part_read()
{
  if (selected_disk && selected_partition) {
  	button_scan->setEnabled(false);
  	files_total_size = 0;
  	selected_file_nbr=0;
  	file_nbr=0;
  	bad_file_nbr=0;
  	part_total_size=0;
  	
  	if (list) {
  		free_file_list(list); list = NULL;
  	}
  	
  	list = new file_list;
	if (!list) {
		return;
	}
	memset(list, 0, sizeof(struct file_list));
  	
  	t = new WorkerThread();
	t->set_disk(selected_disk);
	t->set_partition(selected_partition);
	t->set_file_list(list);
	t->set_op(0); // read
	
	connect(t, &WorkerThread::finished, t, &QObject::deleteLater);
	connect(t, &WorkerThread::resultReady, this, &SdRecovery::handleResults);
	  
	read_finished=false;
	stop_the_read=false;
	button_scan->setText("Scanning");
	HDDlistWidget->setEnabled(false);
	PartListWidget->setEnabled(false);
	button_scan_stop->setEnabled(true);
  
	t->start();

	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(SdRecovery_read_updateUI()));
	timer->start(250);
  }

  return;
}

void SdRecovery::SdRecovery_part_read_stop()
{
	button_scan_stop->setEnabled(false);
	button_scan_stop->setText("Stoping");
	stop_the_read=true;
}

void SdRecovery::SdRecovery_treeItemChanged(QTreeWidgetItem *item, int column)
{
   if (item->checkState(0) == Qt::Unchecked) {
	   for (int i = 0; i < item->childCount(); i++) {
	   	QTreeWidgetItem *child = item->child(i);
	   	child->setCheckState(0, Qt::Unchecked);
	   }
   } else if (item->checkState(0) == Qt::Checked) {
	   QTreeWidgetItem *parent = item->parent();
	   while (parent) {
	   	parent->setCheckState(0, Qt::Checked);
	   	parent = parent->parent();
	   }
   }
}

struct file_list *find_list_dir(struct file_list *list, char *name)
{
	struct file_list *p = NULL;

	for (p = list; p; p=p->fnext) {
		if (p->data && p->data->dir == true) {
			if (!strncmp(p->data->name, name, strlen(p->data->name))) {
				return p;
			}
		}
	}

	return NULL;
}

void add_children_to_item(struct file_list *list, QTreeWidgetItem *item)
{
   struct file_list * p;

	if (!list || !item)
		return;
	
	if (item->childCount() > 1) {
		return; // already populated, or 1 file, reread
	}
	
	QTreeWidgetItem *child = item->child(0);
	if (child && item->childCount()) {
		item->removeChild(child); // removing dummy
		delete child;
		child = NULL;
	}

	for (p = list->fnext; p; p=p->fnext) {
		
		child = new QTreeWidgetItem(item);

		child->setText(0, p->data->name);
		child->setFlags(item->flags());
		if (item->checkState(0) == Qt::Checked) {
			child->setCheckState(0, Qt::Checked);
		} else {
			child->setCheckState(0, Qt::Unchecked);
		}
		child->setText(1, p->data->size);
		if (p->data->dir == true) {
			child->setIcon(0, QIcon(":res/gnome/folder.png")); 
			child->setText(2, "Directory");
			if (p->dnext && p->dnext->fnext)
				QTreeWidgetItem *dummy = new QTreeWidgetItem(child);
		} else {
			child->setIcon(0, QIcon(":res/gnome/document.png"));
			child->setText(2, "File"); 
		}
		child->setText(3, p->data->datestr);
	}
}

struct tree_list {
	struct tree_list *next;
	char *name;
};

void SdRecovery::SdRecovery_treeItemExpanded(QTreeWidgetItem *item)
{
	struct file_list *dir = NULL;
	struct tree_list *root = NULL;
	
	QTreeWidgetItem *parent = item->parent();

	if (parent) {	
	
		while (parent) {
			struct tree_list *item = new tree_list;
			if (!item) return;
			
			memset(item, 0, sizeof(struct tree_list));
			
			log_info("%s parent\n", (char *)(parent->text(0).toUtf8().constData()));
			
			item->name = strdup(parent->text(0).toUtf8().constData());
			
			if (!root) {
				root = item;
			} else {
				item->next = root;
				root = item;
			}
			parent = parent->parent();
		}
		
		
		struct tree_list *tmp;
		while (root) {
			if (!dir) {
				dir = find_list_dir(list, root->name);
			} else {
				dir = find_list_dir(dir->dnext, root->name);
			}
			
			tmp = root;
			root = root->next;
			
			free(tmp->name);
			delete tmp;
		}
		
		dir = find_list_dir(dir->dnext,  (char *)(item->text(0).toUtf8().constData()));
		
	} else {
		dir = find_list_dir(list,  (char *)(item->text(0).toUtf8().constData()));
	}
	
	if (dir) {
		add_children_to_item(dir->dnext, item);
	}
	
	for (int i = 0; i < FsTreeWidget->columnCount(); i++) {
		FsTreeWidget->resizeColumnToContents(i);
	}
}

void SdRecovery::setupUI()
{
  QWidget *t_copy = copyright(this);
  QLabel *t_select = new QLabel(tr("Please select a disk to recover data from: "));

  HDDlistWidget = new QComboBox();
  HDDlistWidget->setToolTip(tr("Disk capacity must be correctly detected for a successful recovery.\n"));

  QStringList oLabel;
  oLabel.append("");
  oLabel.append(tr("File System"));
  oLabel.append(tr("Size"));
  oLabel.append(tr("Label"));

  PartListWidget= new QTableWidget();
  PartListWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  PartListWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
  PartListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
  PartListWidget->verticalHeader()->hide();
  PartListWidget->setShowGrid(false);
  PartListWidget->setColumnCount( 4 );
  PartListWidget->setHorizontalHeaderLabels( oLabel );
  PartListWidget_updateUI();
  
  button_scan = new QPushButton(
      QIcon::fromTheme("go-next", QIcon(":res/gnome/go-next.png")),
      tr("&Scan"));
      
  button_scan_stop = new QPushButton(
      QIcon::fromTheme("go-previous", QIcon(":res/gnome/go-previous.png")),
      tr("&Stop"));
      
  button_scan_stop->setEnabled(false);    
      
  FsTreeWidget = new QTreeWidget();
  FsTreeWidget->setColumnCount(4);
  QStringList tLabel;
  tLabel.append("Name");
  tLabel.append(tr("Size"));
  tLabel.append(tr("Type"));
  tLabel.append(tr("Date"));
  FsTreeWidget->setHeaderLabels(tLabel);
  FsTreeWidget->setEnabled(false);
 
  filesTotalLabel = new QLabel(this);
  filesTotalLabel->setText("");
 
  partTotalLabel = new QLabel(this);
  partTotalLabel->setText("");
  
  badFileNrLabel = new QLabel(this);
  badFileNrLabel->setText("");
  
  QGroupBox *groupBox1;
  QGroupBox *groupBox2;
  QGroupBox *groupBox3;
  QGroupBox *groupBox4;
  
  groupBox1 = new QGroupBox(tr("Partitions"));
  groupBox2 = new QGroupBox(tr("Filesystem Tree"));
  groupBox3 = new QGroupBox();
  groupBox4 = new QGroupBox();

  QVBoxLayout *groupBox1Layout = new QVBoxLayout;
  QVBoxLayout *groupBox2Layout = new QVBoxLayout;
  QHBoxLayout *groupBox3Layout = new QHBoxLayout;
  QHBoxLayout *groupBox4Layout = new QHBoxLayout;
  
  groupBox3Layout->addWidget(button_scan);
  groupBox3Layout->addWidget(button_scan_stop);
  
  groupBox3->setLayout(groupBox3Layout);
  
  groupBox4Layout->addWidget(filesTotalLabel);
  groupBox4Layout->addWidget(partTotalLabel);
  groupBox4Layout->addWidget(badFileNrLabel);
  
  groupBox4->setLayout(groupBox4Layout);
  
  groupBox1Layout->addWidget(PartListWidget);
  groupBox1Layout->addWidget(groupBox4);
  groupBox1Layout->addWidget(groupBox3);


  groupBox1->setLayout(groupBox1Layout);

  groupBox2Layout->addWidget(FsTreeWidget);
  groupBox2->setLayout(groupBox2Layout);

  QWidget *groupBox= new QWidget();
  QHBoxLayout *groupBoxLayout = new QHBoxLayout;
  groupBoxLayout->addWidget(groupBox1);
  groupBoxLayout->addWidget(groupBox2);
  groupBox->setLayout(groupBoxLayout);

  QLabel *dstWidget= new QLabel(tr("Please select a destination to save the recovered files to."));
  directoryLabel=new QLineEdit("");
  QPushButton *dst_button = new QPushButton(
      QIcon::fromTheme("folder", QIcon(":res/gnome/folder.png")),
      tr("&Browse"));

  QWidget *dst_widget= new QWidget(this);
  QWidget *dst_widget2= new QWidget(this);

  QHBoxLayout *dst_widgetLayout2 = new QHBoxLayout;
  dst_widgetLayout2->addWidget(directoryLabel);
  dst_widgetLayout2->addWidget(dst_button);
  dst_widget2->setLayout(dst_widgetLayout2);

  QVBoxLayout *dst_widgetLayout = new QVBoxLayout;
  dst_widgetLayout->addWidget(dstWidget);
  dst_widgetLayout->addWidget(dst_widget2);
  dst_widget->setLayout(dst_widgetLayout);
  
  button_restore = new QPushButton(QIcon::fromTheme("go-next", QIcon(":res/gnome/go-next.png")), tr("&Restore"));
  button_restore->setEnabled(false);
  QPushButton *button_quit= new QPushButton(QIcon::fromTheme("application-exit", QIcon(":res/gnome/application-exit.png")), tr("&Quit"));
  QPushButton *button_about= new QPushButton(QIcon::fromTheme("help-about", QIcon(":res/gnome/help-about.png")), tr("&About"));
  QPushButton *button_donate= new QPushButton(QIcon::fromTheme("donate", QIcon(":res/gnome/donate.png")),tr("&Donate"));

  QWidget *B_widget = new QWidget(this);
  QHBoxLayout *B_layout = new QHBoxLayout(B_widget);
  B_layout->addWidget(button_about);
  B_layout->addWidget(button_donate);
  B_layout->addWidget(button_restore);
  B_layout->addWidget(button_quit);
  B_widget->setLayout(B_layout);

  clearWidgets();
//  QLayout *mainLayout = this->layout();
  delete this->layout();
  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->addWidget(t_copy);
  mainLayout->addWidget(t_select);
  mainLayout->addWidget(HDDlistWidget);
  mainLayout->addWidget(groupBox);
  mainLayout->addWidget(dst_widget);
  mainLayout->addWidget(B_widget);
  this->setLayout(mainLayout);

  HDDlistWidget_updateUI();
  buttons_updateUI();

  connect(button_scan, SIGNAL(clicked()), this, SLOT(SdRecovery_part_read()) );
  connect(button_scan_stop, SIGNAL(clicked()), this, SLOT(SdRecovery_part_read_stop()) );
  connect(button_about, SIGNAL(clicked()), this, SLOT(SdRecovery_about()) );
  connect(button_donate, SIGNAL(clicked()), this, SLOT(SdRecovery_donate()) );
  connect(button_restore, SIGNAL(clicked()), this, SLOT(SdRecovery_restore()) );
  connect(button_quit, SIGNAL(clicked()), qApp, SLOT(quit()) );
  connect(HDDlistWidget, SIGNAL(activated(int)),this,SLOT(disk_changed(int)));
  connect(PartListWidget, SIGNAL(itemSelectionChanged()), this, SLOT(partition_selected()));
  connect(dst_button, SIGNAL(clicked()), this, SLOT(setExistingDirectory()));
  connect(directoryLabel, SIGNAL(editingFinished()), this, SLOT(buttons_updateUI()));
  connect(FsTreeWidget, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(SdRecovery_treeItemChanged(QTreeWidgetItem*, int)));
  connect(FsTreeWidget, SIGNAL(itemExpanded(QTreeWidgetItem*)), this, SLOT(SdRecovery_treeItemExpanded(QTreeWidgetItem*)));
}

void SdRecovery::clearWidgets()
{
  while(1)
  {
    QLayoutItem *layoutwidget;
    layoutwidget = this->layout()->takeAt(0);
    if(layoutwidget==NULL)
      return ;
    layoutwidget->widget()->hide();
    layoutwidget->widget()->deleteLater();
  }
}

void WorkerThread::run()
{
  QString result;
  switch (op) {
  case 0:
	part_read_status = dir_partition_read(list, selected_disk, selected_partition);
  	read_finished = true;
  	break;
  case 1:
  	dir_partition_copy(list, selected_disk, selected_partition, restore_dir);
  	recovery_finished = true;
  	break;
  default:
  	break;
  }
  emit resultReady(result);
}

void SdRecovery::handleResults()
{
    t->exit();
}

unsigned int rowcount = 0;
void SdRecovery::SdRecovery_start_recovery()
{
  if (list) {
	  button_start->setEnabled(false);
	  //filestatsWidget->clear();
	  
	  f_data = new file_data * [selected_file_nbr];
	  if (!f_data)
	  	return;
	  memset(f_data, 0x0, sizeof(struct file_data *)*selected_file_nbr);
	  f_data_count=0;
	  
	  t = new WorkerThread();
	  t->set_disk(selected_disk);
	  t->set_partition(selected_partition);
	  t->set_restore_dir(restore_dir);
	  t->set_file_list(list);
	  t->set_op(1); // copy
	  connect(t, &WorkerThread::finished, t, &QObject::deleteLater);
	  connect(t, &WorkerThread::resultReady, this, &SdRecovery::handleResults);
	  
	  button_start->setText("Recovering");
	  
	  size_copied_files = 0;
	  rowcount = 0;
	  stop_the_recovery=false;
	  recovery_finished=false;
	  
	  t->start();

	  timer = new QTimer(this);
	  connect(timer, SIGNAL(timeout()), this, SLOT(SdRecovery_restore_updateUI()));
	  timer->start(250);
  }
}

void SdRecovery::SdRecovery_stop_recovery()
{
	button_stop->setEnabled(false);
	button_stop->setText("Stoping");
	stop_the_recovery=true;
}

unsigned int copying_progress = 0;
void SdRecovery::SdRecovery_restore_updateUI()
{
        for (;rowcount < f_data_count && f_data && f_data[rowcount];rowcount++) {
        	struct file_data *p = f_data[rowcount];
        	
        	filestatsWidget->setRowCount(rowcount+1);
        	
		QTableWidgetItem *item1 = new QTableWidgetItem(p->full_path);
		filestatsWidget->setItem(rowcount, 0, item1);
		
		QTableWidgetItem *item2 = new QTableWidgetItem(p->size);
		filestatsWidget->setItem(rowcount, 1, item2);
		QTableWidgetItem *item3 = new QTableWidgetItem("File");
		filestatsWidget->setItem(rowcount, 2, item3);
		QTableWidgetItem *item4 = new QTableWidgetItem(p->datestr);
		filestatsWidget->setItem(rowcount, 3, item4);
		QTableWidgetItem *item5 = new QTableWidgetItem(p->status);
		filestatsWidget->setItem(rowcount, 4, item5);  
		filestatsWidget->scrollToBottom();
		filestatsWidget->resizeColumnsToContents();
        }
        
        if (recovery_finished==true) {
        	log_info("size_copied_files %lu files_total_size %lu\n", size_copied_files, files_total_size);

        	if (f_data) { delete[] f_data; f_data=NULL; }
        	
        	free_file_list(list); list = NULL;

        	QDesktopServices::openUrl(QUrl(restore_dir));
        	
        	button_start->setText("Start");
        	button_stop->setText("Stop");
        	button_stop->setEnabled(false);
        	
        	timer->stop();

		if (files_total_size != size_copied_files) {
			size_copied_files = files_total_size; // make progressbar 100%
		}
        } else if (stop_the_recovery == true) {
        	button_start->setText("Start");
        } else {
        	switch(++copying_progress) {
		case 4:
			button_start->setText("Recovering ->");
			break;
		case 8:
			button_start->setText("Recovering -->");
			break;
		case 12:
			button_start->setText("Recovering --->");
			copying_progress = 0;
			break;
		default:
			break;
		}
        }
        
        progress_bar->setMinimum(0);
        progress_bar->setMaximum(100);
        int val = (int)(((double)size_copied_files/(double)files_total_size)*100);
        progress_bar->setValue(val);
}

void SdRecovery::SdRecovery_restore_setupUI()
{
  clearWidgets();
  delete this->layout();
  QVBoxLayout *mainLayout = new QVBoxLayout();
  QWidget *t_copy = copyright(this);

  QSizePolicy c_sizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

  QLabel *disk_img=new QLabel();
  QPixmap disk_pixmap = QPixmap(":res/gnome/drive-harddisk.png");
  disk_img->setPixmap(disk_pixmap);
  disk_img->setSizePolicy(c_sizePolicy);

  QLabel *disk_txt=new QLabel();
  disk_txt->setText(selected_disk->description_short(selected_disk));

  QWidget *diskWidget = new QWidget();
  QHBoxLayout *diskWidgetLayout = new QHBoxLayout(diskWidget);
  diskWidgetLayout->addWidget(disk_img);
  diskWidgetLayout->addWidget(disk_txt);
  diskWidget->setLayout(diskWidgetLayout);

  QLabel *folder_img=new QLabel();
  QPixmap *folder_pixmap = new QPixmap(":res/gnome/folder.png");
  folder_img->setPixmap(*folder_pixmap);
  folder_img->setSizePolicy(c_sizePolicy);

  folder_txt=new QLabel();
  folder_txt->setTextFormat(Qt::RichText);
  folder_txt->setTextInteractionFlags(Qt::TextBrowserInteraction);
  folder_txt->setOpenExternalLinks(true);
  folder_txt->setText(tr("Destination: ")+restore_dir);

  QWidget *folderWidget = new QWidget();
  QHBoxLayout *folderWidgetLayout = new QHBoxLayout(folderWidget);
  folderWidgetLayout->addWidget(folder_img);
  folderWidgetLayout->addWidget(folder_txt);
  folderWidget->setLayout(folderWidgetLayout);

  progress_info=new QLabel();
  progress_filefound=new QLabel();
  progress_bar=new QProgressBar();

  QWidget *progressWidget = new QWidget();
  QHBoxLayout *progressWidgetLayout = new QHBoxLayout(progressWidget);
  progressWidgetLayout->addWidget(progress_info);
  progressWidgetLayout->addWidget(progress_bar);
  progressWidgetLayout->addWidget(progress_filefound);
  progressWidget->setLayout(progressWidgetLayout);

  QWidget *progressWidget2 = new QWidget();
  QHBoxLayout *progressWidgetLayout2 = new QHBoxLayout(progressWidget2);
// TODO
//  progressWidgetLayout2->addWidget(progress_elapsed);
//  progressWidgetLayout2->addWidget(progress_eta);
  progressWidget2->setLayout(progressWidgetLayout2);

  QStringList tLabel;
  tLabel.append("Name");
  tLabel.append(tr("Size"));
  tLabel.append(tr("Type"));
  tLabel.append(tr("Date"));
  tLabel.append(tr("Status"));
  
  filestatsWidget=new QTableWidget();
  filestatsWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  filestatsWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
  filestatsWidget->setSelectionMode(QAbstractItemView::SingleSelection);
  filestatsWidget->verticalHeader()->hide();
  filestatsWidget->setColumnCount( 5 );
  filestatsWidget->setHorizontalHeaderLabels( tLabel );
  filestatsWidget->resizeColumnsToContents();

  QPushButton *button_quit= new QPushButton(QIcon::fromTheme("application-exit", QIcon(":res/gnome/application-exit.png")), tr("&Quit"));
  QPushButton *button_donate= new QPushButton(QIcon::fromTheme("donate", QIcon(":res/gnome/donate.png")),tr("&Donate"));
  button_start = new QPushButton(QIcon::fromTheme("go-next", QIcon(":res/gnome/go-next.png")), tr("&Start"));
  button_stop = new QPushButton(QIcon::fromTheme("go-previous", QIcon(":res/gnome/go-previous.png")), tr("&Stop"));
  QWidget *B_widget = new QWidget(this);
  QHBoxLayout *B_layout = new QHBoxLayout(B_widget);
  B_layout->addWidget(button_start);
  B_layout->addWidget(button_stop);
  B_layout->addWidget(button_donate);
  B_layout->addWidget(button_quit);
  B_widget->setLayout(B_layout);
  
  mainLayout->addWidget(t_copy);
  mainLayout->addWidget(diskWidget);
  mainLayout->addWidget(folderWidget);
  mainLayout->addWidget(progressWidget);
  mainLayout->addWidget(progressWidget2);
  mainLayout->addWidget(filestatsWidget);
  mainLayout->addWidget(B_widget);
  this->setLayout(mainLayout);

  connect(button_start, SIGNAL(clicked()), this, SLOT(SdRecovery_start_recovery()));
  connect(button_stop, SIGNAL(clicked()), this, SLOT(SdRecovery_stop_recovery()));
  connect(button_quit, SIGNAL(clicked()), this, SLOT(stop_and_quit()));
  connect(button_donate, SIGNAL(clicked()), this, SLOT(SdRecovery_donate()));
  connect(this, SIGNAL(finished()), qApp, SLOT(quit()));
}

void SdRecovery::stop_and_quit()
{
  stop_the_recovery=true;
  emit finished();
}

void SdRecovery::clear_QTree()
{
  QTreeWidgetItemIterator it(FsTreeWidget);
  while (*it) {
    delete *it;
    ++it;
  }
} 

static bool empty_item(const char *item_text)
{
	return item_text[0] == '\0';
}

void parse_QTree(QTreeWidgetItemIterator &it, struct file_list *list)
{ 
  if (!list)
  	return;
  	
  struct file_list *p;
  for (p = list->fnext; p && *it; p=p->fnext, ++it) {
  	log_info("item: %s\n", (*it)->text(0).toLocal8Bit().data());
  	log_info("name: %s\n", p->data->name);
  	if (empty_item((*it)->text(0).toLocal8Bit().data())) ++it; // skip dummy item
  	if (!*it) return;
        if (!strncmp(p->data->name, (char *)((*it)->text(0).toLocal8Bit().data()), strlen(p->data->name))) {
            if ((*it)->checkState(0) == Qt::Unchecked) {
        	p->data->checked = false;
        	log_info("%s state: unchecked\n", p->data->name);
            }
            if (p->data->dir == true) {
        	parse_QTree(++it, p->dnext); --it;
            }
        } else {
        	return; // file was not found return from the dir 
        }
  }
}

void reiterate_the_list(struct file_list *list, bool parent_checked)
{	
  if (!list)
  	return;
  	
  struct file_list *p;
  for (p = list->fnext; p; p=p->fnext) {
            if (p->data->dir == true) {
        	reiterate_the_list(p->dnext, parent_checked ? p->data->checked : parent_checked);
            } else if (parent_checked == true && p->data->checked == true) {
		files_total_size+=p->data->st_size;
		selected_file_nbr++;
	    } else {
	    	p->data->checked = false;
	    }
  }
}
       	
void SdRecovery::SdRecovery_restore()
{
  if(selected_disk==NULL || selected_partition==NULL || list == NULL)
    return;
    
  QByteArray byteArray = (directoryLabel->text()).toUtf8();
  restore_dir=strdup(byteArray.constData());

  QTreeWidgetItemIterator it(FsTreeWidget);
  parse_QTree(it, list);
  
  reiterate_the_list(list, true);
  
  log_info("files_total_size = %lu\n", files_total_size);
  
  clear_QTree();

  SdRecovery_restore_setupUI();
}

void SdRecovery::SdRecovery_about()
{
  QPixmap pixmap_img = QPixmap(":res/sdrecovery_64x64.png");
  QMessageBox msg;
  msg.setText(tr("SdRecovery is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any later version.\n\nSdRecovery is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.\n\nYou should have received a copy of the GNU General Public License along with SdRecovery.  If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>."));
  msg.setWindowTitle(tr("SdRecovery: About"));
  msg.addButton(QMessageBox::Close);
  msg.setIconPixmap(pixmap_img);
  msg.exec();
}

void SdRecovery::SdRecovery_donate()
{
  QDesktopServices::openUrl(QUrl("https://www.paypal.com/donate/?hosted_button_id=F9U5PTQEJCRWE"));
}
