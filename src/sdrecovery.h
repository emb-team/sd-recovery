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

#ifndef SdRecovery_H
#define SdRecovery_H
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QProgressBar>
#include <QTreeWidget>
#include <QThread>
#include "types.h"
#include "common.h"
#include "filegen.h"
#include "photorec.h"

struct file_list {
  struct file_list *fnext;
  struct file_list *dnext;
  struct file_data *data;
};

struct file_data {
  char *name;
  char *full_path;
  char *size;
  unsigned long long st_size;
  char *datestr;
  char *status;
  unsigned long int inode;
  unsigned long int parent_inode;
  bool dir;
  bool checked;
};

class WorkerThread : public QThread
{
    	Q_OBJECT
    	void run() override;
	public:
		void set_disk(disk_t * disk) { selected_disk = disk; }
		void set_partition(partition_t * part) { selected_partition = part; }
		void set_restore_dir(char *dir) { restore_dir = dir; }
		void set_file_list(struct file_list *l) { list = l; }
		void set_op(int operation) { op = operation; }
	signals:
    		void resultReady(const QString &s);
    
	private:
		QProgressBar 		*progress_bar;
		disk_t      		*selected_disk;
		partition_t 		*selected_partition;
		char 			*restore_dir;
		QTableWidget		*filestatsWidget;
		struct file_list	*list;
		int op;
};

class SdRecovery: public QWidget
{
  	Q_OBJECT

        public:
                SdRecovery(QWidget *parent = 0);
		~SdRecovery();
        private slots:
		/* Setup recovery UI */
	  	void disk_changed(int index);
		void partition_selected();
		void setExistingDirectory();
		void SdRecovery_about();
		void SdRecovery_donate();
		void SdRecovery_restore();
		void buttons_updateUI();
		/* Recovery UI */
		void SdRecovery_restore_updateUI();
		void stop_and_quit();
		void SdRecovery_start_recovery();
		void SdRecovery_stop_recovery();
		void handleResults();
		void SdRecovery_part_read();
		void SdRecovery_part_read_stop();
		void SdRecovery_read_updateUI();
		void clear_QTree();
		void SdRecovery_treeItemChanged(QTreeWidgetItem *item, int column);
		void SdRecovery_treeItemExpanded(QTreeWidgetItem *item);
		
	protected:
                void setupUI();
		void clearWidgets();
                int no_disk_warning();
		QWidget *copyright(QWidget * qwparent = 0);
		void PartListWidget_updateUI();
		void HDDlistWidget_updateUI();
		void SdRecovery_restore_setupUI();
		void select_disk(disk_t *disk);
	
	signals:
		void finished();
        private:
		list_disk_t		*list_disk;
		disk_t      		*selected_disk;
		list_part_t 		*list_part;
		partition_t 		*selected_partition;
		char 			*restore_dir;
		/* Setup recovery UI */
                QComboBox 		*HDDlistWidget;
                QTableWidget 		*PartListWidget;
                QTreeWidget		*FsTreeWidget;
		QLineEdit 		*directoryLabel;
		QPushButton 		*button_restore;
		/* Recovery UI */
		QLabel			*folder_txt;
		QLabel 			*progress_info;
		QLabel 			*progress_filefound;
		QProgressBar 		*progress_bar;
		QTimer 			*timer;
		QTableWidget		*filestatsWidget;
		QPushButton 		*button_start;
		QPushButton 		*button_stop;
		QPushButton 		*button_scan;
		QPushButton 		*button_scan_stop;
		WorkerThread		*t;
		struct file_list	*list;
		QLabel 			*partTotalLabel;
		QLabel 			*badFileNrLabel;
		QLabel			*filesTotalLabel;

};

#endif
