# Makefile - make all application-level programs for cloud hub mesh networking
#
# Copyright (C) 2012, Greg Johnson
# Released under the terms of the GNU GPL v2.0.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# $Id: Makefile,v 1.31 2012-02-22 18:55:24 greg Exp $
#
TARGET = WRT54G

ifeq ($(TARGET),WRT54G)
    CC = mipsel-uclibc-gcc
    CRIT_SECTION=pcritical_section
    CFLAGS += -DWRT54G
else
    CRIT_SECTION=critical_section
endif # WRT54G

CFLAGS += -g -Wall

# CFLAGS += -DRETRY_TIMED_OUT_LOCKABLES

PROGS = label scan \
        ll_shell_ftp update_wrt_wds \
        merge_cloud status_lights \
        set_merge_cloud_db test_print_tree test_encrypt ll_dump

all: $(PROGS)

scan: scan.c util.h util.o
	$(CC) $(CFLAGS) -o scan scan.c util.o

ll_dump: ll_dump.c util.h util.o
	$(CC) $(CFLAGS) -o ll_dump ll_dump.c util.o

set_merge_cloud_db: set_merge_cloud_db.c util.h
	$(CC) $(CFLAGS) -o set_merge_cloud_db set_merge_cloud_db.c

status_lights: status_lights.c util.h
	$(CC) $(CFLAGS) -o status_lights status_lights.c

test_print_tree: test_print_tree.c util.h util.o print_tree.o
	$(CC) $(CFLAGS) -o test_print_tree test_print_tree.c \
        util.o print_tree.o

test_encrypt: encrypt.c
	$(CC) $(CFLAGS) -DUNIT_TEST -o test_encrypt encrypt.c -lcrypt

critical_section.o: critical_section.c critical_section.h
	$(CC) $(CFLAGS) -c critical_section.c

pcritical_section.o: pcritical_section.c pcritical_section.h
	$(CC) $(CFLAGS) -c pcritical_section.c

merge_cloud: merge_cloud.c mac.o $(CRIT_SECTION).o util.o pio.o wrt_util.o \
        eth_util.o mac_list.o com_util.o status.o device_type.o \
        graphit.o sequence.o html_status.o scan_msg.o parm_change.o \
        ping.o cloud_mod.o print.o timer.o random.o io_stat.o ad_hoc_client.o \
        lock.o stp_beacon.o device.o nbr.o cloud_msg.o cloud_box.o encrypt.o \
        print_tree.o \
        \
        cloud.h mac.h util.h pio.h wrt_util.h eth_util.h com_util.h status.h \
        device_type.h graphit.h sequence.h html_status.h ping.h cloud_mod.h \
        print.h timer.h random.h io_stat.h ad_hoc_client.h lock.h cloud_box.h \
        stp_beacon.h device.h nbr.h encrypt.h print_tree.h \

	$(CC) $(CFLAGS) -o merge_cloud merge_cloud.c mac.o \
        $(CRIT_SECTION).o \
        util.o pio.o wrt_util.o eth_util.o mac_list.o com_util.o status.o \
        device_type.o graphit.o sequence.o html_status.o ping.o cloud_mod.o \
        print.o timer.o random.o io_stat.o ad_hoc_client.o scan_msg.o \
        lock.o stp_beacon.o device.o nbr.o cloud_msg.o cloud_box.o \
        parm_change.o encrypt.o print_tree.o \
        -lm -lcrypt

stp_beacon.h: cloud_msg.h mac.h status.h stp_beacon_data.h
	touch stp_beacon.h

stp_beacon.o: stp_beacon.c util.h cloud.h print.h ad_hoc_client.h lock.h \
        html_status.h timer.h stp_beacon.h nbr.h cloud_msg.h stp_beacon.h
	$(CC) $(CFLAGS) -c stp_beacon.c

device.h: mac.h device_type.h print.h cloud.h
	touch device.h

device.o: device.c cloud.h print.h device.h ad_hoc_client.h io_stat.h
	$(CC) $(CFLAGS) -c device.c

cloud_mod.h: mac.h cloud.h cloud_msg.h util.h
	touch cloud_mod.h

cloud_mod.o: cloud_mod.c cloud_mod.h util.h cloud.h print.h lock.h random.h \
        timer.h nbr.h stp_beacon.h
	$(CC) $(CFLAGS) -c cloud_mod.c

random.o: random.h random.c print.h timer.h
	$(CC) $(CFLAGS) -c random.c

io_stat.h: print.h device.h
	touch io_stat.h

io_stat.o: io_stat.c io_stat.h util.h device.h print.h
	$(CC) $(CFLAGS) -c io_stat.c

timer.h: lock.h
	touch timer.h

timer.o: timer.c timer.h util.h cloud.h print.h random.h sequence.h \
        stp_beacon.h
	$(CC) $(CFLAGS) -c timer.c

ping.h: cloud.h
	touch ping.h

