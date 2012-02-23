/* critical_section.h - use system semaphores to implement critical sections
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: critical_section.h,v 1.6 2012-02-22 18:55:24 greg Exp $
 */
#ifndef CRITICAL_SECTION_H
#define CRITICAL_SECTION_H

extern void critical_section_exit_sem(int sem);
extern char critical_section_can_enter_sem(int sem);
extern void critical_section_enter_sem(int sem);
extern void critical_section_init_sem(int *sem);
extern void critical_section_delete_sem(int sem);
extern int critical_section_getval_sem(int sem);
extern void critical_section_delete();
extern void critical_section_init();
extern char critical_section_can_enter();
extern int critical_section_getval();
extern void critical_section_enter();
extern void critical_section_exit();

#endif
