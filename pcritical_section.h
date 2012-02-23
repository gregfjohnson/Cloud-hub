/* pcritical_section.h - use named pipes to implement critical sections
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: pcritical_section.h,v 1.5 2012-02-22 18:55:25 greg Exp $
 */
#ifndef PCRITICAL_SECTION_H
#define PCRITICAL_SECTION_H

extern int pcritical_section_exit_sem(int sem);
extern char pcritical_section_can_enter_sem(int sem);
extern int pcritical_section_enter_sem(int sem);
extern int pcritical_section_init_sem(int *sem);
extern void pcritical_section_delete_sem(int sem);
extern int pcritical_section_getval_sem(int sem);
extern void pcritical_section_delete();
extern void pcritical_section_init();
extern int pcritical_section_init_named_sem(int *sem, char *name);
extern int pcritical_section_init_named(char *name);
extern char pcritical_section_can_enter();
extern int pcritical_section_getval();
extern void pcritical_section_enter();
extern void pcritical_section_exit();

#endif