ping.o: ping.c ping.h util.h cloud.h print.h wrt_util.h nbr.h timer.h \
        cloud_msg.h device.h
	$(CC) $(CFLAGS) -c ping.c

print.h: util.h
	touch print.h

print.o: print.c util.h cloud.h print.h com_util.h
	$(CC) $(CFLAGS) -c print.c

sequence.h: cloud.h sequence_data.h
	touch sequence.h

sequence.o: sequence.c util.h print.h sequence.h cloud.h timer.h cloud_msg.h \
        nbr.h
	$(CC) $(CFLAGS) -c sequence.c

html_status.h: print.h graphit.h
	touch html_status.h

html_status.o: html_status.c util.h cloud.h print.h graphit.h html_status.h
	$(CC) $(CFLAGS) -c html_status.c

eth_util.h: cloud.h pio.h
	touch eth_util.h

eth_util.o: eth_util.c eth_util.h cloud.h mac_list.h pio.h
	$(CC) $(CFLAGS) -c eth_util.c

com_util.h: mac.h pio.h util.h
	touch com_util.h

com_util.o: com_util.c com_util.h util.h mac.h pio.h
	$(CC) $(CFLAGS) -c com_util.c

graphit.h: util.h
	touch graphit.h

graphit.o: graphit.c graphit.h util.h
	$(CC) $(CFLAGS) -c graphit.c

cloud_msg.h: cloud.h device.h cloud_msg_data.h
	touch cloud_msg.h

cloud_msg.o: cloud_msg.c print.h ad_hoc_client.h cloud_mod.h stp_beacon.h \
        ping.h sequence.h cloud.h cloud_msg.h io_stat.h nbr.h scan_msg.h
	$(CC) $(CFLAGS) -c cloud_msg.c

nbr.h: mac.h cloud.h
	touch nbr.h

nbr.o: nbr.c nbr.h util.h cloud.h print.h
	$(CC) $(CFLAGS) -c nbr.c

wrt_util.h: mac.h
	touch wrt_util.h

wrt_util.o: wrt_util.c wrt_util.h mac_list.h mac.h
	$(CC) $(CFLAGS) -c wrt_util.c

ad_hoc_client.h: util.h mac.h cloud.h
	touch ad_hoc_client.h

ad_hoc_client.o: ad_hoc_client.c cloud.h ad_hoc_client.h print.h lock.h \
        timer.h nbr.h device.h stp_beacon.h random.h
	$(CC) $(CFLAGS) -c ad_hoc_client.c

sequence_data.h: util.h
	touch sequence_data.h

lock_data.h: mac.h
	touch lock_data.h

stp_beacon_data.h: status.h cloud_data.h
	touch stp_beacon_data.h

cloud.h: mac.h util.h status.h pio.h stp_beacon_data.h cloud_data.h \
        cloud_msg_data.h lock_data.h scan_msg_data.h
	touch cloud.h

mac.h: util.h
	touch mac.h

mac_list.h: mac.h
	touch mac_list.h

pio.o: pio.c pio.h
	$(CC) $(CFLAGS) -c pio.c

lock.h: cloud.h mac.h
	touch lock.h

lock.o: lock.c lock.h timer.h cloud.h print.h cloud_msg.h
	$(CC) $(CFLAGS) -c lock.c

status.h: util.h mac.h device_type.h
	touch status.h

status.o: status.c status.h cloud.h util.h device_type.h print.h
	$(CC) $(CFLAGS) -c status.c

scan_msg_data.h: util.h
	touch scan_msg_data.h

scan_msg.h: cloud_msg.h scan_msg_data.h
	touch scan_msg.h

parm_change.o: parm_change.c parm_change.h
	$(CC) $(CFLAGS) -c parm_change.c

scan_msg.o: scan_msg.c scan_msg.h
	$(CC) $(CFLAGS) -c scan_msg.c

device_type.o: device_type.c device_type.h
	$(CC) $(CFLAGS) -c device_type.c

util.o: util.h util.c
	$(CC) $(CFLAGS) -c util.c

mac.o: mac.c mac.h util.h
	$(CC) $(CFLAGS) -c mac.c

mac_list.o: mac_list.c mac_list.h util.h
	$(CC) $(CFLAGS) -c mac_list.c

update_wrt_wds: update_wrt_wds.c mac.o mac_list.o util.o \
	mac.h mac_list.h util.h
	$(CC) $(CFLAGS) -o update_wrt_wds update_wrt_wds.c \
            mac.o mac_list.o util.o

ll_shell_ftp: ll_shell_ftp.c util.o mac.o pio.o com_util.o \
	mac.h util.h pio.h com_util.o
	$(CC) $(CFLAGS) \
        -o ll_shell_ftp ll_shell_ftp.c util.o mac.o pio.o \
            com_util.o
    
label:  label.c
	$(CC) $(CFLAGS) -o label label.c

clean:
	rm -f *.o $(PROGS)
